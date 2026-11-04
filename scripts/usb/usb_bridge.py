#!/usr/bin/env python3
"""
usb_bridge.py - pure-stdlib USB CDC-serial <-> TCP bridge for the LILYGO T-HMI
(ESP32-S3), for a no-root Android / Termux / termux-usb setup.

Depends ONLY on the Python standard library (ctypes, fcntl, os, select,
socket, struct, threading). No cffi, no libusb, no pyusb - so it is immune
to a broken/incompatible native stack in the Termux prefix (the libusb/cffi
route segfaults on the rebuilt python3.14).

HOW IT WORKS (every call verified against the real sources)

  `termux-usb -e "cmd" <devpath>` passes the file descriptor that the
  Android app opened on the USB device (the "uhost" driver's file) as the
  LAST argument. AOSP's libusbhost (platform/system/core/libusbhost/
  usbhost.c) drives that very fd, in blocking mode, with the kernel's
  classic usbdevfs ioctls (include/uapi/linux/usbdevice_fs.h,
  drivers/usb/core/devio.c):

    read() after lseek(0)  -> device + configuration descriptors
    USBDEVFS_CONTROL       -> struct usbdevfs_ctrltransfer (24 bytes);
                               kernel runs usb_control_msg() with a timeout
    USBDEVFS_BULK          -> struct usbdevfs_bulktransfer (24 bytes);
                               kernel runs usb_bulk_msg() with a timeout
                               (O_NONBLOCK is irrelevant to these; the
                               kernel-side timeout bounds every call)
    USBDEVFS_SETCONFIGURATION / USBDEVFS_CLAIMINTERFACE /
    USBDEVFS_RELEASEINTERFACE / USBDEVFS_SETINTERFACE /
    USBDEVFS_CLEAR_HALT / USBDEVFS_GET_SPEED / USBDEVFS_CONNECTINFO

  The ioctl RETURN VALUE is the number of bytes transferred (>= 0); on
  failure the kernel returns -errno, which Python surfaces as OSError
  (EPIPE = endpoint stalled, ENODEV/ECONNRESET/ESHUTDOWN = device gone).
  usbhost.c relies on exactly this contract, and it is what this script
  uses. No URB/poll machinery is needed: each transfer is a bounded
  blocking syscall, which is the simplest design that works.

  The ESP32-S3 USB-Serial-JTAG presents CDC-style descriptors
  (0x303A:0x1001) in both ROM bootloader and app mode. We read the
  descriptors, find the CDC-Data interface (class 0x0A) and its bulk
  IN/OUT endpoints, claim it, and pump.

USAGE (in the Termux shell, NOT from proot):

    cd ~
    termux-usb -l                       # list devices
    termux-usb -e "python3 usb_bridge.py 2323" /dev/bus/usb/001/00X
                                         # tap OK on the permission dialog

  The selftest then exercises every step and prints one line per step;
  you should end with:

    usb_bridge: ... -> ready
    usb_bridge: listening on tcp://127.0.0.1:2323

  Then from proot (same network namespace, same phone):

    python3 /root/repos/PEPit/scripts/usb/sd_filemanager.py --port 2323
    # or:
    python3 /root/repos/PEPit/scripts/usb/sd_filemanager.py --port 2323 \
        --batch "ul /path/firmware.bin /firmware.bin" \
                "ul /path/SDCardContent.tar /SDCardContent.tar" "reset"

NOTES

  * The bridge NEVER sends SET_CONTROL_LINE_STATE (DTR/RTS) - on the
    ESP32-S3 that resets the chip. Only a nominal, best-effort
    SET_LINE_CODING is sent (the USB-Serial-JTAG may stall it; harmless).
    To reset the device, use the serial `reset` command or the BOOT button.
  * When the device reboots (e.g. mid self-flash) the uhost fd goes stale.
    A watchdog bulk-IN probe notices within ~1 s and the bridge exits with
    a message. Re-run the termux-usb command (permission is not revoked).
"""

import ctypes
import errno
import fcntl
import os
import select
import socket
import struct
import sys
import threading
import time

# ---------------------------------------------------------------------------
# usbdevfs ioctl numbers (include/uapi/linux/usbdevice_fs.h)
# _IOC(dir, type, nr, size) = (dir << 30) | (size << 16) | (type << 8) | nr
# sizes must match the kernel structs exactly.
# ---------------------------------------------------------------------------

def _IOC(direction, type_, nr, size):
    return (direction << 30) | (size << 16) | (type_ << 8) | nr

_IOC_NONE, _IOC_WRITE, _IOC_READ, _IOC_READWRITE = 0, 1, 2, 3
_U = 0x55  # 'U'

USBDEVFS_CONTROL         = _IOC(_IOC_READWRITE, _U, 0, 24)   # usbdevfs_ctrltransfer
USBDEVFS_BULK            = _IOC(_IOC_READWRITE, _U, 2, 24)   # usbdevfs_bulktransfer
USBDEVFS_SETINTERFACE    = _IOC(_IOC_READ, _U, 4, 8)         # usbdevfs_setinterface
USBDEVFS_SETCONFIGURATION = _IOC(_IOC_READ, _U, 5, 4)        # int
USBDEVFS_CLAIMINTERFACE    = _IOC(_IOC_READ, _U, 15, 4)      # int
USBDEVFS_RELEASEINTERFACE  = _IOC(_IOC_READ, _U, 16, 4)      # int
USBDEVFS_CONNECTINFO       = _IOC(_IOC_WRITE, _U, 17, 8)     # usbdevfs_connectinfo
USBDEVFS_RESET             = _IOC(_IOC_NONE, _U, 20, 0)
USBDEVFS_CLEAR_HALT        = _IOC(_IOC_READ, _U, 21, 4)      # int
USBDEVFS_GET_SPEED         = _IOC(_IOC_NONE, _U, 31, 0)      # speed comes back as the ioctl return value

# USB descriptor / class / request codes
DT_DEVICE, DT_CONFIG, DT_INTERFACE, DT_ENDPOINT = 1, 2, 4, 5
CLASS_CDC, CLASS_CDC_DATA = 0x02, 0x0A
CDC_SET_LINE_CODING = 0x20
# SET_CONTROL_LINE_STATE = 0x22  -- deliberately NOT used (resets the S3)

O_NONBLOCK = os.O_NONBLOCK
SPEED_NAMES = {0: "unknown", 1: "low", 2: "full", 3: "high",
               5: "super", 6: "super+"}

# errno values that mean "the device is gone / the fd is dead"
GONE_ERRNOS = (errno.ENODEV, errno.ESHUTDOWN, errno.ECONNRESET,
               errno.ESRCH, errno.EBADF)


class DeviceGone(Exception):
    """The USB device disappeared or the fd is no longer usable."""


def _is_gone_err(e):
    return e.errno in GONE_ERRNOS


# ---------------------------------------------------------------------------
# ctypes mirrors of the kernel uapi structs
# ---------------------------------------------------------------------------

class usbdevfs_ctrltransfer(ctypes.Structure):
    # 24 bytes: 1+1+2+2+2 (+2 pad) +4 (+4 pad) +8
    _fields_ = [
        ("bmRequestType", ctypes.c_ubyte),
        ("bRequest", ctypes.c_ubyte),
        ("wValue", ctypes.c_uint16),
        ("wIndex", ctypes.c_uint16),
        ("wLength", ctypes.c_uint16),
        ("timeout", ctypes.c_uint32),
        ("data", ctypes.c_void_p),
    ]


class usbdevfs_bulktransfer(ctypes.Structure):
    # 24 bytes: 4+4+4 (+4 pad) +8
    _fields_ = [
        ("ep", ctypes.c_uint32),
        ("len", ctypes.c_int32),
        ("timeout", ctypes.c_uint32),
        ("data", ctypes.c_void_p),
    ]


class usbdevfs_setinterface(ctypes.Structure):
    _fields_ = [
        ("interface", ctypes.c_uint32),
        ("altsetting", ctypes.c_uint32),
    ]


class usbdevfs_connectinfo(ctypes.Structure):
    _fields_ = [
        ("devnum", ctypes.c_uint32),
        ("slow", ctypes.c_ubyte),   # padded to 8
    ]


# ---------------------------------------------------------------------------
# USB descriptor parsing (pure python)
# ---------------------------------------------------------------------------

class Endpoint:
    def __init__(self, addr, xfer_type, max_packet, iface):
        self.addr = addr
        self.xfer_type = xfer_type
        self.max_packet = max_packet & 0x07FF
        self.iface = iface


class Interface:
    def __init__(self, number, cls, sub, proto, alt=0):
        self.number = number
        self.cls = cls
        self.sub = sub
        self.proto = proto
        self.alt = alt
        self.endpoints = []


class Config:
    def __init__(self, value, interfaces):
        self.value = value
        self.interfaces = interfaces


def parse_config(blob, off):
    """Parse one configuration descriptor block starting at blob[off]."""
    bLength, bDescriptorType, wTotal, bNumIfaces, bConfigValue = \
        struct.unpack_from("<BBHBB", blob, off)
    if bDescriptorType != DT_CONFIG:
        raise ValueError(f"expected config descriptor, got type {bDescriptorType:#x}")
    pos = off + bLength
    end = off + wTotal
    interfaces = []
    while pos < end:
        bLength, bType = struct.unpack_from("<BB", blob, pos)
        if bLength == 0:
            break
        if bType == DT_INTERFACE:
            _, _, bNum, bAlt, bEP, bCls, bSub, bProto, _iStr = \
                struct.unpack_from("<BBBBBBBBB", blob, pos)
            itf = Interface(bNum, bCls, bSub, bProto, bAlt)
            interfaces.append(itf)
            pos += bLength
            for _ in range(bEP):
                if pos + 6 > len(blob):
                    break
                bLength2, bType2 = struct.unpack_from("<BB", blob, pos)
                if bType2 != DT_ENDPOINT or bLength2 < 6:
                    break
                _, _, bAddr, bAttrs, wMax = struct.unpack_from("<BBBBH", blob, pos)
                itf.endpoints.append(
                    Endpoint(bAddr, bAttrs & 0x03, wMax, itf.number))
                pos += bLength2
        else:
            pos += bLength  # class-specific / string / other
    return Config(bConfigValue, interfaces)


def parse_descriptors(blob):
    """Parse the raw descriptor blob (device + all configurations).

    Returns (device_dict, [Config, ...]).
    """
    if len(blob) < 18:
        raise ValueError(f"descriptor blob too short ({len(blob)} bytes)")
    (bLength, bType, bcdUSB, bClass, bSub, bProto, bMax0,
     idVendor, idProduct, bcdDevice, iMfr, iProd, iSer, bNumCfg) = \
        struct.unpack_from("<BBHBBBBHHHBBBB", blob, 0)
    if bType != DT_DEVICE:
        raise ValueError(f"first descriptor is not a device descriptor ({bType:#x})")
    dev = {
        "idVendor": idVendor, "idProduct": idProduct, "bcdDevice": bcdDevice,
        "bcdUSB": bcdUSB, "bClass": bClass, "bSubClass": bSub,
        "bProtocol": bProto,
        "bNumConfigurations": bNumCfg,
    }
    configs = []
    off = bLength
    for _ in range(bNumCfg):
        if off + 9 > len(blob):
            break
        cfg = parse_config(blob, off)
        configs.append(cfg)
        off += struct.unpack_from("<H", blob, off + 2)[0]
    return dev, configs


# ---------------------------------------------------------------------------
# The usbdevfs device (one fd - uhost on Android, blocking mode)
# ---------------------------------------------------------------------------

class UsbDevfs:
    """Raw usbdevfs client on a file descriptor.

    Blocking mode: every transfer is a bounded blocking ioctl (the kernel
    enforces the timeout). This is exactly how AOSP libusbhost drives the
    uhost fd (usbhost.c makes no fcntl/O_NONBLOCK calls at all).
    """

    def __init__(self, fd, path=None, log=print):
        self.fd = fd
        self.path = path
        self._log = log
        # If the fd we were handed is not read-write, re-open the device
        # path the way usbhost.c does (open(dev_name, O_RDWR)).
        try:
            flags = fcntl.fcntl(self.fd, fcntl.F_GETFL)
        except OSError:
            flags = None
        if flags is not None and (flags & 3) != os.O_RDWR and path:
            try:
                new_fd = os.open(path, os.O_RDWR)
                self._log(f"usb_bridge: fd {fd} is not RW (mode {flags & 3:#x}), "
                          f"reopened {path} -> fd {new_fd}")
                self.fd = new_fd
            except OSError as e:
                self._log(f"usb_bridge: could not reopen {path} as RW: {e}")

    def _ioctl(self, request, arg=None):
        try:
            if arg is None:
                return fcntl.ioctl(self.fd, request)
            return fcntl.ioctl(self.fd, request, arg)
        except OSError as e:
            if _is_gone_err(e):
                raise DeviceGone(str(e)) from e
            raise

    # -- descriptors / info -------------------------------------------------

    def read_descriptors(self):
        """Read the whole descriptor blob (lseek(0) + read, like usbhost.c
        usb_device_new())."""
        os.lseek(self.fd, 0, os.SEEK_SET)
        blob = b""
        while len(blob) < 1_000_000:  # safety cap; S3 descriptors are < 1 KB
            chunk = os.read(self.fd, 256)
            if not chunk:
                break
            blob += chunk
        return blob

    def get_speed(self):
        # the speed code comes back as the ioctl return value
        return self._ioctl(USBDEVFS_GET_SPEED)

    def get_capabilities(self):
        buf = ctypes.create_string_buffer(4)
        self._ioctl(_IOC(_IOC_READ, _U, 26, 4), buf)
        return struct.unpack_from("<I", buf.raw)[0]

    def connectinfo(self):
        ci = usbdevfs_connectinfo()
        self._ioctl(USBDEVFS_CONNECTINFO, ci)
        return ci.devnum, ci.slow

    # -- configuration / interfaces -----------------------------------------

    def set_configuration(self, value):
        self._ioctl(USBDEVFS_SETCONFIGURATION, struct.pack("<i", value))

    def claim_interface(self, iface):
        self._ioctl(USBDEVFS_CLAIMINTERFACE, struct.pack("<i", iface))

    def release_interface(self, iface):
        self._ioctl(USBDEVFS_RELEASEINTERFACE, struct.pack("<i", iface))

    def set_interface_alt(self, iface, alt):
        si = usbdevfs_setinterface(interface=iface, altsetting=alt)
        self._ioctl(USBDEVFS_SETINTERFACE, si)

    def clear_halt(self, endpoint):
        self._ioctl(USBDEVFS_CLEAR_HALT, struct.pack("<i", endpoint))

    def reset_device(self):
        self._ioctl(USBDEVFS_RESET)

    # -- transfers ------------------------------------------------------------

    def control(self, bmRequestType, bRequest, wValue=0, wIndex=0,
                data=None, timeout_ms=2000):
        """One control transfer (kernel does usb_control_msg with timeout).

        Returns the number of bytes received (0 for status/OUT requests).
        """
        payload = bytes(data) if data is not None else b""
        if bmRequestType & 0x80:
            # device->host: buffer must hold the expected reply
            size = len(payload) if payload else 1
        else:
            size = len(payload)
        buf = (ctypes.create_string_buffer(payload, size) if payload
               else ctypes.create_string_buffer(size))
        c = usbdevfs_ctrltransfer(
            bmRequestType=bmRequestType & 0xFF,
            bRequest=bRequest & 0xFF,
            wValue=wValue & 0xFFFF,
            wIndex=wIndex & 0xFFFF,
            wLength=len(payload) & 0xFFFF,
            timeout=timeout_ms,
            data=ctypes.addressof(buf),
        )
        n = self._ioctl(USBDEVFS_CONTROL, c)
        if n < 0:
            raise OSError(-n, os.strerror(-n))
        return n

    def bulk_in(self, endpoint, length, timeout_ms):
        """One bulk IN transfer. Returns bytes received (b'' on timeout).

        The kernel runs usb_bulk_msg(): it returns whatever arrived within
        timeout_ms (possibly nothing) - this is the pump primitive.
        """
        buf = ctypes.create_string_buffer(length)
        bt = usbdevfs_bulktransfer(
            ep=endpoint & 0xFF, len=length,
            timeout=timeout_ms, data=ctypes.addressof(buf))
        n = self._ioctl(USBDEVFS_BULK, bt)
        if n < 0:
            raise OSError(-n, os.strerror(-n))
        return buf.raw[:max(n, 0)]

    def bulk_out(self, endpoint, data, timeout_ms):
        """One bulk OUT transfer of `data`. Returns bytes transferred."""
        data = bytes(data)
        buf = ctypes.create_string_buffer(data if data else b"\x00")
        bt = usbdevfs_bulktransfer(
            ep=endpoint & 0xFF, len=len(data),
            timeout=timeout_ms, data=ctypes.addressof(buf))
        n = self._ioctl(USBDEVFS_BULK, bt)
        if n < 0:
            raise OSError(-n, os.strerror(-n))
        # modern kernel: n = bytes sent (a bulk OUT is all-or-nothing);
        # return len(data) if the kernel reported 0 (success, older kernels)
        return n if n > 0 else len(data)


# ---------------------------------------------------------------------------
# CDC-ACM serial over UsbDevfs (duck-types cdc_bridge.CdcSerial)
# ---------------------------------------------------------------------------

class CdcSerial:
    def __init__(self, fd, path=None, selftest=True):
        self._log = lambda m: print(m, flush=True)
        self.usb = UsbDevfs(fd, path=path, log=self._log)
        self._shutdown = threading.Event()    # device gone / fatal
        self._pump_stop = threading.Event()   # reader pumps: stop
        self._io_lock = threading.Lock()      # serialize writes
        self._claimed = []
        self.in_eps = []
        self.out_ep = None
        self.control_iface = None
        self.data_iface = None

        # 1. descriptors
        blob = self.usb.read_descriptors()
        dev, configs = parse_descriptors(blob)
        if not configs:
            raise SystemExit("error: no configuration descriptors found - "
                             "is this really the T-HMI?")
        cfg = configs[0]
        if selftest:
            speed = None
            try:
                speed = self.usb.get_speed()
            except OSError:
                pass
            self._log(f"usb_bridge: device {dev['idVendor']:04x}:"
                      f"{dev['idProduct']:04x} bcdUSB={dev['bcdUSB']:04x} "
                      f"class={dev['bClass']:02x}:{dev['bSubClass']:02x} "
                      f"configs={len(configs)} "
                      f"speed={SPEED_NAMES.get(speed, speed)}")

        # 2. find CDC control + data interfaces / bulk endpoints
        for itf in cfg.interfaces:
            if itf.cls == CLASS_CDC and self.control_iface is None:
                self.control_iface = itf.number
            elif itf.cls == CLASS_CDC_DATA:
                self.data_iface = itf.number
                for ep in itf.endpoints:
                    if ep.xfer_type != 2:  # bulk only
                        continue
                    if ep.addr & 0x80:
                        self.in_eps.append(ep.addr)
                    elif self.out_ep is None:
                        self.out_ep = ep.addr
        if selftest:
            for itf in cfg.interfaces:
                eps = " ".join(f"0x{e.addr:02x}(t{e.xfer_type},mp{e.max_packet})"
                               for e in itf.endpoints)
                self._log(f"usb_bridge: iface {itf.number} class "
                          f"{itf.cls:02x}:{itf.sub:02x}:{itf.proto:02x} "
                          f"alt={itf.alt} ep=[{eps}]")

        if not self.in_eps or self.out_ep is None:
            raise SystemExit(
                "error: no CDC-data interface with bulk IN/OUT endpoints. "
                "Is the device running the app (USB-Serial-JTAG CDC) and "
                "not the ROM bootloader?")

        # 3. configuration + claims
        try:
            self.usb.set_configuration(cfg.value)
            if selftest:
                self._log(f"usb_bridge: set_configuration({cfg.value}) ok")
        except OSError as e:
            self._log(f"usb_bridge: set_configuration({cfg.value}) failed: "
                      f"{e} (continuing - probably already set)")
        if self.data_iface is not None:
            try:
                self.usb.claim_interface(self.data_iface)
                self._claimed.append(self.data_iface)
                if selftest:
                    self._log(f"usb_bridge: claimed data iface {self.data_iface}")
            except DeviceGone:
                raise
            except OSError as e:
                self._log(f"usb_bridge: claim(data iface {self.data_iface}) "
                          f"failed: {e.errno} {os.strerror(e.errno)} "
                          f"(a host kernel driver may hold the interface)")
        if self.control_iface is not None and self.control_iface not in self._claimed:
            try:
                self.usb.claim_interface(self.control_iface)
                self._claimed.append(self.control_iface)
                if selftest:
                    self._log(f"usb_bridge: claimed control iface {self.control_iface}")
            except DeviceGone:
                raise
            except OSError as e:
                self._log(f"usb_bridge: claim(control iface {self.control_iface}) "
                          f"failed: {e.errno} {os.strerror(e.errno)} (continuing)")
        if not self._claimed:
            # A kernel driver (e.g. cdc_acm, which auto-binds to standard
            # CDC-ACM devices) can hold the interface, making usbdevfs
            # claims fail with EBUSY. That does NOT prevent raw usbfs
            # transfers: usb_bulk_msg()/usb_control_msg() do not check
            # interface ownership, so the endpoints are still usable (this
            # is why libusb/cdc_bridge works in the same situation).
            # The selftest below is the real gate: if the transfers work,
            # we proceed unclaimed.
            self._log("usb_bridge: no interface could be claimed - a host "
                      "kernel driver appears to hold it. Continuing anyway: "
                      "usbfs transfers usually still work; the selftest will "
                      "prove it.")

        # 4. nominal line coding. Never touch DTR/RTS (SET_CONTROL_LINE_STATE)
        #    - on the S3 that would reset the chip.
        if self.control_iface is not None:
            line_coding = struct.pack("<IBBB", 115200, 0, 0, 8)
            try:
                self.usb.control(0x21, CDC_SET_LINE_CODING,
                                 wIndex=self.control_iface, data=line_coding)
                if selftest:
                    self._log("usb_bridge: SET_LINE_CODING ok")
            except DeviceGone:
                raise
            except OSError as e:
                # USB-Serial-JTAG may not implement ACM control functions - fine
                if selftest:
                    self._log(f"usb_bridge: SET_LINE_CODING: {e} (continuing)")

        # 5. selftest of the transfer machinery
        if selftest:
            self._selftest()

        self._log(f"usb_bridge: in-eps={[hex(e) for e in self.in_eps]} "
                  f"out-ep=0x{self.out_ep:02x} "
                  f"control-iface={self.control_iface} data-iface={self.data_iface} "
                  f"claimed={self._claimed or 'NONE'} -> ready")

    def _selftest(self):
        t0 = time.time()
        try:
            got = self.usb.bulk_in(self.in_eps[0], 64, 200)
            self._log(f"usb_bridge: selftest bulk-IN(200ms) -> {len(got)} bytes "
                      f"({got[:40]!r}) [device quiet = good] "
                      f"(total {time.time() - t0:.2f}s)")
        except DeviceGone:
            raise SystemExit("usb_bridge: selftest: device gone")
        except OSError as e:
            raise SystemExit(f"usb_bridge: selftest bulk-IN failed: {e.errno} "
                             f"{os.strerror(e.errno)} - the endpoint is not "
                             f"usable (if claims failed above, a bound kernel "
                             f"driver such as cdc_acm is holding it; without "
                             f"root the app cannot unbind it)")
        try:
            self.usb.bulk_out(self.out_ep, b"\n", 1000)
            self._log("usb_bridge: selftest bulk-OUT(1 byte) ok")
        except DeviceGone:
            raise SystemExit("usb_bridge: selftest: device gone")
        except OSError as e:
            # a stall here would be a real problem for the upload path
            try:
                self.usb.clear_halt(self.out_ep)
                self.usb.bulk_out(self.out_ep, b"\n", 1000)
                self._log(f"usb_bridge: selftest bulk-OUT stalled ({e}), "
                          f"cleared halt, retry ok")
            except OSError as e2:
                raise SystemExit(f"usb_bridge: selftest bulk-OUT failed: "
                                 f"{e} then {e2}")

    # -- CdcSerial-compatible API --------------------------------------------

    def write(self, data):
        """Write to the bulk OUT endpoint. Returns bytes written."""
        data = bytes(data)
        if not data:
            return 0
        sent = 0
        with self._io_lock:
            while sent < len(data):
                if self._shutdown.is_set():
                    raise DeviceGone("device gone")
                chunk = data[sent:sent + 16384]
                for attempt in range(3):
                    try:
                        n = self.usb.bulk_out(self.out_ep, chunk, 3000)
                        break
                    except DeviceGone:
                        raise
                    except OSError as e:
                        if _is_gone_err(e):
                            raise DeviceGone(str(e)) from e
                        if e.errno == errno.EPIPE:  # endpoint stalled
                            try:
                                self.usb.clear_halt(self.out_ep)
                            except OSError:
                                pass
                            continue
                        raise
                else:
                    raise OSError(f"bulk OUT to 0x{self.out_ep:02x} failed "
                                  f"after retries")
                sent += n if n > 0 else len(chunk)
        return sent

    def pump_in(self, on_data, poll_timeout_ms=100):
        """Start reader threads (one per bulk IN ep); on_data(bytes)."""
        self._pump_stop.clear()
        for ep in self.in_eps:
            threading.Thread(target=self._pump_one,
                             args=(ep, on_data, poll_timeout_ms),
                             daemon=True).start()

    def stop_pumps(self, timeout=1.0):
        self._pump_stop.set()

    def _pump_one(self, ep, on_data, poll_timeout_ms):
        while not self._shutdown.is_set() and not self._pump_stop.is_set():
            try:
                data = self.usb.bulk_in(ep, 65536, poll_timeout_ms)
            except DeviceGone:
                self._shutdown.set()
                break
            except OSError as e:
                if _is_gone_err(e):
                    self._shutdown.set()
                    break
                self._log(f"usb_bridge: pump 0x{ep:02x}: {e}")
                self._pump_stop.wait(0.5)
                continue
            if data:
                try:
                    on_data(data)
                except (OSError, ConnectionError):
                    # client went away; serve() will stop the pumps
                    self._pump_stop.set()
                    break

    def close(self):
        self._shutdown.set()
        self._pump_stop.set()
        for i in self._claimed:
            try:
                self.usb.release_interface(i)
            except (OSError, DeviceGone):
                pass
        self._claimed = []
        # NOTE: never close(self.usb.fd) - it belongs to the app (uhost fd).
        # Never send SET_CONTROL_LINE_STATE either (would reset the S3).


# ---------------------------------------------------------------------------
# TCP side
# ---------------------------------------------------------------------------

def serve(serial, host="127.0.0.1", port=2323):
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((host, port))
    srv.listen(1)
    print(f"usb_bridge: listening on tcp://{host}:{port} (Ctrl-C to stop)",
          flush=True)

    try:
        while not serial._shutdown.is_set():
            # ---- wait for a client (with a device-death watchdog) --------
            conn = None
            while not serial._shutdown.is_set():
                poller = select.poll()
                poller.register(srv, select.POLLIN)
                try:
                    events = poller.poll(1000)
                except OSError:
                    return
                # watchdog: a short bulk-IN probe notices a dead device
                # within ~1 s even while no client is connected (and drains
                # any bytes the device emitted meanwhile, which have no
                # receiver anyway)
                if not events and serial.in_eps:
                    try:
                        serial.usb.bulk_in(serial.in_eps[0], 4096, 10)
                    except DeviceGone:
                        serial._shutdown.set()
                        break
                    except OSError:
                        pass
                if serial._shutdown.is_set():
                    break
                if events:
                    try:
                        conn, addr = srv.accept()
                    except OSError as e:
                        print(f"usb_bridge: accept failed: {e}", flush=True)
                        continue
                    break
            if serial._shutdown.is_set():
                print("usb_bridge: device gone - re-run the termux-usb "
                      "command to reconnect", flush=True)
                return

            # -- one client at a time ---------------------------------------
            # No TCP keepalive: this is a loopback link to a serial device. The
        # client can legitimately be silent for a long time (waiting on slow
        # USB transfers / device responses), so an aggressive keepalive would
        # RST live sessions. The writer's recv() EOF + the device watchdog
        # already detect the dead ends.
        print(f"usb_bridge: client connected from {addr[0]}:{addr[1]}",
              flush=True)

        client = {"alive": True}

        def writer():
            try:
                while client["alive"] and not serial._shutdown.is_set():
                    data = conn.recv(65536)
                    if not data:
                        break
                    serial.write(data)
            except DeviceGone:
                pass
            except (OSError, ConnectionError):
                pass
            finally:
                client["alive"] = False
                try:
                    conn.close()
                except OSError:
                    pass
            print("usb_bridge: client disconnected", flush=True)

        def on_data(data):
            if not client["alive"]:
                return
            try:
                conn.sendall(data)
            except (OSError, ConnectionError):
                client["alive"] = False

        serial.pump_in(on_data)
        threading.Thread(target=writer, daemon=True).start()

        while client["alive"] and not serial._shutdown.is_set():
            try:
                select.select([conn], [], [], 0.25)
            except OSError:
                break
            if not client["alive"]:
                break

        if client["alive"]:
            serial.stop_pumps()
            time.sleep(0.3)
        if serial._shutdown.is_set():
            print("usb_bridge: device gone - re-run the termux-usb command "
                  "to reconnect", flush=True)
            return
        # else: loop back to accept()
    finally:
        try:
            srv.close()
        except OSError:
            pass
        serial.close()


def main():
    args = sys.argv[1:]
    if len(args) < 1 or args[0] in ("-h", "--help"):
        print(__doc__)
        sys.exit(1)
    port = int(args[0])

    fd = None
    if len(args) >= 2 and args[-1].isdigit():
        fd = int(args[-1])
    else:
        env_fd = os.environ.get("TERMUX_USB_FD")
        if env_fd and env_fd.isdigit():
            fd = int(env_fd)
    if fd is None:
        print("error: no device fd argument.\n"
              '  run via:  termux-usb -e "python3 usb_bridge.py %d" <device>'
              % port)
        sys.exit(1)

    # The path (for the RW re-open fallback); /proc/self/fd works in-app.
    path = None
    try:
        path = os.readlink(f"/proc/self/fd/{fd}")
    except OSError:
        pass

    serial = CdcSerial(fd, path=path)
    try:
        serve(serial, port=port)
    except KeyboardInterrupt:
        pass
    finally:
        serial.close()


if __name__ == "__main__":
    main()
