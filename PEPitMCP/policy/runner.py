"""PolicyRunner — plays a PEPit lua game for us.

Phases:
  boot     : wait for the session to reach profile selection (state 1)
  profile  : pick the profile (slot tap; level-triggered, bulletproof)
  gamesel  : pick the game (slot tap; index discovered from the boot log)
  rotate   : tap through between-task "rotate the device" screens
  play     : loop  shot -> extract -> decide (rulebot | llm) -> apply
  finish   : release inputs, write the summary

Menu fast path (autonav): pepit_compat accepts --profile/--game and makes
the firmware pick them itself (shim/autonav.cpp). When a session was
launched with those flags (auto_profile/auto_game set here), the profile
and gamesel phases WAIT for the firmware to advance and only tap as a
fallback if the page is still up after a grace window — the shim stops
tapping permanently once it gives up, so a fallback tap can never collide
with one of its.

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
from .frames import FrameSaver
from .games import get_game
from .llm import LLMClient
from .verifier import FastVerifier

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
# autonav (shim/autonav.cpp) terminal lines — after any of these the shim
# will never tap that stage again, so the runner may fall back to a tap.
RE_AUTONAV_TERMINAL = re.compile(
    r"\[autoNav\] (profile|game): (gave up after retries|not resolved|not selectable)")
# The shim emits PRE_SETUP_MARKER once per process (right before setup())
# and an exit line when it dies (shim/main.cpp). The firmware log is
# SHARED across sessions — every session appends to the same file — so a
# follower must ingest only bytes from its own session's marker onward;
# anything earlier belongs to earlier sessions (see LogFollower).
SESSION_START_MARK = b"PRE_SETUP_MARKER\n"
SESSION_END_MARKS = (b"Exiting cleanly", b"Signal received, exiting")
ROTATE_TASK_NAMES = {
    "Short blows": "pepShort",
    "Long blows": "pepLong",
    "Equal blows": "pepEqual",
    "Inhalation": "inhalation",
    "Inhalation+PEP": "inhalationPep",
    "Trampoline": "trampoline",
}

STATE_BOOT, STATE_PROFILE, STATE_GAMESEL, STATE_RUNNING, STATE_ENDING, STATE_WIN = range(6)


def ini_name(path: str) -> str | None:
    """The `name = ...` value of an INI file (first occurrence), or None."""
    try:
        text = open(path).read()
    except OSError:
        return None
    for line in text.splitlines():
        m = re.match(r"\s*name\s*=\s*(\S.*?)\s*$", line)
        if m:
            return m.group(1)
    return None


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


def _session_start_offset(path: str) -> tuple[int, bool]:
    """(offset, started) for a fresh follower on the shared session log.

    Returns the byte offset just past the last session-start marker.
    `started` is False when the marker's own session has already exited
    (an exit marker follows it) or no marker exists: then the follower
    must wait for the next session's marker before ingesting anything,
    so earlier sessions' blow/rotate lines can never be re-counted.
    """
    try:
        with open(path, "rb") as f:
            data = f.read()
    except OSError:
        return 0, False
    idx = data.rfind(SESSION_START_MARK)
    if idx == -1:
        return 0, False
    tail = data[idx + len(SESSION_START_MARK):]
    for end in SESSION_END_MARKS:
        if end in tail:
            return idx + len(SESSION_START_MARK), False  # that session is dead
    return idx + len(SESSION_START_MARK), True


class LogFollower:
    """Tail the session log; expose marker-derived facts to the runner.

    The log file is shared across sessions (each appends to it), so this
    follower only ingests bytes from the current session's start marker
    (SESSION_START_MARK, one per process) onward. Bytes before it belong
    to earlier sessions and are consumed from the file but never scanned.
    """

    def __init__(self, path: str):
        self.path = path
        self.offset, self._session_started = _session_start_offset(path)
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
        # autonav stage -> True once its shim gave up (terminal; no more taps)
        self.autonav_terminal = {"profile": False, "game": False}

    def read(self) -> str:
        try:
            size = os.path.getsize(self.path)
        except OSError:
            return ""
        if size < self.offset:  # truncated/rotated
            self.offset = 0
            self._session_started = False
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
        head = chunk[: last_nl + 1]
        if not self._session_started:
            # Bytes before this session's start marker belong to earlier
            # sessions: consume them from the file, but never ingest.
            idx = head.rfind(SESSION_START_MARK)
            if idx == -1:
                self.offset += last_nl + 1
                return ""
            self._session_started = True
            head = head[idx + len(SESSION_START_MARK):]
        self.offset += last_nl + 1
        return head.decode("utf-8", errors="replace")

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
            m = RE_AUTONAV_TERMINAL.search(s)
            if m:
                self.autonav_terminal[m.group(1)] = True
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
        for line in chunk.splitlines():
            m = RE_AUTONAV_TERMINAL.search(line)
            if m:
                self.autonav_terminal[m.group(1)] = True


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
                 own_session: bool = False,
                 auto_profile: str | None = None,
                 auto_game: str | None = None,
                 autonav: bool | None = None,
                 verify: bool = True):
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
        self.frames = FrameSaver(self.run_id)
        # Fast in-loop visual verifier (decision model, ~1.7 s/call).
        # Short timeout: it must never stall gameplay on a slow LAN peer.
        self.verifier = FastVerifier(
            LLMClient(model=llm_model, base_url=llm_base, timeout=8.0),
            self.run_id, frames_dir=self.frames.dir, enabled=verify)
        self.log_path = log_path
        self.logf = LogFollower(log_path)
        self.profiles = parse_profiles(os.path.join(datadir, "profiles.ini"))

        # Menu fast path (autonav): the specs passed to / received from
        # pepit_compat --profile/--game. None = stage navigated the classic
        # way (runner taps). When we own the session and no explicit spec
        # was given, derive them (profile by index — unambiguous; game by
        # the gameconfig.ini display name — robust to list reordering).
        if autonav is None:
            autonav = own_session
        # Derive missing specs only when WE launch the binary (own_session):
        # then we control the flags. When attaching to a running session the
        # flags are fixed, so use exactly what was passed (None = that stage
        # was not auto-nav'd -> classic tap navigation, never re-derive).
        if autonav and own_session:
            if auto_profile is None:
                auto_profile = str(profile_index)
            if auto_game is None:
                auto_game = self._ini_game_name(game)
        elif not autonav:
            auto_profile = auto_game = None
        self.auto_profile = auto_profile
        self.auto_game = auto_game
        self.autonav = autonav

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

    def _ini_game_name(self, game_dir: str) -> str | None:
        """Display name from <datadir>/games/<game_dir>/gameconfig.ini.

        The autonav --game spec is matched against the gameconfig.ini `name`
        key (that's what the shim resolves), not the directory name.
        """
        return ini_name(os.path.join(self.datadir, "games", game_dir, "gameconfig.ini"))

    # ------------------------------------------------------------------
    # Session lifecycle (only when we own the session)
    # ------------------------------------------------------------------
    def _start_session(self):
        os.makedirs(self.datadir, exist_ok=True)
        env = os.environ.copy()
        env["SDL_VIDEODRIVER"] = "dummy"
        env["SDL_AUDIODRIVER"] = "dummy"
        os.makedirs(os.path.dirname(self.log_path) or ".", exist_ok=True)
        # We own this session: start the shared firmware log clean, so the
        # file holds exactly this session's output (LogFollower also skips
        # bytes before the session's start marker, so stale content from
        # earlier sessions can never be ingested).
        try:
            os.truncate(self.log_path, 0)
        except OSError:
            pass
        logf = open(self.log_path, "ab")
        cmd = [PEPIT_BINARY, "--data-dir", self.datadir, "--scale", "1"]
        if self.auto_profile is not None:
            cmd += ["--profile", self.auto_profile]
        if self.auto_game is not None:
            cmd += ["--game", self.auto_game]
        self._child = subprocess.Popen(
            cmd,
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
            "autonav": {"profile": self.auto_profile, "game": self.auto_game},
            "frames": self.frames.summary(),
            "verifier": {"checks": self.verifier.checks,
                          "mismatches": self.verifier.mismatches},
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
    def _wait_state(self, want, timeout=120.0, want_extra=None):
        # monotonic, not wall clock: the host suspends when idle (CLOCK_REALTIME
        # jumps on resume, CLOCK_MONOTONIC does not — the game clock is
        # monotonic too, so only it can be used for gameplay deadlines)
        accept = {want} | (want_extra or set())
        deadline = time.monotonic() + timeout
        last = -1
        while time.monotonic() < deadline and not self.stop_event.is_set():
            img, meta = self.control.shot(timeout=5)
            last = int(meta.get("systemState", -1))
            if last in accept:
                return last, img, meta
            time.sleep(0.25)
        return last, None, None

    def _phase_boot(self):
        self.phase = "boot"
        accept = {STATE_PROFILE}
        if self.auto_profile or self.auto_game:
            # Autonav races through the menu: the profile page is only up
            # for ~1-2 s, so our polling can legitimately miss it. Accept
            # any past-boot state instead of waiting for state 1 in vain.
            accept |= {STATE_GAMESEL, STATE_RUNNING, STATE_ENDING, STATE_WIN}
        st, img, meta = self._wait_state(STATE_PROFILE, timeout=150,
                                         want_extra=accept - {STATE_PROFILE})
        if st not in accept:
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

    def _wait_autonav(self, targets, timeout=30.0):
        """Wait for the shim's autonav to move us out of the current menu
        page into one of `targets` (a set of systemStates).

        Returns (result, last_state):
          "done"     — firmware advanced into a target state
          "gave_up"  — the shim logged a terminal failure for this stage and
                       will never tap again — a fallback tap is safe
          "timeout"  — neither happened within `timeout`; the caller must
                       check `last_state` before tapping (a shim still
                       mid-retry must not be second-guessed)
        """
        key = "profile" if self.phase == "profile" else "game"
        deadline = time.monotonic() + timeout
        last = -1
        while time.monotonic() < deadline and not self.stop_event.is_set():
            img, meta = self.control.shot(timeout=5)
            last = int(meta.get("systemState", -1))
            self.logf.scan(self.logf.read())
            if last in targets:
                return "done", last
            if self.logf.autonav_terminal.get(key):
                return "gave_up", last
            time.sleep(0.25)
        img, meta = self.control.shot(timeout=5)
        if img is not None:
            last = int(meta.get("systemState", -1))
        self.logf.scan(self.logf.read())
        if self.logf.autonav_terminal.get(key):
            return "gave_up", last
        return "timeout", last

    def _phase_profile(self):
        self.phase = "profile"
        if self.profiles:
            self.ctx["task_profile"] = self.profiles[self.profile_index]
        if self.auto_profile:
            # The firmware navigates this page itself (autonav). Wait for it
            # to advance. The fallback tap below is only reached when the
            # shim declared a terminal failure (or the window expired with
            # the page still up) — it never fires while the shim is still
            # retrying, so the two tap sources cannot collide.
            res, st = self._wait_autonav({STATE_GAMESEL, STATE_RUNNING})
            if st in (STATE_GAMESEL, STATE_RUNNING):
                return True  # includes manometer mode (skips game selection)
            if res == "done":
                return True
            if st != STATE_PROFILE:
                self.error = f"profile: session left the menu (state {st})"
                return False
            # still on the profile page: the shim gave up — classic fallback
        return self._select_via_tap(self.profile_index, len(self.profiles),
                                     STATE_GAMESEL, STATE_PROFILE)

    def _phase_gamesel(self):
        self.phase = "gamesel"
        if self.auto_game:
            # The firmware navigates this page itself (autonav) — wait for it
            # to start the game; fallback tap only if the shim gave up and
            # the page is still up (see _phase_profile for the collision
            # argument; _wait_autonav keys on self.phase, set above).
            res, st = self._wait_autonav(
                {STATE_RUNNING, STATE_ENDING, STATE_WIN})
            if st in (STATE_RUNNING, STATE_ENDING, STATE_WIN):
                return True
            if st != STATE_GAMESEL:
                self.error = f"gamesel: session left the menu (state {st})"
                return False
            # still on the game page: the shim gave up — classic fallback
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
            self.frames.save(img, meta, self._frame_ctx())  # throttled to 1/s
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

    def _frame_ctx(self, reason: str | None = None) -> dict:
        """Compact per-frame context for the FrameSaver metadata record."""
        c = {
            "phase": self.phase,
            "ctx_now": round(self.ctx.get("now", 0.0), 2),
            "reason": reason,
            "decisions": self.decisions,
        }
        task = self.ctx.get("task") or {}
        if task:
            c["task_type"] = task.get("type")
            c["task_index"] = task.get("index")
            c["reps_done"] = task.get("reps_done")
        if self.ctx.get("blowing"):
            c["blowing"] = True
        return c

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
            self.ctx["in_rotate"] = self._in_rotate

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
                self.frames.save(img, meta, self._frame_ctx(self.phase), force=True)
                # final screen: clear expectations (no bars on a win screen)
                self.ctx.pop("in_rotate", None)
                self.ctx.pop("bars_present", None)
                self.verifier.maybe(img, meta, self.ctx, reason=self.phase)
                break

            self.frames.save(img, meta, self._frame_ctx())  # throttled to 1/s

            self.state = self.game.extract(img, meta, self.ctx)
            ext = E.Extractor(img, meta)
            bars_present = ext.bars()["pressure"]["present"]
            self.ctx["bars_present"] = bars_present
            button = E.ok_button(img)

            if self._in_rotate:
                # on the between-task change screen: no gameplay decisions
                self.actions = []
                self._step_change_screen(img, meta, self.ctx["now"])
                if button:
                    # button drawn = definitively the change screen; verify it
                    self.verifier.maybe(img, meta, self.ctx, reason="change-screen")
                time.sleep(0.3)
                continue
            if button:
                # change screen's button drawn — definitive signal
                self._in_rotate = True
                self.ctx["in_rotate"] = True   # sync for the verifier's expectation
                self._rotate_since = self.ctx["now"]
                self._last_ok_tap = 0.0
                self.frames.save(img, meta, self._frame_ctx("button"), force=True)
                self.verifier.maybe(img, meta, self.ctx, reason="button")
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
                    self.frames.save(img, meta, self._frame_ctx("change-screen"), force=True)
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

            # Fast visual cross-check (rate-limited; skipped mid-blow so a
            # slow LLM round-trip can never disturb rep timing).
            if not self.ctx.get("blowing"):
                self.verifier.maybe(img, meta, self.ctx, reason="loop")

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
                elif a.cmd == "joystick_y":
                    self.control.joystick_y(a.args[0])
                elif a.cmd == "joystick_xy":
                    self.control.joystick_xy(a.args[0], a.args[1])
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
            self.frames.close()   # contact sheet + metadata flush
            self.verifier.close()
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
    ap.add_argument("--no-autonav", action="store_true",
                    help="disable the menu fast path (pepit_compat "
                         "--profile/--game) and navigate with taps like the "
                         "classic flow — for the sessions we start ourselves")
    ap.add_argument("--no-verify", action="store_true",
                    help="disable the in-loop visual verifier (decision-model "
                         "cross-check of frames while playing)")
    args = ap.parse_args()

    runner = PolicyRunner(
        game=args.game, source=args.source, datadir=args.data_dir,
        profile_index=args.profile, duration=args.duration, log_path=args.log,
        llm_model=args.model, llm_base=args.llm_base, own_session=not args.attach,
        # attaching: a foreign session may or may not autonav — default to the
        # classic (immediate-tap) flow; own session: autonav unless disabled
        autonav=(None if args.attach else not args.no_autonav),
        verify=not args.no_verify)
    status = runner.run()
    print(json.dumps(status, indent=2, default=str))
    print(f"status file: {runner.status_path}")


if __name__ == "__main__":
    main()
