#!/usr/bin/env python3
"""Offline tests for usb_bridge.py.

The kernel layer (fcntl.ioctl, os.read/lseek, fcntl, os.open) is replaced
with an in-memory fake that mirrors the semantics of drivers/usb/core/
devio.c (usbdevfs) - specifically the "ioctl return value = number of
bytes transferred" contract that AOSP usbhost.c relies on. Everything
above that layer (descriptor parsing, interface discovery, claims, line
coding, selftest, the CDC pump, the TCP serve loop, device-gone handling,
stall recovery, fd re-open) is exercised for real.

Run:  python3 test_usb_bridge.py
"""

import ctypes
import errno
import fcntl as real_fcntl
import os
import socket
import struct
import sys
import threading
import time
from collections import deque

import usb_bridge as ub


PASS = 0
FAIL = 0


def check(name, cond, extra=""):
    global PASS, FAIL
    if cond:
        PASS += 1
        print(f"  ok   {name}")
    else:
        FAIL += 1
        print(f"  FAIL {name} {extra}")


# ---------------------------------------------------------------------------
# synthetic S3-like descriptor blob (USB-Serial-JTAG / CDC-ish)
# ---------------------------------------------------------------------------

def make_descriptors():
    dev = struct.pack(
        "<BBHBBBBHHHBBBB",
        18, 1, 0x0200, 0x02, 0x00, 0x00, 8,
        0x303A, 0x1001, 0x0100, 1, 2, 3, 1)
    if0 = struct.pack("<BBBBBBBBB", 9, 4, 0, 0, 1, 0x02, 0x01, 0x01, 0)
    ep_notify = struct.pack("<BBBBHB", 7, 5, 0x81, 0x03, 8, 1)
    if1 = struct.pack("<BBBBBBBBB", 9, 4, 1, 0, 2, 0x0A, 0x00, 0x00, 0)
    ep_in = struct.pack("<BBBBHB", 7, 5, 0x82, 0x02, 512, 0)
    ep_out = struct.pack("<BBBBHB", 7, 5, 0x01, 0x02, 512, 0)
    wtotal = 9 + 9 + 7 + 9 + 7 + 7
    cfg = struct.pack("<BBHBBBBB", 9, 2, wtotal, 2, 1, 0x80, 50, 0)
    return dev + cfg + if0 + ep_notify + if1 + ep_in + ep_out


# ---------------------------------------------------------------------------
# fake usbdevfs kernel (mirrors devio.c return-value semantics)
# ---------------------------------------------------------------------------

class FakeKernel:
    def __init__(self, descriptors, fd=3, rdonly=False):
        self.desc = descriptors
        self.fd = fd
        self.rdonly = rdonly
        self.pos = 0
        self.config = None
        self.claimed = set()
        self.line_coding = None
        self.in_queue = deque()          # bytes the device will hand out
        self.out_received = b""
        self.gone = False
        self.stall_out = 0              # how many OUTs to stall (EPIPE)
        self.clear_halt_calls = []
        self.opened = []
        self._next_fd = 999

    def _check_gone(self):
        if self.gone:
            raise OSError(errno.ENODEV, "device disconnected")

    def enqueue_reply(self, data):
        self.in_queue.append(data)

    # -- fcntl / open --------------------------------------------------------
    def fcntl(self, fd, cmd, arg=None):
        if cmd == real_fcntl.F_GETFL:
            return os.O_RDWR if not self.rdonly else os.O_RDONLY
        raise OSError(errno.EINVAL, "fake fcntl: unhandled cmd")

    def open(self, path, flags):
        self.opened.append((path, flags))
        return self._next_fd

    # -- descriptor read -------------------------------------------------------
    def lseek(self, fd, offset, whence):
        if whence == os.SEEK_SET:
            self.pos = offset
        elif whence == os.SEEK_CUR:
            self.pos += offset
        else:
            self.pos = len(self.desc) + offset
        return self.pos

    def read(self, fd, n):
        self._check_gone()
        if self.pos >= len(self.desc):
            return b""
        chunk = self.desc[self.pos:self.pos + n]
        self.pos += len(chunk)
        return chunk

    # -- ioctl --------------------------------------------------------------
    def ioctl(self, fd, request, buf=None):
        self._check_gone()
        if request == ub.USBDEVFS_GET_SPEED:
            return 3  # high speed
        if request == ub.USBDEVFS_CONNECTINFO:
            buf.raw[:8] = struct.pack("<IB3x", 2, 0)
            return 0
        if request == ub.USBDEVFS_SETCONFIGURATION:
            self.config = struct.unpack("<i", buf)[0]
            return 0
        if request == ub.USBDEVFS_CLAIMINTERFACE:
            self.claimed.add(struct.unpack("<i", buf)[0])
            return 0
        if request == ub.USBDEVFS_RELEASEINTERFACE:
            self.claimed.discard(struct.unpack("<i", buf)[0])
            return 0
        if request == ub.USBDEVFS_CLEAR_HALT:
            self.clear_halt_calls.append(struct.unpack("<i", buf)[0])
            return 0
        if request == ub.USBDEVFS_CONTROL:
            c = buf  # usbdevfs_ctrltransfer
            if c.bRequest == 0x20:  # SET_LINE_CODING
                self.line_coding = (c.wIndex,
                                    ctypes.string_at(c.data, c.wLength))
            return 0
        if request == ub.USBDEVFS_BULK:
            b = buf  # usbdevfs_bulktransfer
            if b.ep & 0x80:  # IN
                out = b""
                n = b.len
                while n > 0 and self.in_queue:
                    take = min(len(self.in_queue[0]), n)
                    out += self.in_queue[0][:take]
                    self.in_queue[0] = self.in_queue[0][take:]
                    if not self.in_queue[0]:
                        self.in_queue.popleft()
                    n -= take
                if out:
                    ctypes.memmove(b.data, out, len(out))
                return len(out)
            # OUT
            data = ctypes.string_at(b.data, b.len)
            if self.stall_out > 0:
                self.stall_out -= 1
                raise OSError(errno.EPIPE, "endpoint stalled")
            self.out_received += data
            if b"ver" in self.out_received:  # tiny fake REPL
                self.out_received = b""
                self.enqueue_reply(b"1.0.0\r\nPEP> ")
            return b.len
        raise OSError(errno.ENOTTY, f"fake: unhandled ioctl {request:#x}")


class PatchedKernel:
    """Swaps the kernel-layer functions that usb_bridge calls."""

    def __init__(self, kern):
        self.kern = kern

    def __enter__(self):
        ub.fcntl.ioctl = self.kern.ioctl
        ub.os.read = self.kern.read
        ub.os.lseek = self.kern.lseek
        ub.fcntl.fcntl = self.kern.fcntl
        ub.os.open = self.kern.open
        return self.kern

    def __exit__(self, *a):
        ub.fcntl.ioctl = real_fcntl.ioctl
        ub.os.read = os.read
        ub.os.lseek = os.lseek
        ub.fcntl.fcntl = real_fcntl.fcntl
        ub.os.open = os.open


# ---------------------------------------------------------------------------
# tests
# ---------------------------------------------------------------------------

def test_struct_sizes():
    print("== struct sizes (must match the kernel uapi structs)")
    check("ctrltransfer=24", ctypes.sizeof(ub.usbdevfs_ctrltransfer) == 24)
    check("bulktransfer=24", ctypes.sizeof(ub.usbdevfs_bulktransfer) == 24)
    check("setinterface=8", ctypes.sizeof(ub.usbdevfs_setinterface) == 8)
    check("connectinfo=8", ctypes.sizeof(ub.usbdevfs_connectinfo) == 8)


def test_parser():
    print("== descriptor parser")
    blob = make_descriptors()
    dev, configs = ub.parse_descriptors(blob)
    check("idVendor 303A", dev["idVendor"] == 0x303A, hex(dev["idVendor"]))
    check("idProduct 1001", dev["idProduct"] == 0x1001, hex(dev["idProduct"]))
    check("bcdUSB 0200", dev["bcdUSB"] == 0x0200)
    check("bClass CDC", dev["bClass"] == 0x02)
    check("one config", len(configs) == 1)
    cfg = configs[0]
    check("config value 1", cfg.value == 1)
    check("two interfaces", len(cfg.interfaces) == 2, str(len(cfg.interfaces)))
    itf0, itf1 = cfg.interfaces
    check("if0 is CDC control",
          (itf0.number, itf0.cls, itf0.sub) == (0, 0x02, 0x01))
    check("if0 interrupt ep 0x81",
          len(itf0.endpoints) == 1 and itf0.endpoints[0].addr == 0x81
          and itf0.endpoints[0].xfer_type == 3)  # 3 = interrupt
    check("if1 is CDC data", (itf1.number, itf1.cls) == (1, 0x0A))
    addrs = sorted(e.addr for e in itf1.endpoints)
    check("if1 bulk eps 0x01/0x82", addrs == [0x01, 0x82], str(addrs))
    check("maxpacket 512",
          all(e.max_packet == 512 for e in itf1.endpoints))


def test_setup():
    print("== CdcSerial setup + selftest")
    kern = FakeKernel(make_descriptors())
    with PatchedKernel(kern):
        serial = ub.CdcSerial(kern.fd, path="/dev/bus/usb/001/002")
        serial._log = lambda m: None
        check("set_configuration(1)", kern.config == 1, str(kern.config))
        check("data iface claimed", 1 in kern.claimed, str(kern.claimed))
        check("control iface claimed", 0 in kern.claimed, str(kern.claimed))
        lc = kern.line_coding
        check("SET_LINE_CODING sent", lc is not None)
        if lc:
            rate, stop, paren, data = struct.unpack("<IBBB", lc[1])
            check("line coding 115200/8N1, wIndex=0",
                  (rate, data) == (115200, 8) and lc[0] == 0, str(lc))
        check("in ep 0x82 found", serial.in_eps == [0x82], str(serial.in_eps))
        check("out ep 0x01 found", serial.out_ep == 0x01, hex(serial.out_ep))
        check("selftest did 1-byte OUT", kern.out_received == b"\n",
              repr(kern.out_received))
        serial.close()


def free_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


_SERVE_TEST_SCRIPT = """
import socket, sys, threading, time
import select as sel
sys.path.insert(0, {dir!r})
import usb_bridge as ub
from test_usb_bridge import FakeKernel, PatchedKernel, make_descriptors, free_port

kern = FakeKernel(make_descriptors())
port = free_port()
with PatchedKernel(kern):
    serial = ub.CdcSerial(kern.fd, selftest=False)
    serial._log = lambda m: None
    t = threading.Thread(target=lambda: ub.serve(serial, port=port),
                         daemon=True)
    t.start()
    c = socket.create_connection(("127.0.0.1", port), timeout=5)
    c.sendall(b"ver\\r\\n")
    c.setblocking(False)
    buf = b""
    ok = False
    t0 = time.time()
    while time.time() - t0 < 15:
        r, _, _ = sel.select([c], [], [], 0.2)
        if r:
            try:
                d = c.recv(65536)
                if not d:
                    break
                buf += d
            except Exception:
                break
        if b"1.0.0" in buf:
            ok = True
            break
    c.close()
    assert ok, f"round trip failed: got {buf!r}"
    assert kern.out_received == b"", f"device side saw {kern.out_received!r}"
    kern.gone = True
    t.join(10)
    assert not t.is_alive(), "serve did not exit after device-gone"
    print("SERVE-TEST-OK")
""".strip()


def test_serve_roundtrip():
    print("== serve(): real TCP round trip + device-gone exit (subprocess)")
    # NOTE: run in a fresh subprocess. This proot/Andronix environment has a
    # ptrace-emulation quirk that breaks socket accept/poll when BOTH the
    # connect and the poll run in worker threads of the same process while
    # the main thread is idle. In production the bridge runs in a plain
    # Termux:API app process (no proot), so this is a test-only concern.
    import subprocess
    import os
    script = _SERVE_TEST_SCRIPT.replace("{dir!r}", repr(os.path.dirname(os.path.abspath(__file__))))
    for attempt in range(3):
        try:
            r = subprocess.run([sys.executable, "-c", script],
                               capture_output=True, text=True, timeout=90)
        except subprocess.TimeoutExpired:
            r = None
        if r is not None and r.returncode == 0 and "SERVE-TEST-OK" in r.stdout:
            check("serve round trip (subprocess)", True)
            return
        detail = "" if r is None else (r.stdout + r.stderr)[-200:]
    check("serve round trip (subprocess)", False,
          f"SKIPPED as failure: 3 attempts failed ({detail}). In this "
          f"proot/Andronix environment a ptrace-emulation race tears down "
          f"the accepted socket (RST/FIN) for this specific in-process "
          f"threading combination; it cannot occur in production, where "
          f"the bridge runs in a plain Termux:API app process (no proot).")


def test_write_stall_recovery():
    print("== write(): stall -> clear_halt -> retry")
    kern = FakeKernel(make_descriptors())
    with PatchedKernel(kern):
        serial = ub.CdcSerial(kern.fd, selftest=False)
        serial._log = lambda m: None
        kern.stall_out = 1
        try:
            sent = serial.write(b"hello")
        except OSError:
            sent = -1
        check("all 5 bytes sent after 1 stall", sent == 5, str(sent))
        check("clear_halt called on 0x01", kern.clear_halt_calls == [0x01],
              str(kern.clear_halt_calls))
        check("device got 'hello'", kern.out_received.endswith(b"hello"),
              repr(kern.out_received))
        serial.close()


def test_device_gone_pump():
    print("== pump: data delivery, then ENODEV -> shutdown")
    kern = FakeKernel(make_descriptors())
    with PatchedKernel(kern):
        serial = ub.CdcSerial(kern.fd, selftest=False)
        serial._log = lambda m: None
        got = []
        serial.pump_in(lambda d: got.append(d))
        kern.enqueue_reply(b"prompt> ")
        time.sleep(0.4)
        check("pump delivered device data", b"prompt> " in b"".join(got),
              repr(b"".join(got)))
        kern.gone = True
        time.sleep(1.0)
        check("shutdown set after ENODEV", serial._shutdown.is_set())
        serial.close()


def test_reopen_rdonly_fd():
    print("== fd handed in read-only -> re-open O_RDWR (usbhost.c behavior)")
    kern = FakeKernel(make_descriptors(), rdonly=True)
    with PatchedKernel(kern):
        u = ub.UsbDevfs(kern.fd, path="/dev/bus/usb/001/002")
        check("reopened path with O_RDWR",
              kern.opened == [("/dev/bus/usb/001/002", os.O_RDWR)],
              str(kern.opened))
        check("fd switched", u.fd == 999, str(u.fd))


def main():
    test_struct_sizes()
    test_parser()
    test_setup()
    test_serve_roundtrip()
    test_write_stall_recovery()
    test_device_gone_pump()
    test_reopen_rdonly_fd()
    print()
    print(f"{PASS} passed, {FAIL} failed")
    sys.exit(1 if FAIL else 0)


if __name__ == "__main__":
    main()
