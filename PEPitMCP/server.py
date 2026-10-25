#!/usr/bin/env python3
"""PEPitCompat MCP Server — let an AI agent control a running emulated PEPit session.

Tools:
  - pepit_session_start   : start the headless peppit_compat child process
  - pepit_session_status  : current session state (running / system state)
  - pepit_screenshot      : capture framebuffer -> image content for the agent
  - pepit_touch_start     : touch down at (x, y) display coordinates (0-320 / 0-240)
  - pepit_touch_move      : touch move / drag while down
  - pepit_touch_stop      : touch up
  - pepit_joystick_control: joystick axis (adcX, adcY) + button
  - pepit_set_pressure    : set emulated air-pressure sensor value
  - pepit_session_kill    : kill the running session

Control channel: polled control file /tmp/pepit_control (written here, read by the C++ shim).
Screenshot channel: /tmp/pepit_shot.raw (RGB888) + /tmp/pepit_shot.json written by the C++ shim.

Transport: MCP stdio (used by OpenCode via `mcp.servers` local server entry).
"""

import io
import json
import os
import re
import subprocess
import time
from typing import Any

from mcp.server import MCPServer
from mcp.server.mcpserver.utilities.types import Image
from mcp.types import TextContent

from PIL import Image as PILImage

# ---------------------------------------------------------------------------
# Constants / paths
# ---------------------------------------------------------------------------
CONTROL_PATH = "/tmp/pepit_control"
SHOT_RAW_PATH = "/tmp/pepit_shot.raw"
SHOT_JSON_PATH = "/tmp/pepit_shot.json"
PEPIT_BINARY = "/root/repos/PEPit/PEPitCompat/build/pepit_compat"
PEPIT_LOG = "/tmp/opencode/pepit_mcp_pepit_compat.log"

# ---------------------------------------------------------------------------
# Session state
# ---------------------------------------------------------------------------
_child = None
_data_dir = "/tmp/opencode/pepit_data"  # last value passed to pepit_session_start


def _child_running() -> bool:
    return _child is not None and _child.poll() is None


def _task_target_strength() -> float:
    """Target pressure of the first task of the first profile, from profiles.ini.

    Used to convert the agent-friendly 0-65 percent scale to the raw sensor
    units the compat shim injects. Falls back to the factory profile value (30).
    """
    try:
        with open(os.path.join(_data_dir, "profiles.ini"), "r") as f:
            text = f.read()
        m = re.search(r"task_0_targetStrength\s*=\s*([0-9.]+)", text)
        if m:
            return float(m.group(1))
        m = re.search(r"task_\d+_targetStrength\s*=\s*([0-9.]+)", text)
        if m:
            return float(m.group(1))
    except OSError:
        pass
    return 30.0


def _write_control(cmd: str) -> None:
    """Queue a command for the C++ shim, atomically.

    The shim consumes the control file via an atomic rename() and reads all
    lines, so we append: existing content (if any) + the new line is built
    in a temp file and rename()d into place. This way a reader and writer
    running concurrently can never tear or erase each other's data (a naive
    truncate-then-write interleaved with the reader could silently lose the
    command — the source of the old "tap flake").
    """
    existing = ""
    try:
        with open(CONTROL_PATH, "r") as f:
            existing = f.read()
    except OSError:
        pass  # File absent — nothing queued yet.
    tmp = CONTROL_PATH + ".tmp"
    with open(tmp, "w") as f:
        f.write(existing + cmd + "\n")
    os.rename(tmp, CONTROL_PATH)  # Atomic: the file is always complete.


def _wait_consumed(timeout: float = 5.0) -> bool:
    """Wait until the shim has consumed the control file (renamed away).

    Returns True once the file is gone (delivery confirmed), False on timeout.
    """
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if not os.path.exists(CONTROL_PATH):
            return True
        time.sleep(0.05)
    return not os.path.exists(CONTROL_PATH)


def _write_control_acked(cmd: str, timeout: float = 5.0) -> bool:
    _write_control(cmd)
    return _wait_consumed(timeout)


def _read_shot_meta() -> dict:
    try:
        with open(SHOT_JSON_PATH, "r") as f:
            return json.load(f)
    except Exception:
        return {}


def _get_system_state() -> int:
    return int(_read_shot_meta().get("systemState", -1))


def _capture_image() -> Image:
    """Write SHOT to the control file, wait for the C++ shim to capture, return PNG image."""
    _write_control("SHOT")
    # Wait for the shim to capture the frame (polled once per frame)
    deadline = time.time() + 10.0
    captured = False
    while time.time() < deadline:
        try:
            size = os.path.getsize(SHOT_RAW_PATH)
            if size == 320 * 240 * 3:
                captured = True
                break
        except OSError:
            pass
        time.sleep(0.05)
    if not captured:
        raise RuntimeError("Screenshot capture timed out")

    with open(SHOT_RAW_PATH, "rb") as f:
        raw = f.read()
    if len(raw) != 320 * 240 * 3:
        # The shim publishes the frame via a single write + atomic rename, so a
        # short read should be impossible; retry once as defense-in-depth.
        time.sleep(0.1)
        with open(SHOT_RAW_PATH, "rb") as f:
            raw = f.read()
    img = PILImage.frombytes("RGB", (320, 240), raw)
    buf = io.BytesIO()
    img.save(buf, format="PNG")
    return Image(data=buf.getvalue(), format="png")


# ---------------------------------------------------------------------------
# MCP server
# ---------------------------------------------------------------------------
server = MCPServer(name="pepit_compat", version="0.1.0")


@server.tool(name="pepit_session_start", description="Start a headless emulated PEPit session (pepit_compat via SDL dummy driver).")
def pepit_session_start(data_dir: str = "/tmp/opencode/pepit_data") -> str:
    global _child, _data_dir
    if _child_running():
        return "Session already running"
    _data_dir = data_dir
    env = os.environ.copy()
    env["SDL_VIDEODRIVER"] = "dummy"
    env["SDL_AUDIODRIVER"] = "dummy"
    _child = subprocess.Popen(
        [PEPIT_BINARY, "--data-dir", data_dir, "--scale", "1"],
        stdin=subprocess.DEVNULL,
        stdout=open(PEPIT_LOG, "ab"),
        stderr=subprocess.STDOUT,
        env=env,
        start_new_session=True,
    )
    return f"Session started with pid {_child.pid}"


@server.tool(name="pepit_session_status", description="Return the current status of the emulated PEPit session.")
def pepit_session_status() -> str:
    running = _child_running()
    state = _get_system_state()
    return json.dumps({"running": running, "systemState": state})


@server.tool(name="pepit_screenshot", description="Capture a screenshot of the emulated PEPit screen and return it as image content for the agent.")
def pepit_screenshot() -> Any:
    img = _capture_image()
    meta = _read_shot_meta()
    text = f"Screenshot captured: systemState={meta.get('systemState')} width={meta.get('width')} height={meta.get('height')}"
    return [img, TextContent(type="text", text=text)]


@server.tool(name="pepit_touch_start", description="Touch down on the emulated screen at display coordinates (x: 0-320, y: 0-240). Waits until the down is confirmed received by the sim before returning.")
def pepit_touch_start(x: int, y: int) -> str:
    ok = _write_control_acked(f"TOUCH_DOWN {x} {y}")
    return f"Touch down at ({x}, {y})" + ("" if ok else " (WARNING: not confirmed received)")


@server.tool(name="pepit_touch_move", description="Move the touch (drag) while it is pressed, at display coordinates (x: 0-320, y: 0-240).")
def pepit_touch_move(x: int, y: int) -> str:
    _write_control(f"TOUCH_MOVE {x} {y}")
    return f"Touch move to ({x}, {y})"


@server.tool(name="pepit_touch_stop", description="Release the touch on the emulated screen. Waits until confirmed received.")
def pepit_touch_stop() -> str:
    ok = _write_control_acked("TOUCH_UP")
    return "Touch up" + ("" if ok else " (WARNING: not confirmed received)")


@server.tool(name="pepit_joystick_control", description="Control the emulated joystick: axis values in ADC space (center 32768, extremes 0/65535) and button (0=pressed, 1=released).")
def pepit_joystick_control(x: int, y: int, button_pressed: bool) -> str:
    btn = 0 if button_pressed else 1
    _write_control(f"JOYSTICK {x} {y} {btn}")
    return f"Joystick set to adc=({x}, {y}) button_pressed={button_pressed}"


@server.tool(name="pepit_set_pressure", description="Set the emulated air-pressure sensor value. 0 = idle, ~32.5 = ~50% of target, ~65 = ~100% of target. A blow counts once its duration exceeds the task's target duration while above the minimum pressure.")
def pepit_set_pressure(value: float) -> str:
    # Convert the percent-of-target scale (65 = 100%) to raw readChannel
    # units: percent = raw / (65 * target)  →  raw = value * 100 * target.
    target = _task_target_strength()
    raw = value * 100.0 * target
    _write_control(f"PRESSURE {raw:.0f}")
    return f"Pressure set to {value} (~{100.0 * value / 65.0:.0f}% of target {target:g}, injected raw {raw:.0f})"


@server.tool(name="pepit_session_kill", description="Kill the running emulated PEPit session.")
def pepit_session_kill() -> str:
    global _child
    if _child_running():
        _write_control("QUIT")
        try:
            _child.wait(timeout=15)
        except subprocess.TimeoutExpired:
            _child.kill()
            _child.wait()
    return "Session killed"


def main() -> None:
    server.run(transport="stdio")


if __name__ == "__main__":
    main()
