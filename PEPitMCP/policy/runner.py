"""PolicyRunner — plays a PEPit lua game for us.

Phases:
  boot     : wait for the session to reach profile selection (state 1)
  profile  : pick the profile with the joystick (click = button press)
  gamesel  : pick the game (index discovered from the session boot log)
  rotate   : tap through between-task "rotate the device" screens
  play     : loop  shot -> extract -> decide (rulebot | llm) -> apply
  finish   : release inputs, write the summary

Decision sources:
  rulebot : deterministic game heuristics (games/*.py)
  llm     : LM Studio /v1/systemone; vision (image), text (extractor)
            or hybrid (both). Per-step latency ~1.5-2s on the Mac.

The runner is source-agnostic otherwise: it owns navigation, the blow
cycle context, log-derived ground truth (blow successes/fails, task
transitions) and the status file that the MCP tools read.

Standalone:
  python3 -m PEPitMCP.policy.runner --game miner --source rulebot \
      --data-dir /tmp/opencode/pepit_data_lua --duration 300
"""

import argparse
import json
import os
import re
import subprocess
import threading
import time

from .control import ControlChannel
from . import extract as E
from .games import get_game
from .llm import LLMClient

PEPIT_BINARY = "/root/repos/PEPit/PEPitCompat/build/pepit_compat"
STATUS_DIR = "/tmp/opencode"

# firmware serial markers (physioProtocolHandler.cpp / sdHandler.cpp).
# Note the multi-line reality: "Found game directory ...," then debug lines,
# then "CONFIRMED!" (or "does not support ...") on its own line.
RE_GAME_FOUND = re.compile(r"Found game directory /games/([A-Za-z0-9_\-]+)/")
RE_GAME_COUNT = re.compile(r"Number of games: (\d+)")
RE_ROTATE = re.compile(r"##### DISPLAY ROTATE ######")
RE_ROTATING_TO = re.compile(r"Rotating to: ([A-Za-z+ ]+?)\s*$")
RE_TARGET_DUR = re.compile(r"Target Duration: (\d+)")
ROTATE_TASK_NAMES = {
    "Short blows": "pepShort",
    "Long blows": "pepLong",
    "Equal blows": "pepEqual",
    "Inhalation": "inhalation",
    "Inhalation+PEP": "inhalationPep",
    "Trampoline": "trampoline",
}

STATE_BOOT, STATE_PROFILE, STATE_GAMESEL, STATE_RUNNING, STATE_ENDING, STATE_WIN = range(6)


def parse_profiles(path: str) -> list:
    """profiles.ini -> [{name, cycles, tasks: [{type, time_s, target, reps}]}]."""
    profiles = []
    if not os.path.isfile(path):
        return profiles
    text = open(path).read()
    section = None
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith(("#", ";")):
            continue
        m = re.match(r"\[(profile_\d+)\]", line)
        if m:
            section = {}
            profiles.append(section)
            continue
        if section is None or "=" not in line:
            continue
        key, _, val = line.partition("=")
        section[key.strip()] = val.strip()
    out = []
    for p in profiles:
        tasks = []
        i = 0
        while f"task_{i}_type" in p:
            tasks.append({
                "type": p[f"task_{i}_type"],
                "time_s": float(p.get(f"task_{i}_time", 5000)) / 1000.0,
                "target": float(p.get(f"task_{i}_targetStrength", 0) or 0),
                "min": float(p.get(f"task_{i}_minStrength", 0) or 0),
                "reps": int(p.get(f"task_{i}_repetitions", 0) or 0),
            })
            i += 1
        out.append({
            "name": p.get("name", "?"),
            "cycles": int(p.get("cycles", 1) or 1),
            "tasks": tasks,
        })
    return out


class LogFollower:
    """Tail the session log; expose marker-derived facts to the runner."""

    def __init__(self, path: str):
        self.path = path
        self.offset = 0
        self.games_confirmed = []   # ordered, in the order the firmware found them
        self._pending_game = None
        self.selected_game = None
        self.game_count = None        # "Number of games: N"
        self.blow_starts = 0
        self.blow_ends = 0
        self.blow_successes = 0
        self.rotate_pending = 0     # "##### DISPLAY ROTATE ######" seen
        self.next_task = None       # "Rotating to: X"
        self.target_duration_ms = None

    def read(self) -> str:
        try:
            size = os.path.getsize(self.path)
        except OSError:
            return ""
        if size < self.offset:  # truncated/rotated
            self.offset = 0
        if size == self.offset:
            return ""
        with open(self.path, "rb") as f:
            f.seek(self.offset)
            chunk = f.read()
        # Only consume complete lines (track bytes, not decoded chars, so
        # the offset stays exact) — a trailing partial line waits for the
        # next read so line-based markers can never be split.
        last_nl = chunk.rfind(b"\n")
        if last_nl == -1:
            return ""
        self.offset += last_nl + 1
        return chunk[: last_nl + 1].decode("utf-8", errors="replace")

    def scan(self, chunk: str) -> None:
        if not chunk:
            return
        for line in chunk.splitlines():
            s = line.strip()
            m = RE_GAME_FOUND.search(s)
            if m:
                self._pending_game = m.group(1)
            if s == "CONFIRMED!" and self._pending_game:
                if self._pending_game not in self.games_confirmed:
                    self.games_confirmed.append(self._pending_game)
                self._pending_game = None
            elif "does not support" in s or "is hidden" in s:
                self._pending_game = None
        self.blow_starts += chunk.count("Blowing... ")
        self.blow_ends += chunk.count("Ending blow")
        self.blow_successes += chunk.count(" successfully")
        m = RE_GAME_COUNT.search(chunk)
        if m:
            self.game_count = int(m.group(1))
        if RE_ROTATE.search(chunk):
            self.rotate_pending += 1
        m = RE_ROTATING_TO.search(chunk)
        if m:
            self.next_task = ROTATE_TASK_NAMES.get(m.group(1).strip(), m.group(1).strip())
        m = RE_TARGET_DUR.search(chunk)
        if m:
            self.target_duration_ms = int(m.group(1))


class PolicyRunner:
    def __init__(self,
                 game: str,
                 source: str = "rulebot",
                 datadir: str = "/tmp/opencode/pepit_data_lua",
                 profile_index: int = 0,
                 duration: float = 300.0,
                 log_path: str = "/tmp/opencode/pepit_mcp_pepit_compat.log",
                 llm_model: str = "cloudflare_clef-flash",
                 llm_base: str = "http://192.168.0.68:1234",
                 run_id: str | None = None,
                 own_session: bool = False):
        self.game_name = game
        self.game = get_game(game)
        self.source = source  # rulebot | llm | llm-text | llm-image
        self.datadir = datadir
        self.profile_index = profile_index
        self.duration = duration
        self.run_id = run_id or time.strftime("run_%Y%m%d_%H%M%S")
        self.own_session = own_session
        self.llm = LLMClient(model=llm_model, base_url=llm_base) if source.startswith("llm") else None

        self.control = ControlChannel()
        self.log_path = log_path
        self.logf = LogFollower(log_path)
        self.profiles = parse_profiles(os.path.join(datadir, "profiles.ini"))

        self.ctx = {
            "now": 0.0,
            "task": {},
            "blowing": False,
            "blow_start": 0.0,
            "blow_end": 0.0,
        }
        # "change screen" state machine (displayPhysioRotateScreen):
        # shown at game start AND between tasks; blocking; OK zone
        # (230,170,80,60) active from t=0, button drawn after 5 s.
        self._in_rotate = False
        self._rotate_since = None   # clock when the screen was detected
        self._last_ok_tap = 0.0
        self._no_bar_streak = 0     # consecutive bar-less frames (transient filter)
        self.rotates = 0            # change screens passed (status output)
        self._task_success_base = 0  # cumulative " successfully" count at task start
        self.state = {}
        self.actions = []
        self.last_state = {}
        self.decisions = 0
        self.phase = "init"
        self.error = None
        self.start_time = None
        self.stop_event = threading.Event()
        self.finished = False
        self._child = None
        self.status_path = os.path.join(STATUS_DIR, f"pepit_policy_{self.run_id}.json")
        self.stop_path = os.path.join(STATUS_DIR, f"pepit_policy_stop_{self.run_id}")

    # ------------------------------------------------------------------
    # Session lifecycle (only when we own the session)
    # ------------------------------------------------------------------
    def _start_session(self):
        os.makedirs(self.datadir, exist_ok=True)
        env = os.environ.copy()
        env["SDL_VIDEODRIVER"] = "dummy"
        env["SDL_AUDIODRIVER"] = "dummy"
        os.makedirs(os.path.dirname(self.log_path) or ".", exist_ok=True)
        logf = open(self.log_path, "ab")
        self._child = subprocess.Popen(
            [PEPIT_BINARY, "--data-dir", self.datadir, "--scale", "1"],
            stdin=subprocess.DEVNULL, stdout=logf, stderr=subprocess.STDOUT,
            env=env, start_new_session=True)

    def _stop_session(self):
        if self._child is None:
            return
        try:
            self.control.quit()
        except Exception:
            pass
        try:
            self._child.wait(timeout=15)
        except Exception:
            self._child.kill()

    # ------------------------------------------------------------------
    def request_stop(self):
        self.stop_event.set()

    # ------------------------------------------------------------------
    def _status(self, extra=None):
        s = {
            "run_id": self.run_id, "game": self.game_name, "source": self.source,
            "phase": self.phase,
            "running": (self.phase == "play") and not self.finished
                       and not self.stop_event.is_set(),
            "elapsed": round(time.time() - self.start_time, 1) if self.start_time else 0,
            "decisions": self.decisions,
            "profile": self.profiles[self.profile_index]["name"] if self.profiles else None,
            "task": self.ctx.get("task"),
            "selected_game": self.logf.selected_game,
            "games": self.logf.games_confirmed,
            "blows": {"starts": self.logf.blow_starts, "ends": self.logf.blow_ends,
                      "successes": self.logf.blow_successes},
            "last_state": {k: v for k, v in self.last_state.items() if k != "bars"}
            or {k: v for k, v in self.state.items() if k != "bars"},
            "last_actions": [str(a) for a in self.actions],
            "error": self.error,
        }
        if extra:
            s.update(extra)
        tmp = self.status_path + ".tmp"
        with open(tmp, "w") as f:
            json.dump(s, f, indent=1, default=str)
        os.rename(tmp, self.status_path)
        return s

    # ------------------------------------------------------------------
    # Phases
    # ------------------------------------------------------------------
    def _wait_state(self, want, timeout=120.0):
        # monotonic, not wall clock: the host suspends when idle (CLOCK_REALTIME
        # jumps on resume, CLOCK_MONOTONIC does not — the game clock is
        # monotonic too, so only it can be used for gameplay deadlines)
        deadline = time.monotonic() + timeout
        last = -1
        while time.monotonic() < deadline and not self.stop_event.is_set():
            img, meta = self.control.shot(timeout=5)
            last = int(meta.get("systemState", -1))
            if last == want:
                return last, img, meta
            time.sleep(0.25)
        return last, None, None

    def _phase_boot(self):
        self.phase = "boot"
        st, img, meta = self._wait_state(STATE_PROFILE, timeout=150)
        if st != STATE_PROFILE:
            self.error = f"boot: session stuck at state {st}"
            return False
        return True

    def _joystick_click(self, deflections=0):
        """Move selection right `deflections` times, then click the button.

        The firmware's updateJoystickSelection() returns "selected" when the
        button is pressed, so one button press = one selection confirm.

        NOTE: kept for reference/fallback only — the deflection count is
        edge-triggered in the firmware (updateJoystickSelection: one step per
        center->deflect->center cycle) and a cycle the firmware misses leaves
        the selection on the wrong entry with no way to tell. Selection now
        uses direct slot taps (_select_slot), which are level-triggered.
        """
        for _ in range(deflections):
            self.control.joystick_x(0.6)
            time.sleep(0.15)
            self.control.joystick_reset()
            time.sleep(0.2)
        self.control.joystick(32768, 32768, button_pressed=True)
        time.sleep(0.5)
        self.control.joystick_reset()

    def _select_slot(self, index: int, nr: int) -> None:
        """Tap the selection-page slot for entry `index` (of `nr`).

        checkSelectionPageSelection() returns the slot a touch is in, and the
        selection loops (displayProfileSelection / displayGameSelection) act
        on it immediately — level-triggered, checked on every firmware loop
        iteration. A single held tap is bulletproof where N counted
        joystick deflections are not.

        Geometry (gfxHandler.cpp checkSelectionPageSelection):
          columns = min(4, nr); rows = 2 if nr > 4 else 1
          cWidth  = (290 - 10*columns) // columns
          cHeight = 220 (1 row) / 105 (2 rows)
          zone    = (20 + c*(cWidth+10), 10 + r*(cHeight+10), cWidth, cHeight)
        """
        columns = min(4, max(nr, 1))
        rows = 2 if nr > 4 else 1
        c_width = (290 - 10 * columns) // columns
        c_height = 220 if rows == 1 else 105
        c, r = index % columns, index // columns
        x0 = 20 + c * (c_width + 10)
        y0 = 10 + r * (c_height + 10)
        self.control.tap(x0 + c_width // 2, y0 + c_height // 2, hold=0.6)

    def _select_via_tap(self, index, nr, want, page_state, timeout=60.0) -> bool:
        """Tap the target slot, then wait for the firmware to advance to
        `want`. Re-taps only while still on the selection page (a tap there
        is inert); stops if the firmware advanced somewhere unexpected."""
        st, img, meta = STATE_BOOT, None, {}
        for attempt in range(3):
            self._select_slot(index, nr)
            st, img, meta = self._wait_state(want, timeout=timeout)
            if st == want:
                return True
            if st != page_state:
                break  # moved on unexpectedly — do not blindly re-tap
        self.error = f"selection: expected state {want}, got state {st}"
        return False

    def _phase_profile(self):
        self.phase = "profile"
        if self.profiles:
            self.ctx["task_profile"] = self.profiles[self.profile_index]
        return self._select_via_tap(self.profile_index, len(self.profiles),
                                     STATE_GAMESEL, STATE_PROFILE)

    def _phase_gamesel(self):
        self.phase = "gamesel"
        # discover where our game sits in the firmware's filtered list.
        # Wait until the enumeration is *complete* (the "Number of games: N"
        # line), with a grace period in case it never appears.
        deadline = time.monotonic() + 45
        complete_after = None
        while time.monotonic() < deadline:
            self.logf.scan(self.logf.read())
            if self.logf.game_count is not None:
                if complete_after is None:
                    complete_after = time.monotonic()
                if len(self.logf.games_confirmed) >= self.logf.game_count \
                        and time.monotonic() - complete_after > 1.0:
                    break
            time.sleep(0.2)
        if self.game_name not in self.logf.games_confirmed:
            self.error = (f"game '{self.game_name}' not in the firmware's game list "
                          f"({self.logf.games_confirmed})")
            return False
        idx = self.logf.games_confirmed.index(self.game_name)
        return self._select_via_tap(idx, len(self.logf.games_confirmed),
                                     STATE_RUNNING, STATE_GAMESEL)

    def _tap_rotate_ok(self):
        self.control.tap(255, 200, 0.5)  # inside (230, 170, 80, 60)

    def _step_change_screen(self, img, meta, now) -> bool:
        """One step of the change-screen handler (img already captured).

        displayPhysioRotateScreen() is a blocking loop: it shows the task's
        change image, draws the OK button after 5 s, and waits for a touch
        in (230,170,80,60) [the zone is active from t=0]. Returns True once
        the gameplay chrome bars are back (screen dismissed, or absent).
        """
        if E.Extractor(img, meta).bars()["pressure"]["present"]:
            self._in_rotate = False
            self._no_bar_streak = 0
            self._sync_task_after_rotate()
            return True
        button = E.ok_button(img)
        if button and now - self._last_ok_tap > 2.0:
            self._tap_rotate_ok()
            self._last_ok_tap = now
        elif (not button and self._rotate_since is not None
              and now - self._rotate_since > 8.0 and now - self._last_ok_tap > 6.0):
            # button never drawn (shouldn't happen; it appears after 5 s) —
            # the touch zone is active anyway, tap defensively
            self._tap_rotate_ok()
            self._last_ok_tap = now
        if self._rotate_since is not None and now - self._rotate_since > 45.0:
            # give up waiting; resume (worst case: a few stray taps, which
            # are harmless in the lua games)
            self._in_rotate = False
            self._sync_task_after_rotate()
            return True
        return False

    def _phase_rotate(self):
        """Wait out the change screen shown at game start.

        main.cpp calls displayPhysioRotateScreen() right after
        setSystemState(STATE_GAME_RUNNING) to show the *first* task's
        change image (cannula size) before the first blow. If the task has
        no change image no screen appears and the chrome bars show up
        immediately.
        """
        self.phase = "rotate"
        t0 = time.monotonic()
        self._in_rotate = True
        self._rotate_since = t0
        self._last_ok_tap = t0 - 8.0
        while time.monotonic() - t0 < 60.0 and not self.stop_event.is_set():
            img, meta = self.control.shot(timeout=5)
            if img is None:
                time.sleep(0.2)
                continue
            if self._step_change_screen(img, meta, time.monotonic()):
                return self._gameplay_live()
            time.sleep(0.3)
        return self._gameplay_live()

    def _gameplay_live(self) -> bool:
        img, meta = self.control.shot(timeout=5)
        if img is None:
            return False
        return E.Extractor(img, meta).bars()["pressure"]["present"]

    # ------------------------------------------------------------------
    def _update_ctx_from_log(self):
        self.logf.scan(self.logf.read())
        prof = self.ctx.get("task_profile")
        if prof and prof["tasks"]:
            idx = self._task_index()
            self.ctx["task"] = dict(prof["tasks"][idx])
            self.ctx["task"]["index"] = idx
            if not self.ctx["task"].get("time_s"):
                # profile entry without a time — fall back to the log value
                if self.logf.target_duration_ms:
                    self.ctx["task"]["time_s"] = self.logf.target_duration_ms / 1000.0
            # pressure target follows the active task
            if self.ctx["task"].get("target"):
                self.control.pressure_target = self.ctx["task"]["target"]
            if idx != self.ctx.get("task_idx_base"):
                # firmware advanced to a new task (rotate marker / index
                # wrapped): reset the per-task rep baseline and the blow
                # cycle state
                self.ctx["task_idx_base"] = idx
                self._task_success_base = self.logf.blow_successes
                self.ctx["blowing"] = False
                self.ctx["blow_start"] = self.ctx.get("now", 0.0)
                self.ctx["blow_end"] = self.ctx.get("now", 0.0)
            # reps completed within THIS task — the firmware logs one
            # " successfully" per blow that ends after the target duration
            self.ctx["task"]["reps_done"] = max(
                0, self.logf.blow_successes - self._task_success_base)
        if self.logf.next_task:
            self.ctx["next_task"] = self.logf.next_task
            self.logf.next_task = None

    def _task_index(self) -> int:
        """Index of the active profile task.

        The firmware prints one rotate marker per task transition (after a
        task's last blow) and starts at task 0 when the game begins, so
        index = (markers seen) % (number of tasks).
        """
        tasks = (self.ctx.get("task_profile") or {}).get("tasks") or []
        if not tasks:
            return 0
        return self.logf.rotate_pending % len(tasks)

    def _sync_task_after_rotate(self):
        """Re-sync task parameters after a change screen cleared."""
        self.rotates = self.logf.rotate_pending
        self._update_ctx_from_log()
        # start a fresh blow cycle for the new task
        self.ctx["blowing"] = False
        self.ctx["blow_start"] = self.ctx.get("now", 0.0)
        self.ctx["blow_end"] = self.ctx.get("now", 0.0)

    # ------------------------------------------------------------------
    def _phase_play(self):
        self.phase = "play"
        t0 = time.monotonic()
        last_status = 0.0
        loop_s = 0.1 if self.source == "rulebot" else 0.0  # llm latency paces it
        # change-screen machine clocks live in ctx["now"] (relative) domain;
        # _phase_rotate used wall time, so re-arm for gameplay
        self._in_rotate = False
        self._rotate_since = None
        self._last_ok_tap = 0.0
        self._no_bar_streak = 0
        while not self.stop_event.is_set():
            self.ctx["now"] = time.monotonic() - t0
            self._update_ctx_from_log()

            if time.monotonic() - t0 > self.duration:
                self.phase = "timeout"
                break
            if self.ctx["now"] - last_status > 5.0:
                last_status = self.ctx["now"]
                self.last_state = {k: v for k, v in self.state.items() if k != "bars"}
                self._status()

            img, meta = self.control.shot(timeout=5)
            if img is None:
                time.sleep(0.2)
                continue
            st = int(meta.get("systemState", -1))
            if st in (STATE_ENDING, STATE_WIN):
                self.phase = "win" if st == STATE_WIN else "ended"
                break

            self.state = self.game.extract(img, meta, self.ctx)
            ext = E.Extractor(img, meta)
            bars_present = ext.bars()["pressure"]["present"]
            button = E.ok_button(img)

            if self._in_rotate:
                # on the between-task change screen: no gameplay decisions
                self.actions = []
                self._step_change_screen(img, meta, self.ctx["now"])
                time.sleep(0.3)
                continue
            if button:
                # change screen's button drawn — definitive signal
                self._in_rotate = True
                self._rotate_since = self.ctx["now"]
                self._last_ok_tap = 0.0
                self.actions = []
                time.sleep(0.3)
                continue
            if not bars_present:
                # bars gone (or a 1-frame transition black): require a short
                # streak before entering the change-screen handler
                self._no_bar_streak += 1
                if self._no_bar_streak >= 2:
                    self._in_rotate = True
                    self._rotate_since = self.ctx["now"]
                    self._last_ok_tap = 0.0
                self.actions = []
                time.sleep(0.3)
                continue
            self._no_bar_streak = 0

            # decide
            if self.source == "rulebot":
                self.actions = self.game.rulebot(self.state, self.ctx)
            else:
                self.actions = self._decide_llm(img)
            self.decisions += 1
            self.actions = self._apply(self.actions)

            if loop_s:
                time.sleep(loop_s)

    def _decide_llm(self, img) -> list:
        q = self.game.llm_questions(self.state, self.ctx)
        if self.source == "llm-text":
            data = self.llm.decide(q, state_text=E.format_state(self.state))
        elif self.source == "llm-image":
            data = self.llm.decide(q, image=E.img_to_png_b64(img))
        else:  # hybrid: both
            data = self.llm.decide(q,
                                   state_text=E.format_state(self.state),
                                   image=E.img_to_png_b64(img))
        if "error" in data:
            self.error = f"llm: {data['error']}"
            # degrade gracefully to the rulebot for this step
            return self.game.rulebot(self.state, self.ctx)
        return self.game.llm_actions(data, self.state, self.ctx)

    def _apply(self, actions) -> list:
        applied = []
        for a in actions:
            try:
                if a.cmd == "pressure":
                    self.control.pressure_percent(a.args[0])
                elif a.cmd == "tap":
                    self.control.tap(*a.args)
                elif a.cmd == "touch_down":
                    self.control.touch_down(*a.args)
                elif a.cmd == "touch_move":
                    self.control.touch_move(*a.args)
                elif a.cmd == "touch_up":
                    self.control.touch_up()
                elif a.cmd == "joystick_x":
                    self.control.joystick_x(a.args[0])
                elif a.cmd == "joystick_btn":
                    self.control.joystick(32768, 32768, a.args[0])
                applied.append(a)
            except Exception as e:
                self.error = f"apply {a!r}: {e}"
        return applied

    # ------------------------------------------------------------------
    def run(self) -> dict:
        os.makedirs(STATUS_DIR, exist_ok=True)
        self.start_time = time.time()
        if self.own_session:
            self._start_session()
            time.sleep(1.0)
        try:
            ok = self._phase_boot()
            if ok:
                ok = self._phase_profile()
            if ok:
                ok = self._phase_gamesel()
            if ok:
                ok = self._phase_rotate()
            if ok:
                self._phase_play()
        except Exception as e:
            self.error = f"{type(e).__name__}: {e}"
            self.phase = "error"
        finally:
            self.finished = True
            # release all inputs
            try:
                self.control.stop_blow()
                self.control.joystick_reset()
                self.control.touch_up()
            except Exception:
                pass
            self._status()
            if self.own_session:
                self._stop_session()
        return self._status()


def main() -> None:
    ap = argparse.ArgumentParser(description="Run the PEPit policy runner")
    ap.add_argument("--game", required=True, help="miner | plane | farm")
    ap.add_argument("--source", default="rulebot",
                    help="rulebot | llm | llm-text | llm-image (llm = hybrid)")
    ap.add_argument("--data-dir", default="/tmp/opencode/pepit_data_lua")
    ap.add_argument("--profile", type=int, default=0, help="profile index (0 = PEP S)")
    ap.add_argument("--duration", type=float, default=300.0, help="seconds of play")
    ap.add_argument("--log", default="/tmp/opencode/pepit_mcp_pepit_compat.log")
    ap.add_argument("--model", default="cloudflare_clef-flash")
    ap.add_argument("--llm-base", default="http://192.168.0.68:1234")
    ap.add_argument("--attach", action="store_true",
                    help="use an already-running session instead of starting one")
    args = ap.parse_args()

    runner = PolicyRunner(
        game=args.game, source=args.source, datadir=args.data_dir,
        profile_index=args.profile, duration=args.duration, log_path=args.log,
        llm_model=args.model, llm_base=args.llm_base, own_session=not args.attach)
    status = runner.run()
    print(json.dumps(status, indent=2, default=str))
    print(f"status file: {runner.status_path}")


if __name__ == "__main__":
    main()
