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
  - pepit_policy_run      : delegate gameplay to the fast policy loop
                            (pepit_policy_run / pepit_policy_status /
                             pepit_policy_stop, see policy/)

Control channel: polled control file /tmp/pepit_control (written here, read by the C++ shim).
Screenshot channel: /tmp/pepit_shot.raw (RGB888) + /tmp/pepit_shot.json written by the C++ shim.

Transport: MCP stdio (used by OpenCode via `mcp.servers` local server entry).
"""

import io
import json
import os
import re
import subprocess
import sys
import threading
import time
from typing import Any

from mcp.server import MCPServer
from mcp.server.mcpserver.utilities.types import Image
from mcp.types import TextContent

from PIL import Image as PILImage

# Make the policy package importable no matter how this file is launched
# (it runs as a plain script, not as part of the PEPitMCP package).
_REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
if _REPO_ROOT not in sys.path:
    sys.path.insert(0, _REPO_ROOT)
from PEPitMCP.policy.runner import PolicyRunner, ini_name  # noqa: E402

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
# what the running session was launched with (menu fast path), or None:
_auto_profile: str | None = None
_auto_game: str | None = None


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
    # Drop any previous frame first, otherwise the wait below would accept a
    # stale file from an earlier capture as the new one.
    for p in (SHOT_RAW_PATH, SHOT_JSON_PATH):
        try:
            os.unlink(p)
        except OSError:
            pass
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


@server.tool(name="pepit_session_start", description="Start a headless emulated PEPit session (pepit_compat via SDL dummy driver). "
              "Optional profile/game drop straight into a game: pass the profile name (profiles.ini `name`) or 0-based "
              "slot index, and the game name (gameconfig.ini `name`) or 0-based slot index. Without them the session "
              "stops at the profile-selection menu for manual navigation.")
def pepit_session_start(data_dir: str = "/tmp/opencode/pepit_data",
                        profile: str | None = None,
                        game: str | None = None) -> str:
    global _child, _data_dir, _auto_profile, _auto_game
    if _child_running():
        return "Session already running"
    _data_dir = data_dir
    _auto_profile, _auto_game = profile, game
    env = os.environ.copy()
    env["SDL_VIDEODRIVER"] = "dummy"
    env["SDL_AUDIODRIVER"] = "dummy"
    cmd = [PEPIT_BINARY, "--data-dir", data_dir, "--scale", "1"]
    if profile is not None:
        cmd += ["--profile", profile]
    if game is not None:
        cmd += ["--game", game]
    _child = subprocess.Popen(
        cmd,
        stdin=subprocess.DEVNULL,
        stdout=open(PEPIT_LOG, "ab"),
        stderr=subprocess.STDOUT,
        env=env,
        start_new_session=True,
    )
    note = ""
    if profile is not None or game is not None:
        note = f" (auto-nav: profile={profile!r} game={game!r})"
    return f"Session started with pid {_child.pid}{note}"


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
    global _child, _auto_profile, _auto_game
    if _child_running():
        _write_control("QUIT")
        try:
            _child.wait(timeout=15)
        except subprocess.TimeoutExpired:
            _child.kill()
            _child.wait()
    _child = None
    _auto_profile = _auto_game = None
    return "Session killed"


# ---------------------------------------------------------------------------
# Policy runner ("System 1") — delegate gameplay to a fast policy loop
# ---------------------------------------------------------------------------
_policy_runs: dict[str, PolicyRunner] = {}


@server.tool(name="pepit_policy_run",
             description="Delegate gameplay to the policy runner: it navigates "
                         "(profile -> game -> rotate screens) and plays the given "
                         "lua game (miner/plane/farm) until the duration expires or "
                         "the game finishes. source: 'rulebot' (deterministic) or "
                         "'llm'/'llm-text'/'llm-image' (LM Studio decision model). "
                         "Attaches to the running session, or starts one if none.")
def pepit_policy_run(game: str,
                     source: str = "rulebot",
                     duration_seconds: float = 300.0,
                     data_dir: str = "/tmp/opencode/pepit_data_lua",
                     profile_index: int = 0,
                     model: str = "cloudflare_clef-flash",
                     llm_base: str = "http://192.168.0.68:1234") -> str:
    global _data_dir, _auto_profile, _auto_game
    # Menu fast path: the session (running or freshly started) is launched
    # with --profile/--game so the firmware picks them itself. Profile by
    # index (unambiguous), game by gameconfig.ini display name (the shim
    # matches on that); if the name can't be read the stage falls back to
    # the runner's tap navigation.
    game_name = ini_name(os.path.join(data_dir, "games", game, "gameconfig.ini"))
    profile_spec = str(profile_index)
    if _child_running():
        own_session = False
        _data_dir = _data_dir or data_dir
        # A running binary can't gain --profile/--game after launch, so
        # respect how it was actually started (_auto_* as set by
        # pepit_session_start; possibly None -> classic tap navigation).
    else:
        # start our own session (with the policy data dir), like pepit_session_start
        _data_dir = data_dir
        _auto_profile, _auto_game = profile_spec, game_name
        env = os.environ.copy()
        env["SDL_VIDEODRIVER"] = "dummy"
        env["SDL_AUDIODRIVER"] = "dummy"
        cmd = [PEPIT_BINARY, "--data-dir", data_dir, "--scale", "1"]
        if _auto_profile is not None:
            cmd += ["--profile", _auto_profile]
        if _auto_game is not None:
            cmd += ["--game", _auto_game]
        _child = subprocess.Popen(
            cmd,
            stdin=subprocess.DEVNULL,
            stdout=open(PEPIT_LOG, "ab"),
            stderr=subprocess.STDOUT,
            env=env,
            start_new_session=True,
        )
        own_session = False  # the MCP server owns the child, not the runner

    run = PolicyRunner(
        game=game, source=source, datadir=_data_dir,
        profile_index=profile_index, duration=duration_seconds,
        log_path=PEPIT_LOG, llm_model=model, llm_base=llm_base,
        run_id=time.strftime("run_%H%M%S") + f"_{game}", own_session=own_session,
        auto_profile=_auto_profile, auto_game=_auto_game,
        autonav=(_auto_profile is not None or _auto_game is not None))
    _policy_runs[run.run_id] = run
    t = threading.Thread(target=run.run, name=f"policy-{run.run_id}", daemon=True)
    t.start()
    return (f"Policy run {run.run_id} started ({game}, {source}, "
            f"{duration_seconds}s) — status: {run.status_path}")


@server.tool(name="pepit_policy_status",
             description="Status of a policy run: phase (boot/profile/gamesel/"
                         "rotate/play/timeout/win), decisions made, last extracted "
                         "state, last actions, and ground-truth blow stats from the "
                         "session log.")
def pepit_policy_status(run_id: str = "") -> str:
    if not run_id:
        if len(_policy_runs) == 1:
            run_id = next(iter(_policy_runs))
        elif _policy_runs:
            run_id = max(_policy_runs)  # most recent by name
        else:
            return "No policy runs in this server session"
    run = _policy_runs.get(run_id)
    if run is None:
        return f"No run {run_id} in this server session"
    try:
        with open(run.status_path) as f:
            return f.read()
    except OSError as e:
        return f"status file unreadable: {e}"


@server.tool(name="pepit_policy_stop", description="Stop the policy run and release all inputs.")
def pepit_policy_stop(run_id: str = "") -> str:
    if not run_id:
        if len(_policy_runs) == 1:
            run_id = next(iter(_policy_runs))
        else:
            return "Ambiguous — pass run_id (or there are no runs)"
    run = _policy_runs.get(run_id)
    if run is None:
        return f"No run {run_id} in this server session"
    run.request_stop()
    return f"Stop requested for {run_id}"


def main() -> None:
    server.run(transport="stdio")


if __name__ == "__main__":
    main()
