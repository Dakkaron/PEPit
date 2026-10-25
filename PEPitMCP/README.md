# PEPitCompat MCP Server

An [MCP](https://modelcontextprotocol.io) server that lets an AI agent control a running
[PEPitCompat](../PEPitCompat/README.md) (Linux emulation) session of the PEPit physiotherapy
game console — and run it fully headless.

## What it does

| Tool | Purpose |
|---|---|
| `pepit_session_start` | Start a headless emulated session (`SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./build/pepit_compat`) |
| `pepit_session_status` | Report whether the session is running and the current system state |
| `pepit_screenshot` | Capture the current framebuffer and return it as MCP image content the agent can see |
| `pepit_touch_start` | Touch down at display coordinates (`x`: 0–320, `y`: 0–240) |
| `pepit_touch_move` | Move / drag the touch while pressed |
| `pepit_touch_stop` | Release the touch |
| `pepit_joystick_control` | Joystick axis (`adcX`, `adcY`; center 32768, extremes 0/65535) + button (`0` = pressed, `1` = released) |
| `pepit_set_pressure` | Emulated air-pressure sensor value on a percent-of-target scale (`0` = idle, ~32.5 = ~50 %, ~65 = ~100 %). The server converts to raw sensor units using the profile's target strength. A blow counts once it stays above the task's minimum pressure for the task's target duration. |
| `pepit_session_kill` | Kill the running session |

## Architecture

```
MCP server (Python, mcp SDK, stdio)
   │  writes control commands to /tmp/pepit_control
   ▼
pepit_compat (C++ SDL2 emulator)  ←  reads /tmp/pepit_control once per frame
   │  SHOT → writes /tmp/pepit_shot.raw (RGB888) + /tmp/pepit_shot.json
   ▼
MCP server reads frame → encodes PNG (Pillow) → returns image content
```

- Control channel: control file `/tmp/pepit_control` (commands: `TOUCH_DOWN|TOUCH_MOVE|TOUCH_UP`, `JOYSTICK <adcX> <adcY> <btn>`, `PRESSURE <rawUnits>`, `SHOT`, `QUIT`).
  - Concurrency protocol: the emulator claims the file with an atomic `rename()`, reads all lines, then removes it; the server appends new commands via a temp file + atomic `rename()`. Commands can therefore never be torn or silently lost.
  - `PRESSURE` takes the *raw* post-tare sensor value (the emulator divides by `65 × taskTargetStrength` to get percent, so a 100 % blow of a 30 target = 195000). `pepit_set_pressure` converts the agent-friendly percent scale to raw units automatically.
  - `pepit_touch_start` / `pepit_touch_stop` wait until the file is consumed (delivery acknowledged) before returning.
- Screenshot channel: `/tmp/pepit_shot.raw` (320×240 RGB888) + `/tmp/pepit_shot.json` (timestamp, system state). Both are published atomically (write to `*.tmp` + `rename()`), so readers never see a partial frame. The JSON is only updated on `SHOT`; a stale timestamp means "no screenshot since boot", not a dead app.
- Headless: the child is spawned with `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy`.

## Setup

1. Install Python dependencies: `pip install --break-system-packages mcp pillow`
2. Start the server: `python3 /root/repos/PEPit/PEPitMCP/server.py`
3. Connect an MCP client (e.g. OpenCode) via stdio — see the project-level `opencode.json`:

```json
{
  "mcp": {
    "servers": {
      "pepit_compat": {
        "type": "local",
        "command": ["python3", "/root/repos/PEPit/PEPitMCP/server.py"]
      }
    }
  }
}
```

4. In OpenCode: `opencode mcp list` → `✓ pepit_compat connected`; use `/mcps` in the TUI to inspect/connect.

## Usage in OpenCode

Once connected, the agent sees tools prefixed with the server name:

- `pepit_compat_pepit_session_start`
- `pepit_compat_pepit_screenshot`
- `pepit_compat_pepit_touch_start`
- `pepit_compat_pepit_joystick_control`
- `pepit_compat_pepit_set_pressure`
- `pepit_compat_pepit_session_kill`

## Closed-loop control pattern

The agent should use this pattern:

1. `pepit_screenshot` → observe current screen (image content).
2. Issue input commands (`pepit_touch_*`, `pepit_joystick_control`, `pepit_set_pressure`).
3. `pepit_screenshot` again → verify effect.

## Kill

`pepit_session_kill` writes `QUIT` to the control file → C++ shim sets `g_requestQuit` → graceful
shutdown of the `pepit_compat` child process.

## Files

- Server: `/root/repos/PEPit/PEPitMCP/server.py`
- C++ control/screenshot shim: `/root/repos/PEPit/PEPitCompat/shim/{touch,joystick,sensor,display,main}.cpp`
- OpenCode config: `/root/repos/PEPit/opencode.json`
