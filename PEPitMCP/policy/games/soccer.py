"""Soccer Penalty (games/soccer) — self-built prototype penalty game.

Mechanics (mainGame.lua):
  * one penalty per blow: the ball is struck when the blow FINISHES (the
    firmware's NewRepetition pulse, i.e. at target duration), so the aim
    can move for the whole blow; shot target = the player's aim then
  * aim: joystick (both axes) or touch drag, clamped inside the goal mouth
    (x 92..228, y 58..118) and eased with a ~120 ms time constant
  * the keeper dives to the aim sampled 500 ms BEFORE the blow start
  * goal iff the shot target is > 30 px from the keeper's dive target
  * ball on the penalty spot (160,152) = idle; GOAL!/SAVED! banner, then
    ball + keeper travel back over ~5.8 s

Deterministic signals:
  ball   : the only solid pure-white blob (net/frame are pale blue-gray)
  keeper : blue blob (sprite shirt, rgb 7,112,177 565-quantized)
  banner : green "GOAL!" / red "SAVED!" text in the y 130..165 band
  cross  : yellow aim crosshair, only while idle

Rulebot (open-loop control, closed-loop verification): per rep,
  touch_down in corner A (alternating by rep parity) -> hold 1.6 s ->
  touch_move to the opposite diagonal corner B -> 0.3 s -> release the
  touch and blow; the ball is only struck when the blow FINISHES.
The keeper's -500 ms sample lands inside the A-hold, so the keeper dives
to A; the released aim meanwhile eases back to goal center, so the ball
flies center, ~50 px from the keeper -> goal. Perception (ball/keeper/
banner) is record-only: it verifies the sequence but never steers the bot.
"""

import numpy as np

from .. import extract as E
from .base import GameSpec, a_pressure, a_joystick_xy, \
    a_touch_down, a_touch_move, a_touch_up


def _q565(r, g, b):
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def _deq565(c):
    r = ((c >> 11) & 31) * 255 // 31
    g = ((c >> 5) & 63) * 255 // 63
    b = (c & 31) * 255 // 31
    return (r, g, b)


# exact 565-quantized references (what init.lua's rgb() draws)
BANNER_GREEN = np.array(_deq565(_q565(64, 230, 96)), dtype=np.int16)
BANNER_RED = np.array(_deq565(_q565(238, 48, 48)), dtype=np.int16)
KEEPER_BLUE = np.array(_deq565(_q565(7, 112, 177)), dtype=np.int16)  # sprite shirt
CROSS_YELLOW = np.array(_deq565(_q565(255, 196, 0)), dtype=np.int16)
WHITE = np.array([255, 255, 255], dtype=np.int16)

BANNER_ZONE = (0, 128, 320, 40)   # result text band
PLAY_Y0, PLAY_Y1 = 52, 188        # above HUD, above firmware bars

GOAL_X0, GOAL_X1 = 92, 228
GOAL_Y0, GOAL_Y1 = 58, 118
SPOT = (160, 152)

# aim corners (inside the goal mouth), alternating by rep parity
CORNERS = {
    0: {"A": (112, 70), "B": (208, 104)},   # top-left -> bottom-right
    1: {"A": (208, 70), "B": (112, 104)},   # top-right -> bottom-left
}
HOLD_A_S = 1.6     # keeper's -500 ms sample must land inside this hold
FLICK_TO_BLOW_S = 0.3

JOY_ON = 0.85      # stick deflection for the LLM's corner aim


class SoccerSpec(GameSpec):
    name = "soccer"

    def __init__(self):
        self._phase = "idle"
        self._phase_t = 0.0
        self._reps_seen = -1
        self._joy = (0.0, 0.0)
        self._touch_downed = False

    # ------------------------------------------------------------------
    def extract(self, img, meta, ctx) -> dict:
        bars = E.Extractor(img, meta).bars()
        px = img.astype(np.int16)
        play = np.zeros(px.shape[:2], dtype=bool)
        play[PLAY_Y0:PLAY_Y1, :] = True

        # ball: the only solid pure-white blob (net/frame are pale gray)
        white = np.all(np.abs(px - WHITE) <= 24, axis=2) & play
        ball = None
        if white.sum() > 30:
            blobs = E.find_blobs(white, min_size=36)
            if blobs:
                cx, cy, size = max(blobs, key=lambda b: b[2])
                cx, cy = int(cx), int(cy)
                if cy > 130 and abs(cx - SPOT[0]) < 30:
                    zone = "spot"
                elif GOAL_X0 <= cx <= GOAL_X1 and cy <= GOAL_Y1 + 8:
                    zone = "in_goal"
                else:
                    zone = "flight"
                ball = {"x": cx, "y": cy, "size": int(size), "zone": zone}

        # keeper: blue blob
        blue = np.all(np.abs(px - KEEPER_BLUE) <= 40, axis=2) & play
        keeper = None
        if blue.sum() > 20:
            ys, xs = np.nonzero(blue)
            keeper = {"x": int(xs.mean()), "y": int(ys.mean()),
                      "size": int(blue.sum())}

        # result banner in the text band
        zx, zy, zw, zh = BANNER_ZONE
        band = px[zy:zy + zh, zx:zx + zw]
        green = int((np.all(np.abs(band - BANNER_GREEN) <= 30, axis=2)).sum())
        red = int((np.all(np.abs(band - BANNER_RED) <= 30, axis=2)).sum())
        result = "goal" if green > 120 else ("saved" if red > 120 else None)

        # aim crosshair (idle indicator)
        cross = int((np.all(np.abs(px - CROSS_YELLOW) <= 40, axis=2) & play).sum()) > 8

        return {
            "bars": bars,
            "ball": ball,
            "keeper": keeper,
            "result": result,
            "aiming": cross,
            "task": ctx.get("task", {}),
        }

    # ------------------------------------------------------------------
    def rulebot(self, state, ctx) -> list:
        now = ctx["now"]
        task = ctx.get("task", {})
        reps_left = self._reps_left(task)
        reps_done = task.get("reps_done", 0)
        target_s = task.get("time_s", 5.0)
        actions = []

        # new rep?  -> start the feint sequence for this rep
        if reps_done != self._reps_seen:
            self._reps_seen = reps_done
            self._phase = "aim"
            self._phase_t = now
            self._joy = (0.0, 0.0)
            if reps_left is not None and reps_left <= 0:
                self._phase = "done"

        parity = reps_done % 2
        a_corner = CORNERS[parity]["A"]
        b_corner = CORNERS[parity]["B"]

        if self._phase == "aim" and (reps_left is None or reps_left > 0):
            if not self._touch_downed:
                actions.append(a_touch_down(*a_corner))
                self._touch_downed = True
            if now - self._phase_t >= HOLD_A_S:
                actions.append(a_touch_move(*b_corner))
                self._phase = "flick"
                self._phase_t = now
        elif self._phase == "flick":
            if now - self._phase_t >= FLICK_TO_BLOW_S:
                actions.append(a_touch_up())
                actions.append(a_pressure(100.0))
                self._touch_downed = False
                ctx["blowing"] = True
                ctx["blow_start"] = now
                self._phase = "blow"
                self._phase_t = now
        elif self._phase == "blow":
            if (now - self._phase_t) > target_s + 1.2:
                actions.append(a_pressure(0))
                ctx["blowing"] = False
                ctx["blow_end"] = now
                self._phase = "idle"
                self._phase_t = now
        # "idle" / "done": wait for the next rep
        return actions

    # ------------------------------------------------------------------
    def llm_questions(self, state, ctx) -> dict:
        return {
            "ball_zone": {
                "type": "choice",
                "instructions": "Where is the soccer ball?",
                "criteria": {
                    "penalty spot": "Ball sits on the white penalty spot below the goal",
                    "in flight": "Ball is moving toward / just after the goal (mid-air)",
                    "in goal": "Ball is inside the goal mouth, near the crossbar or a corner",
                    "no ball": "No ball visible",
                },
            },
            "keeper_side": {
                "type": "choice",
                "instructions": "Which side of the goal is the blue keeper on?",
                "criteria": {
                    "left": "Keeper is clearly in the left third of the goal",
                    "center": "Keeper is near the middle of the goal",
                    "right": "Keeper is clearly in the right third of the goal",
                },
            },
            "shoot": {
                "type": "noul",
                "instructions": (
                    "Should you blow now? The keeper dives to where you were "
                    "aiming half a second BEFORE you blow, and the ball is "
                    "struck at the END of the blow, where you are aiming THEN "
                    "- so after starting to blow, move the aim to the "
                    "opposite side of the keeper."
                ),
            },
        }

    def llm_actions(self, answers, state, ctx) -> list:
        actions = []
        a = answers.get("answers", {})
        side = a.get("keeper_side", {}).get("choice")
        ball = state.get("ball")
        on_spot = bool(ball and ball["zone"] == "spot")

        # aim at the corner opposite the keeper (joystick is 2-D in the game)
        if side == "left":
            joy = (JOY_ON, -JOY_ON)
        elif side == "right":
            joy = (-JOY_ON, -JOY_ON)
        else:
            joy = (0.0, -JOY_ON * 0.5)   # center -> up
        if joy != self._joy:
            actions.append(a_joystick_xy(joy[0], joy[1]))
            self._joy = joy

        # blow cycle: start blowing when the model says so, but never stall
        # a task. The ball is struck at blow end, so the joystick (aim) can
        # keep moving mid-blow to steer where the ball goes.
        want = a.get("shoot", {}).get("noul", 0.5) > 0.5
        reps_left = self._reps_left(ctx.get("task", {}))
        now = ctx["now"]
        target_s = ctx.get("task", {}).get("time_s", 5.0)
        if reps_left is not None and reps_left > 0 and on_spot:
            want = want or (now - (ctx.get("blow_end") or 0.0)) > 2.0
        if ctx.get("blowing"):
            if (now - (ctx.get("blow_start") or 0.0)) > target_s + 1.2:
                ctx["blowing"] = False
                ctx["blow_end"] = now
                actions.append(a_pressure(0))
        elif want and (now - (ctx.get("blow_end") or 0.0)) >= 1.0:
            ctx["blowing"] = True
            ctx["blow_start"] = now
            actions.append(a_pressure(100.0))
        return actions

    # ------------------------------------------------------------------
    @staticmethod
    def _reps_left(task) -> int | None:
        reps = task.get("reps")
        done = task.get("reps_done")
        if reps is None or done is None:
            return None
        return max(0, reps - done)

    def summary_line(self, state, ctx) -> str:
        b = state.get("ball")
        k = state.get("keeper")
        parts = [f"ball={'%s(%d,%d)' % (b['zone'], b['x'], b['y']) if b else '-'}"]
        if k:
            parts.append(f"keeper=({k['x']},{k['y']})")
        if state.get("result"):
            parts.append(f"result={state['result']}")
        parts.append(f"phase={self._phase}")
        return " ".join(parts)
