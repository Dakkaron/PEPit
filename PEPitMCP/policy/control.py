"""Control channel to a running pepit_compat session.

Talks to the C++ shim over two file channels (same protocol the MCP server
uses — see ../server.py and the PEPitCompat shim):

  /tmp/pepit_control   : we append lines, the shim atomically rename()s them
                         away once consumed; file absence = ACK
  /tmp/pepit_shot.raw  : 320*240*3 RGB888 frame, published by the shim on
  /tmp/pepit_shot.json : SHOT (timestamp + systemState + dimensions)

Commands:  TOUCH_DOWN x y | TOUCH_MOVE x y | TOUCH_UP | JOYSTICK ax ay btn
           PRESSURE raw | SHOT | QUIT
"""

import fcntl
import json
import os
import time

import numpy as np

W, H = 320, 240
CENTER = 32768
FULL = 65535


class ControlChannel:
    def __init__(self,
                 control_path: str = "/tmp/pepit_control",
                 raw_path: str = "/tmp/pepit_shot.raw",
                 meta_path: str = "/tmp/pepit_shot.json",
                 pressure_target: float = 30.0):
        self.control_path = control_path
        self.raw_path = raw_path
        self.meta_path = meta_path
        self.pressure_target = pressure_target

    # ------------------------------------------------------------------
    # Raw protocol
    # ------------------------------------------------------------------
    def write(self, cmd: str) -> None:
        """Queue a command line, atomically (existing content is preserved).

        Multi-writer safe: an exclusive flock around read-modify-rename so
        concurrent writers (runner, MCP tools, a sampler) can never clobber
        each other's pending lines, and a per-PID temp file so no writer can
        rename a temp that another writer removed. A line may in the worst
        case be delivered twice (the shim renames the file aside between our
        read and our rename) — harmless: every command in this protocol is
        level/state-based, hence idempotent.
        """
        lock_path = self.control_path + ".lock"
        with open(lock_path, "w") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            try:
                existing = ""
                try:
                    with open(self.control_path, "r") as f:
                        existing = f.read()
                except OSError:
                    pass
                tmp = self.control_path + f".tmp{os.getpid()}"
                with open(tmp, "w") as f:
                    f.write(existing + cmd + "\n")
                os.rename(tmp, self.control_path)
            finally:
                fcntl.flock(lock, fcntl.LOCK_UN)

    def wait_consumed(self, timeout: float = 5.0) -> bool:
        """Wait until the shim has consumed the control file (ACK)."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if not os.path.exists(self.control_path):
                return True
            time.sleep(0.02)
        return not os.path.exists(self.control_path)

    def write_acked(self, cmd: str, timeout: float = 5.0) -> bool:
        self.write(cmd)
        return self.wait_consumed(timeout)

    def quit(self) -> None:
        self.write("QUIT")
        self.wait_consumed(5.0)

    # ------------------------------------------------------------------
    # Pressure
    # ------------------------------------------------------------------
    def pressure_percent(self, pct: float) -> None:
        """Inject pressure as percent of the current task target (0..100+).

        Firmware derivation (hardware/pressuresensor.cpp readPressure):
            sensorValue = readChannel / (PRESSURE_SENSOR_DIVISOR * targetPressure)
        with DIVISOR=65, and sensorValue is used directly as the *percent*
        reading (blow success checks `abs((int)pressure - 100)`; blow start
        at pressure > 100*minStrength/targetStrength). So 100% of the target
        requires readChannel = 100 * 65 * target (e.g. target 30 -> 195000,
        which is what the MCP pepit_set_pressure tool sends).
        => raw = pct * 65 * target
        """
        raw = pct * 65.0 * self.pressure_target
        self.write(f"PRESSURE {raw:.0f}")

    def blow(self, pct: float = 100.0) -> None:
        self.pressure_percent(pct)

    def stop_blow(self) -> None:
        self.pressure_percent(0)

    # ------------------------------------------------------------------
    # Joystick (ADC space; firmware cross-wires: x = (adc_y-32768)/32768)
    # ------------------------------------------------------------------
    def joystick(self, x_adc: int = CENTER, y_adc: int = CENTER,
                 button_pressed: bool = False) -> None:
        btn = 0 if button_pressed else 1
        self.write(f"JOYSTICK {x_adc} {y_adc} {btn}")

    def joystick_x(self, x: float, button_pressed: bool = False) -> None:
        """Set the firmware-level joystick X (-1..1), Y centered.

        Firmware cross-wires the axes: joystickX = (adc_y - 32768)/32768.
        """
        adc_y = min(FULL, max(0, int(CENTER + max(-1.0, min(1.0, x)) * CENTER)))
        self.joystick(CENTER, adc_y, button_pressed)

    def joystick_y(self, y: float, button_pressed: bool = False) -> None:
        """Set the firmware-level joystick Y (-1..1), X centered."""
        # firmware: joystickY = -(adc_x - 32768)/32768
        adc_x = min(FULL, max(0, int(CENTER - max(-1.0, min(1.0, y)) * CENTER)))
        self.joystick(adc_x, CENTER, button_pressed)

    def joystick_xy(self, x: float, y: float, button_pressed: bool = False) -> None:
        """Set BOTH firmware-level axes (-1..1) in a single write.

        joystick_x()/joystick_y() each center the other axis, so issuing
        them as two commands cancels the first — games reading both axes
        need this combined form.
        """
        cx = max(-1.0, min(1.0, x))
        cy = max(-1.0, min(1.0, y))
        # firmware: joystickX = (adc_y - CENTER)/CENTER,
        #          joystickY = -(adc_x - CENTER)/CENTER
        adc_x = min(FULL, max(0, int(CENTER - cy * CENTER)))
        adc_y = min(FULL, max(0, int(CENTER + cx * CENTER)))
        self.joystick(adc_x, adc_y, button_pressed)

    def joystick_reset(self) -> None:
        self.joystick()

    # ------------------------------------------------------------------
    # Touch
    # ------------------------------------------------------------------
    def touch_down(self, x: int, y: int) -> bool:
        return self.write_acked(f"TOUCH_DOWN {x} {y}")

    def touch_move(self, x: int, y: int) -> None:
        self.write(f"TOUCH_MOVE {x} {y}")

    def touch_up(self) -> bool:
        return self.write_acked("TOUCH_UP")

    def tap(self, x: int, y: int, hold: float = 0.4) -> bool:
        ok = self.touch_down(x, y)
        time.sleep(hold)
        self.touch_up()
        return ok

    # ------------------------------------------------------------------
    # Screenshot
    # ------------------------------------------------------------------
    def shot(self, timeout: float = 10.0):
        """Capture one frame -> (numpy RGB array, meta dict) or (None, {})."""
        self.write("SHOT")
        deadline = time.monotonic() + timeout
        got = False
        while time.monotonic() < deadline:
            try:
                if os.path.getsize(self.raw_path) == W * H * 3:
                    got = True
                    break
            except OSError:
                pass
            time.sleep(0.02)
        if not got:
            return None, {}
        try:
            with open(self.raw_path, "rb") as f:
                raw = f.read()
        except OSError:
            return None, {}
        if len(raw) != W * H * 3:
            time.sleep(0.1)
            try:
                with open(self.raw_path, "rb") as f:
                    raw = f.read()
            except OSError:
                return None, {}
            if len(raw) != W * H * 3:
                return None, {}
        img = np.frombuffer(raw, dtype=np.uint8).reshape(H, W, 3)
        meta = {}
        try:
            with open(self.meta_path, "r") as f:
                meta = json.load(f)
        except Exception:
            pass
        return img, meta

    def system_state(self, timeout: float = 10.0) -> int:
        """systemState from the latest screenshot metadata (-1 = unknown)."""
        _, meta = self.shot(timeout=timeout)
        return int(meta.get("systemState", -1))
