#!/usr/bin/env python3
"""
USB CDC (serial) <-> TCP bridge for a no-root Android/Termux setup.

The ESP32-S3 in a LILYGO T-HMI presents its USB port as a USB-Serial-JTAG /
CDC-ACM device (303A:1001 in ROM bootloader mode; CDC via
ARDUINO_USB_CDC_ON_BOOT in app mode). Android does not create /dev/tty* nodes
for such devices; it hands the app a raw file descriptor via UsbManager
(`termux-usb`). This bridge wraps that fd with libusb, drives the CDC-ACM
protocol in userspace, and exposes the serial byte stream as a TCP port, so
any normal serial client (pyserial over socket://, the PEPit filemanager,
nc) can talk to the device - including from inside the proot chroot (same
network namespace; 127.0.0.1 works).

Prerequisites (Termux, i.e. the Android side):
  - Termux:API app (F-Droid)
  - USB-OTG enabled + a data cable
  - cffi + libusb importable/dlopen-able by the python you run this with
      in Termux:  pkg install python cffi libusb
      in proot:    apt install libusb-1.0-0  (plus cffi for your python)

Usage:
  termux-usb -l                            # list devices -> /dev/bus/usb/001/00X
  termux-usb -r /dev/bus/usb/001/00X       # grant permission (tap OK on the phone)
  termux-usb -e "python3 cdc_bridge.py 2323" /dev/bus/usb/001/00X

  `termux-usb -e` appends the device fd number as the LAST argument.
  Can be run from the Termux shell OR from inside proot (the fd passes
  through either way; the fd must have been opened by the app).

  Then, in proot:
    python3 scripts/usb/sd_filemanager.py --port 2323

Notes:
  - The bridge NEVER toggles DTR/RTS (that would reset the chip). It only
    sends a nominal SET_LINE_CODING. To reset the device, use the serial
    "reset " command, the BOOT button, or esptool usb_reset separately.
  - When the device reboots, the USB device re-enumerates and the libusb
    handle goes stale. The bridge will notice and exit; re-run the
    termux-usb command (the permission is not revoked by a reboot).
"""

import select
import socket
import struct
import sys
import threading
import time

from cffi import FFI

# --------------------------------------------------------------------------
# libusb (C API, minimal subset)
# --------------------------------------------------------------------------

LIBUSB_CDEF = """
struct libusb_context;
struct libusb_device;
struct libusb_device_handle;
struct libusb_device_descriptor {
    uint8_t bLength, bDescriptorType;
    uint16_t bcdUSB;
    uint8_t bDeviceClass, bDeviceSubClass, bDeviceProtocol;
    uint8_t bMaxPacketSize0;
    uint16_t idVendor, idProduct, bcdDevice;
    uint8_t iManufacturer, iProduct, iSerialNumber, bNumConfigurations;
};
struct libusb_ss_endpoint_companion_descriptor;
struct libusb_endpoint_descriptor {
    uint8_t bLength, bDescriptorType;
    uint8_t bEndpointAddress;
    uint8_t bAttributes;
    uint16_t wMaxPacketSize;
    uint8_t bInterval;
    uint8_t bRefresh, bSynchAddress;
    struct libusb_ss_endpoint_companion_descriptor *extra;
    int extra_length;
};
struct libusb_interface_descriptor {
    uint8_t bLength, bDescriptorType;
    uint8_t bInterfaceNumber;
    uint8_t bAlternateSetting;
    uint8_t bEndpointCount;
    uint8_t bInterfaceClass, bInterfaceSubClass, bInterfaceProtocol;
    char iInterface;
    struct libusb_endpoint_descriptor endpoint[32];
    void *extra;
    int extra_length;
};
struct libusb_interface {
    struct libusb_interface_descriptor *altsetting[32];
    int num_altsetting;
};
struct libusb_config_descriptor {
    uint8_t bLength, bDescriptorType;
    uint16_t wTotalLength;
    uint8_t bNumInterfaces;
    uint8_t bConfigurationValue;
    char iConfiguration;
    uint8_t bmAttributes;
    uint8_t bMaxPower;
    struct libusb_interface *interface[32];
};
struct libusb_version {
    int major;
    int minor;
    int micro;
    int nano;
    char rc[2];
};

int libusb_init(struct libusb_context **);
int libusb_set_option(struct libusb_context *, int, int);
void libusb_exit(struct libusb_context *);
const struct libusb_version *libusb_get_version(void);
char *libusb_strerror(int rc);
int libusb_wrap_sys_device(struct libusb_context *, int, struct libusb_device_handle **);
struct libusb_device *libusb_get_device(struct libusb_device_handle *);
int libusb_get_device_descriptor(struct libusb_device *, struct libusb_device_descriptor *);
int libusb_get_active_config_descriptor(struct libusb_device *, struct libusb_config_descriptor **);
void libusb_free_config_descriptor(struct libusb_config_descriptor *);
int libusb_set_configuration(struct libusb_device_handle *, int);
int libusb_claim_interface(struct libusb_device_handle *, int);
int libusb_release_interface(struct libusb_device_handle *, int);
int libusb_close(struct libusb_device_handle *);
int libusb_control_transfer(struct libusb_device_handle *, uint8_t, uint8_t,
                            uint16_t, uint16_t, unsigned char *, int, uint32_t);
int libusb_bulk_transfer(struct libusb_device_handle *, uint8_t,
                         unsigned char *, int, int *, uint32_t);
"""

LIBUSB_SONAMES = [
    "libusb-1.0.so.0", "libusb-1.0.so.2", "libusb-1.0.so.2.0.0",
    "libusb-1.0.so.1", "libusb-1.0.so",
]

LIBUSB_TIMEOUT = -4  # LIBUSB_ERROR_TIMEOUT

# libusb_option values (stable numbering since 1.0.17; matches libusb.h
# in both the Termux 1.0.30 and Debian 1.0.27 builds):
LIBUSB_OPTION_LOG_LEVEL = 0
LIBUSB_OPTION_USE_USBDK = 1
LIBUSB_OPTION_NO_DEVICE_DISCOVERY = 2


def load_libusb():
    ffi = FFI()
    ffi.cdef(LIBUSB_CDEF)
    last_err = None
    for name in LIBUSB_SONAMES:
        try:
            lib = ffi.dlopen(name)
        except OSError as e:
            last_err = e
            continue
        v = lib.libusb_get_version()
        ver = f"{v.major}.{v.minor}.{v.micro}"
        print(f"cdc_bridge: loaded {name} (libusb {ver})")
        if (v.major, v.minor, v.micro) < (1, 0, 23):
            raise SystemExit(
                f"error: libusb {ver} is too old - libusb_wrap_sys_device "
                "requires >= 1.0.23 (in Termux: pkg install libusb)"
            )
        return ffi, lib
    raise SystemExit(
        f"error: could not load libusb (tried {', '.join(LIBUSB_SONAMES)}): {last_err}\n"
        "  in Termux:  pkg install libusb cffi\n"
        "  in proot:   apt install libusb-1.0-0  (and make sure cffi is importable)"
    )


# --------------------------------------------------------------------------
# CDC-ACM over the wrapped fd
# --------------------------------------------------------------------------

class CdcSerial:
    """A raw bidirectional byte stream to a USB CDC-ACM device.

    Discovers all data interfaces (class 0x0A) with bulk in/out endpoints of
    the active configuration. Bytes written go to the first bulk OUT
    endpoint; every bulk IN endpoint is pumped to reader callbacks.
    """

    def __init__(self, ffi, libusb, fd):
        self.ffi = ffi
        self.libusb = libusb
        self.fd = fd
        self._shutdown = threading.Event()   # device gone / fatal
        self._pump_stop = threading.Event()  # reader pumps requested to stop
        self._claimed = []
        self.in_eps = []
        self.out_ep = None

        # Android app sandbox: no /dev/bus/usb, no udev - a plain
        # libusb_init() would enumerate devices (opendir /sys/devices,
        # /dev/bus/usb) and fail with errno 13 (EACCES). Skip device
        # discovery entirely: a wrapped-fd handle
        # (libusb_wrap_sys_device) only issues plain usbfs ioctls on the fd
        # (which Android's uhost driver implements), so no backend
        # enumeration is needed at all. This is the official Termux wiki
        # pattern (usbtest.c sample).
        #
        # NOTE: option 0 is LOG_LEVEL, 2 is NO_DEVICE_DISCOVERY. Setting
        # defaults with ctx=NULL before libusb_init() is the documented way
        # to make libusb_init() honor them (deprecated in favor of
        # libusb_init_context() in 1.0.27+, but still fully functional).
        libusb.libusb_set_option(
            ffi.NULL, LIBUSB_OPTION_LOG_LEVEL, 1  # warnings: useful diagnostics
        )
        libusb.libusb_set_option(
            ffi.NULL, LIBUSB_OPTION_NO_DEVICE_DISCOVERY, 1
        )
        ctx_p = ffi.new("struct libusb_context **")
        r = libusb.libusb_init(ctx_p)
        if r != 0:
            try:
                what = ffi.string(libusb.libusb_strerror(r)).decode()
            except Exception:
                what = "?"
            raise SystemExit(f"libusb_init failed: {r} ({what})")
        self.ctx = ctx_p[0]

        handle_p = ffi.new("struct libusb_device_handle **")
        r = libusb.libusb_wrap_sys_device(self.ctx, fd, handle_p)
        if r != 0:
            self._cleanup()
            try:
                what = ffi.string(libusb.libusb_strerror(r)).decode()
            except Exception:
                what = "?"
            raise SystemExit(
                f"libusb_wrap_sys_device failed: {r} ({what}) - "
                f"fd={fd} (is it the right device? permission granted?)"
            )
        self.handle = handle_p[0]

        dev = libusb.libusb_get_device(self.handle)
        dd = ffi.new("struct libusb_device_descriptor *")
        if libusb.libusb_get_device_descriptor(dev, dd) == 0:
            print(f"cdc_bridge: attached USB device {dd.idVendor:04x}:{dd.idProduct:04x} "
                  f"(fd {fd})")
        else:
            print(f"cdc_bridge: attached USB device (fd {fd}, no descriptor)")

        cfg_p = ffi.new("struct libusb_config_descriptor **")
        if libusb.libusb_get_active_config_descriptor(dev, cfg_p) != 0:
            raise SystemExit("libusb_get_active_config_descriptor failed")
        cfg = cfg_p[0]
        if libusb.libusb_set_configuration(self.handle, cfg.bConfigurationValue) != 0:
            print("cdc_bridge: warning: set_configuration failed (continuing)")

        control_iface = None
        for i in range(cfg.bNumInterfaces):
            idesc = cfg.interface[i].altsetting[0]
            if idesc.bInterfaceClass == 0x02 and control_iface is None:
                control_iface = idesc.bInterfaceNumber
            if idesc.bInterfaceClass != 0x0A:
                continue
            for j in range(idesc.bEndpointCount):
                ep = idesc.endpoint[j]
                if ep.bAttributes & 0x03 != 0x02:  # only bulk
                    continue
                if ep.bEndpointAddress & 0x80:
                    self.in_eps.append(ep.bEndpointAddress)
                elif self.out_ep is None:
                    self.out_ep = ep.bEndpointAddress
            if libusb.libusb_claim_interface(self.handle, idesc.bInterfaceNumber) == 0:
                self._claimed.append(idesc.bInterfaceNumber)
            else:
                print(f"cdc_bridge: warning: claim_interface({idesc.bInterfaceNumber}) failed")

        if not self.in_eps or self.out_ep is None:
            raise SystemExit(
                "error: no CDC data interface with bulk in/out endpoints found - "
                "is the device in the right mode (app running or ROM bootloader)?"
            )

        # Nominal line coding; the S3 ignores baud over USB, but some stacks
        # expect the request. Deliberately does NOT touch DTR/RTS
        # (SET_CONTROL_LINE_STATE), which on the S3 would reset the chip.
        line_coding = ffi.new("unsigned char[7]")
        struct.pack_into("<IBBB", line_coding, 0, 115200, 0, 0, 8)
        if control_iface is not None:
            libusb.libusb_control_transfer(
                self.handle, 0x21, 0x20, 0, control_iface, line_coding, 1000
            )

        libusb.libusb_free_config_descriptor(cfg_p[0])
        print(f"cdc_bridge: in-eps={[hex(e) for e in self.in_eps]} "
              f"out-ep={self.out_ep:02x} -> ready")

    def _cleanup(self):
        if getattr(self, "handle", None) is None:
            return
        for i in self._claimed:
            self.libusb.libusb_release_interface(self.handle, i)
        self.libusb.libusb_close(self.handle)  # releases the wrapped fd's device ref
        self.handle = None
        self._claimed = []
        # libusb_exit() deinitializes the context (present in all 1.0.x builds)
        try:
            self.libusb.libusb_exit(self.ctx)
        except Exception:
            pass

    def _bulk(self, ep, buf, size, timeout_ms):
        n = self.ffi.new("int *")
        return self.libusb.libusb_bulk_transfer(self.handle, ep, buf, size, n, timeout_ms), n

    def write(self, data):
        """Write bytes to the bulk OUT endpoint. Raises OSError on device error."""
        sent = 0
        buf = self.ffi.new("unsigned char[]", data)
        while sent < len(data):
            if self._shutdown.is_set() or self.handle is None:
                raise OSError("device gone")
            r, n = self._bulk(self.out_ep, buf + sent, len(data) - sent, 5000)
            if r == LIBUSB_TIMEOUT:
                if n[0] > 0:
                    sent += n[0]
                continue
            if r != 0:
                raise OSError(f"bulk OUT failed: {r} (device gone?)")
            if n[0] == 0:
                self._shutdown.wait(0.01)
                continue
            sent += n[0]
        return sent

    def pump_in(self, on_data, poll_timeout=0.2):
        """Start reader threads; on_data(bytes) is called per bulk IN chunk."""
        self._pump_stop.clear()
        for ep in self.in_eps:
            threading.Thread(target=self._pump_one, args=(ep, on_data, poll_timeout),
                             daemon=True).start()

    def stop_pumps(self, timeout=1.0):
        """Ask reader pumps to stop (e.g. when the TCP client goes away)."""
        self._pump_stop.set()
        self._pump_stop.wait(0)  # yields; pump threads exit on their next iteration

    def _pump_one(self, ep, on_data, poll_timeout):
        buf = self.ffi.new("unsigned char[65536]")
        while not self._shutdown.is_set() and not self._pump_stop.is_set() \
                and self.handle is not None:
            r, n = self._bulk(ep, buf, 65536, int(poll_timeout * 1000))
            if r == LIBUSB_TIMEOUT:
                continue
            if r != 0:
                print(f"cdc_bridge: bulk IN on {ep:02x} failed: {r} (device gone?)")
                self._shutdown.set()
                return
            if n[0] > 0:
                try:
                    on_data(self.ffi.buffer(buf, n[0])[:])
                except (OSError, ConnectionError):
                    self._shutdown.set()
                    return

    def close(self):
        if not self._shutdown.is_set():
            self._shutdown.set()
            self._cleanup()


# --------------------------------------------------------------------------
# TCP side
# --------------------------------------------------------------------------

def serve(serial, host="127.0.0.1", port=2323):
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((host, port))
    srv.listen(1)
    print(f"cdc_bridge: listening on tcp://{host}:{port}")

    while True:
        conn, addr = srv.accept()
        conn.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)
        try:  # Linux: keepalive to detect a dead client quickly
            conn.setsockopt(socket.IPPROTO_TCP, 8, 10)  # TCP_KEEPIDLE
            conn.setsockopt(socket.IPPROTO_TCP, 6, 3)   # TCP_KEEPINTVL
            conn.setsockopt(socket.IPPROTO_TCP, 7, 3)   # TCP_KEEPCNT
        except OSError:
            pass
        print(f"cdc_bridge: client connected from {addr[0]}:{addr[1]}")

        client = {"alive": True}

        def writer():
            try:
                while client["alive"] and not serial._shutdown.is_set():
                    data = conn.recv(65536)
                    if not data:
                        break
                    serial.write(data)
            except (OSError, ConnectionError):
                pass
            finally:
                client["alive"] = False
                try:
                    conn.close()
                except OSError:
                    pass
            print("cdc_bridge: client disconnected")

        def on_data(data):
            if not client["alive"]:
                return
            try:
                conn.sendall(data)
            except (OSError, ConnectionError):
                client["alive"] = False

        serial.pump_in(on_data)
        threading.Thread(target=writer, daemon=True).start()

        # wait until client or device dies
        while client["alive"] and not serial._shutdown.is_set():
            select.select([conn], [], [], 0.25)
            # a half-closed client shows up as a zero recv in the writer;
            # the keepalive/recv error there flips client["alive"]

        if client["alive"]:
            serial.stop_pumps()
            time.sleep(0.6)  # let pump threads exit before accepting a new client
        if serial._shutdown.is_set():
            print("cdc_bridge: device gone - re-run the termux-usb command to reconnect")
            return


def main():
    args = sys.argv[1:]
    if len(args) < 1:
        print(__doc__)
        sys.exit(1)
    port = int(args[0])
    if len(args) < 2 or not args[-1].isdigit():
        print("error: no device fd argument.\n"
              f"  run via:  termux-usb -e \"python3 cdc_bridge.py {port}\" <device>")
        sys.exit(1)
    fd = int(args[-1])

    ffi, libusb = load_libusb()
    serial = CdcSerial(ffi, libusb, fd)
    try:
        serve(serial, port=port)
    except KeyboardInterrupt:
        pass
    finally:
        serial.close()


if __name__ == "__main__":
    main()
