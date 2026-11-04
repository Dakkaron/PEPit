#!/usr/bin/env python3
"""
usbdiag.py - stage-by-stage bisector for the cffi/libusb bridge route.

Run this in the Termux shell. It walks the exact initialization path that
cdc_bridge.py uses (and nothing else), printing one line per stage with
flush=True. If the interpreter dies (segfault, abort, ...), the LAST line
printed tells you exactly which stage to blame:

  01  interpreter / OS info
  02  import _ctypes                       (Termux python's native ctypes)
  03  import ctypes + sizeof sanity check
  04  from cffi import FFI                 (the reinstalled cffi 2.1.1)
  05  FFI()                                (first cffi internal codegen)
  06  ffi.cdef(minimal)                    (cdef machinery, tiny decl)
  07  ffi.dlopen(None) + getpid()          (first real call via dlopen)
  08  import cdc_bridge                    (module import: its cdef string etc.)
  09  FFI().cdef(LIBUSB_CDEF)              (the FULL libusb ABI, verbatim)
  10  ffi.dlopen(libusb-1.0.so.*)          (each soname, first success wins)
  11  libusb_get_version()
  12  set_option(NULL, ...) x2 + ffi.new + libusb_init(ctx)
  13  libusb_wrap_sys_device + descriptor reads   (ONLY if a device fd
      was passed / TERMUX_USB_FD is set - i.e. when run via termux-usb)
  14  ALL OK

Exit codes: 0 = all stages passed. 100+N = stage N raised a Python
exception (the exception is printed). If the process DIES (segfault etc.)
the shell will report a signal/139 and the last printed line is the answer.

Usage (plain Termux shell, NOT proot):

  python3 usbdiag.py                          # stages 01-12 + verdict
  termux-usb -e "python3 usbdiag.py" /dev/bus/usb/001/00X
                                              # stages 01-14, incl. the
                                              # real device fd
"""

import os
import sys

STAGE = [0]


def stage(n, msg):
    STAGE[0] = n
    print(f"stage {n:02d} ok: {msg}", flush=True)


def fail(n, what):
    print(f"stage {n:02d} FAIL: {what!r}", flush=True)
    print("== stop here: report this line ==", flush=True)
    sys.exit(100 + n)


def main():
    # 01 - environment -----------------------------------------------------
    try:
        import platform
        k = f"{platform.machine()} {platform.platform()}"
    except Exception:
        k = "?"
    stage(1, f"python {sys.version.split()[0]} impl={sys.implementation.name} "
             f"machine={k} pid={os.getpid()}")

    # 02 - _ctypes ----------------------------------------------------------
    try:
        import _ctypes
        stage(2, f"import _ctypes ({getattr(_ctypes, '__file__', '?')})")
    except BaseException as e:
        fail(2, f"import _ctypes: {e!r}")

    # 03 - ctypes ------------------------------------------------------------
    try:
        import ctypes
        assert ctypes.sizeof(ctypes.c_int) == 4
        assert ctypes.sizeof(ctypes.c_uint16) == 2
        stage(3, "import ctypes, struct sizes sane")
    except BaseException as e:
        fail(3, f"ctypes: {e!r}")

    # 04 - cffi import -------------------------------------------------------
    try:
        import cffi
        from cffi import FFI
        stage(4, f"import cffi (version {getattr(cffi, '__version__', '?')}, "
                 f"{getattr(cffi, '__file__', '?')})")
    except BaseException as e:
        fail(4, f"import cffi: {e!r}")

    # 05 - FFI() -------------------------------------------------------------
    try:
        ffi = FFI()
        stage(5, "FFI()")
    except BaseException as e:
        fail(5, f"FFI(): {e!r}")

    # 06 - minimal cdef --------------------------------------------------------
    try:
        ffi.cdef("int getpid(void);")
        stage(6, 'cdef("int getpid(void);")')
    except BaseException as e:
        fail(6, f"minimal cdef: {e!r}")

    # 07 - dlopen(None) + first call ------------------------------------------
    try:
        lib0 = ffi.dlopen(None)
        pid = lib0.getpid()
        if pid != os.getpid():
            fail(7, f"getpid()={pid} != os.getpid()={os.getpid()}")
        stage(7, f"dlopen(None).getpid() = {pid}")
    except BaseException as e:
        fail(7, f"dlopen(None)/getpid: {e!r}")

    # 08 - import cdc_bridge (module import is a step of the real route) -----
    try:
        import cdc_bridge
        stage(8, f"import cdc_bridge ({len(cdc_bridge.LIBUSB_CDEF)} bytes of cdef, "
                 f"{len(cdc_bridge.LIBUSB_SONAMES)} sonames)")
    except BaseException as e:
        fail(8, f"import cdc_bridge: {e!r}")

    # 09 - the FULL libusb cdef (fresh FFI, exactly like load_libusb()) -------
    try:
        ffi2 = FFI()
        ffi2.cdef(cdc_bridge.LIBUSB_CDEF)
        stage(9, "FFI().cdef(LIBUSB_CDEF)")
    except BaseException as e:
        fail(9, f"full LIBUSB_CDEF cdef: {e!r}")

    # 10 - dlopen the libusb shared object -------------------------------------
    lib = None
    for name in cdc_bridge.LIBUSB_SONAMES:
        try:
            lib = ffi2.dlopen(name)
            stage(10, f"dlopen({name!r}) ok")
            break
        except OSError as e:
            print(f"stage 10   ... dlopen({name!r}) failed: {e}", flush=True)
    if lib is None:
        fail(10, f"no libusb soname loadable (tried "
                 f"{', '.join(cdc_bridge.LIBUSB_SONAMES)})")

    # 11 - libusb_get_version ---------------------------------------------------
    try:
        v = lib.libusb_get_version()
        ver = f"{v.major}.{v.minor}.{v.micro}"
        stage(11, f"libusb_get_version() = {ver}")
    except BaseException as e:
        fail(11, f"libusb_get_version: {e!r}")

    # 12 - set_option(NULL, ...) + libusb_init (the CdcSerial prelude) --------
    try:
        lib.libusb_set_option(ffi2.NULL,
                              cdc_bridge.LIBUSB_OPTION_LOG_LEVEL, 1)
        lib.libusb_set_option(ffi2.NULL,
                              cdc_bridge.LIBUSB_OPTION_NO_DEVICE_DISCOVERY, 1)
        ctx_p = ffi2.new("struct libusb_context **")
        r = lib.libusb_init(ctx_p)
        if r != 0:
            fail(12, f"libusb_init rc={r}")
        stage(12, "set_option x2 + ffi.new + libusb_init")
    except BaseException as e:
        fail(12, f"libusb_init sequence: {e!r}")

    # 13 - wrap the real device fd (only when one was handed to us) ------------
    fd = None
    if len(sys.argv) >= 2 and sys.argv[-1].isdigit():
        fd = int(sys.argv[-1])
    else:
        env_fd = os.environ.get("TERMUX_USB_FD")
        if env_fd and env_fd.isdigit():
            fd = int(env_fd)

    if fd is None:
        print("stage 13   ... skipped (no device fd: run via termux-usb, or "
              "it is not needed for usb_bridge.py)", flush=True)
        try:
            lib.libusb_exit(ctx_p[0])
        except BaseException:
            pass
        stage(14, "ALL OK (no device fd passed) - every cffi/libusb stage "
                  "the bridge route uses works; usb_bridge.py (pure stdlib) "
                  "needs none of these")
        return

    try:
        handle_p = ffi2.new("struct libusb_device_handle **")
        r = lib.libusb_wrap_sys_device(ctx_p[0], fd, handle_p)
        if r != 0:
            fail(13, f"libusb_wrap_sys_device rc={r}")
        dev = lib.libusb_get_device(handle_p[0])
        dd = ffi2.new("struct libusb_device_descriptor *")
        if lib.libusb_get_device_descriptor(dev, dd) == 0:
            stage(13, f"wrap_sys_device ok, device "
                      f"{dd.idVendor:04x}:{dd.idProduct:04x}")
        else:
            stage(13, "wrap_sys_device ok (no descriptor)")
        cfg_p = ffi2.new("struct libusb_config_descriptor **")
        if lib.libusb_get_active_config_descriptor(dev, cfg_p) == 0:
            stage(13, f"active config {cfg_p[0].bConfigurationValue} "
                      f"({cfg_p[0].bNumInterfaces} ifaces)")
            lib.libusb_free_config_descriptor(cfg_p[0])
        lib.libusb_close(handle_p[0])
        lib.libusb_exit(ctx_p[0])
    except BaseException as e:
        fail(13, f"wrap/descriptor sequence: {e!r}")

    stage(14, "ALL OK including real device fd - cffi/libusb route is "
              "functional end to end")


if __name__ == "__main__":
    main()
