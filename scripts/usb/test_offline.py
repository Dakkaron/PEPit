#!/usr/bin/env python3
"""Offline tests for scripts/usb/{cdc_bridge,sd_filemanager} - no board needed.

  python3 test_offline.py            # runs both tests
"""
import os
import socket
import struct
import sys
import threading
import time
import zlib

HERE = os.path.join(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, HERE)


# ---------------------------------------------------------------------------
# Fake device: implements the T-HMI serialHandler protocol over a TCP port
# ---------------------------------------------------------------------------

class FakeDevice:
    def __init__(self, port=23999):
        self.port = port
        self.received = {}   # path -> bytes
        self._stop = False
        srv = socket.socket()
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind(("127.0.0.1", port))
        srv.listen(1)
        threading.Thread(target=self._loop, args=(srv,), daemon=True).start()

    def _loop(self, srv):
        while not self._stop:
            try:
                conn, _ = srv.accept()
            except OSError:
                return
            threading.Thread(target=self._client, args=(conn,), daemon=True).start()

    def _client(self, conn):
        """Stateful emulation of serialHandler.cpp for one connection."""
        state = "cmd"
        buf = b""
        # ulb context
        ulb_path, ulb_size, ulb_remaining, ulb_last, ulb_data = None, 0, 0, -1, None

        def send(b):
            conn.sendall(b)

        while True:
            if state == "cmd":
                # device matches "ver " on 4 buffered chars, no newline needed
                stripped = buf.lstrip(b" ")
                if stripped.startswith(b"ver ") and len(stripped) >= 4:
                    send(b"PEPit Version '9.9.9-test'\r\n")
                    buf = buf[len(buf) - len(stripped) + 4:]
                    continue
                # wait for a full line, then dispatch like handleSerial
                nl = buf.find(b"\n")
                if nl < 0:
                    pass
                else:
                    line = buf[:nl + 1]
                    rest = buf[nl + 1:]
                    cmd = line.lstrip(b" ").split(b" ")[0]
                    if cmd in (b"ul", b"ulb"):
                        # the client sends " ul <target>\n"; the device's
                        # readSerialLine() reads the rest of that line
                        path = line.lstrip(b" ")[len(cmd) + 1:-1].decode()
                        send(path.encode() + b"\n")
                        state, ulb_path, ulb_data = "ulb_size", path, bytearray()
                        buf = rest
                        continue
                    elif cmd == b"ver":
                        send(b"PEPit Version '9.9.9-test'\r\n")
                        buf = rest
                        continue
                    elif cmd == b"ls":
                        out = b"Listing directory: /\r\n"
                        for p in sorted(self.received):
                            out += f"  FILE: {p}  SIZE: {len(self.received[p])}\r\n".encode()
                        out += b"\r\n$ "
                        send(out)
                        buf = rest
                        continue
                    elif cmd == b"reset":
                        buf = rest
                        continue
                    else:
                        # unknown: drop the line
                        buf = rest
                        continue
            elif state == "ulb_size":
                nl = buf.find(b"\n")
                if nl < 0:
                    pass  # fall through to recv() - do NOT busy-spin
                else:
                    size_line = buf[:nl + 1]
                    buf = buf[nl + 1:]
                    ulb_size = int(size_line.strip())
                    ulb_remaining = ulb_size
                    ulb_last = -1
                    send(b"Upload blocks mode\r\n")
                    send(("Writing to file: '%s'\r\n" % ulb_path).encode())
                    send(("Number of bytes: '%d'\r\n" % ulb_size).encode())
                    send(b"Starting transmission\r\n")
                    state = "ulb_blocks"
            elif state == "ulb_blocks":
                if ulb_remaining <= 0:
                    # finished: final markers (device lines 186-200)
                    send(b"SUCCESSFUL_CRC_CHECK\r\n")
                    send(b"\r\nDone writing tmp file\r\n")
                    send(b"Deleting existing file\r\nRenamed tmp file to final file\r\n")
                    self.received[ulb_path] = bytes(ulb_data)
                    state = "cmd"
                    continue
                need = 8 + min(ulb_remaining, 4096)
                if len(buf) >= need:
                    block_id, crc = struct.unpack("<II", buf[:8])
                    chunk_len = min(ulb_remaining, 4096)
                    chunk = buf[8:8 + chunk_len]
                    buf = buf[8 + chunk_len:]
                    if block_id == ulb_last:
                        send(b"SUCCESSFUL_CRC_CHECK\r\n")  # dedupe path
                        continue
                    if crc == (zlib.crc32(chunk) & 0xFFFFFFFF):
                        send(b"SUCCESSFUL_CRC_CHECK\r\n")
                        ulb_data += chunk
                        ulb_last = block_id
                        ulb_remaining -= chunk_len
                    else:
                        send(b"REQUEST_RESEND_CRC_CHECK\r\n")
                    continue
                # else: fall through to recv() - do NOT busy-spin
            try:
                data = conn.recv(65536)
            except OSError:
                return
            if not data:
                return
            buf += data


# ---------------------------------------------------------------------------
# Fake "USB CDC" serial for the bridge serve() loop
# ---------------------------------------------------------------------------

class FakeSerial:
    """Duck-typed CdcSerial: queue-echo 'device' for serve() loop testing."""

    def __init__(self):
        import cdc_bridge  # noqa: F401  (same module under test)
        self._shutdown = threading.Event()
        self._pump_stop = threading.Event()
        self._out = []
        self.handle = object()

    def write(self, data):
        self._out.append(bytes(data))
        return len(data)

    def pump_in(self, on_data, poll_timeout=0.2):
        def loop():
            while not self._shutdown.is_set() and not self._pump_stop.is_set():
                if self._out:
                    on_data(self._out.pop(0))
                else:
                    time.sleep(0.01)

        threading.Thread(target=loop, daemon=True).start()

    def stop_pumps(self):
        self._pump_stop.set()

    def close(self):
        self._shutdown.set()
        self._pump_stop.set()


def test_filemanager_protocol():
    print("== test 1: sd_filemanager.py ulb protocol vs fake device")
    import subprocess
    dev = FakeDevice(23999)
    # make a ~1.7MB-ish test file (5 blocks incl. partial last)
    payload = os.urandom(10000)  # 2 full 4096 blocks + 1808 partial
    with open("/tmp/pepit_test_fw.bin", "wb") as f:
        f.write(payload)
    p = subprocess.run(
        [sys.executable, os.path.join(HERE, "sd_filemanager.py"),
         "--port", "23999", "--batch",
         "ul /tmp/pepit_test_fw.bin /firmware.bin", "ls /"],
        capture_output=True, text=True, timeout=120)
    out = p.stdout
    ok = (b"" if False else None)
    passed = True
    if "/firmware.bin" not in out or "Done uploading" not in out:
        passed = False
    # the fake device must have the exact bytes
    if dev.received.get("/firmware.bin") != payload:
        got = dev.received.get("/firmware.bin")
        print(f"   MISMATCH: device got {0 if got is None else len(got)} bytes, expected {len(payload)}")
        passed = False
    # the rename marker is consumed by read_until() on the client side, so
    # the real proof is the fake device's received bytes
    if "Finished writeFileBlocked()" not in out:
        print("   upload did not complete on the client side")
        passed = False
    print(out[-800:])
    print("== test 1:", "PASS" if passed else "FAIL")
    return passed


def test_bridge_tcp():
    print("== test 2: cdc_bridge serve() loop vs fake serial + raw client")
    import cdc_bridge
    fake = FakeSerial()
    port = 24001
    threading.Thread(target=cdc_bridge.serve, args=(fake, "127.0.0.1", port), daemon=True).start()
    time.sleep(0.5)

    def roundtrip(msg):
        c = socket.create_connection(("127.0.0.1", port), timeout=5)
        c.sendall(msg)
        got = b""
        end = time.time() + 5
        while len(got) < len(msg) and time.time() < end:
            c.settimeout(1)
            try:
                got += c.recv(65536)
            except socket.timeout:
                pass
        c.close()
        return got

    ok = roundtrip(b"hello-from-client") == b"hello-from-client"
    # after client 1 goes away the bridge must accept client 2
    time.sleep(0.8)
    ok = ok and roundtrip(b"again") == b"again"
    fake.close()
    print("== test 2:", "PASS" if ok else "FAIL")
    return ok


if __name__ == "__main__":
    r1 = test_filemanager_protocol()
    r2 = test_bridge_tcp()
    sys.exit(0 if (r1 and r2) else 1)
