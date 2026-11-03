"""Hot Air Balloon (games/balloon, "Heißluftballon") — pixel-art balloon game.

Mechanics (mainGame.lua / init.lua):
  * balloon at (x,y) with envelope radius 16; joystick (or touch drag) sets
    an eased target velocity, max 70 px/s; y is clamped to [40, 132]
  * fuel: drains 2/s idle + 5/s steering, gains 6/s while blowing, +40 at
    the END of every successful blow (NewRepetition pulse, like soccer);
    hitting an obstacle costs 30
  * clouds and birds spawn right of the screen every 2.2 s and drift left
    at 32 px/s; collision when centre distance <= 16 + r - 3
  * at fuel 0 the balloon falls (dead sprite, "ABGESTÜRZT!") and after 2.2 s
    the run resets to start position with 60 fuel

Deterministic signals:
  envelope : the big red/orange blob in the playfield (flame pixels merge
             into it — centroid stays within ~2 px of the envelope centre)
  crashing : the dead balloon is a grey blob (the envelope colours vanish)
  fuel     : height of the green/red fill inside the gauge at x 7..22
  obstacles: white cloud blobs, dark bird blobs

Rulebot (open loop): continuous blow cycle (wait ~1 s, blow past the target
duration, release) so every repetition pays +40 fuel; steer only vertically,
away from the nearest cloud/bird inside ~150 px, and never into the ground
strip or the top edge.
"""

import math

import numpy as np

from .. import extract as E
from .base import GameSpec, a_pressure, a_joystick_xy


def _q565(r, g, b):
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def _deq565(c):
    r = ((c >> 11) & 31) * 255 // 31
    g = ((c >> 5) & 63) * 255 // 63
    b = (c & 31) * 255 // 31
    return (r, g, b)


# exact 565-quantized references (what gen_sprites.py writes into the BMPs)
CLOUD_REF = np.array(_deq565(_q565(240, 246, 252)), dtype=np.int16)
BIRD_REF = np.array(_deq565(_q565(38, 42, 54)), dtype=np.int16)
OUTLINE_REF = np.array(_deq565(_q565(34, 26, 32)), dtype=np.int16)
DEAD_REF = np.array(_deq565(_q565(128, 132, 138)), dtype=np.int16)
DEAD_DK_REF = np.array(_deq565(_q565(96, 100, 106)), dtype=np.int16)
FUEL_GREEN = np.array(_deq565(_q565(0, 170, 90)), dtype=np.int16)
FUEL_RED = np.array(_deq565(_q565(220, 40, 40)), dtype=np.int16)

PLAY_TOP, PLAY_BOTTOM = 22, 188
GROUND_Y = PLAY_BOTTOM - 23
BALLOON_R = 16
# BalloonY clamp in mainGame.lua: PLAY_TOP + BALLOON_R + 2 .. GROUND_Y - BASKET_DROP
Y_MIN, Y_MAX = 40, 132

GAUGE_X0, GAUGE_X1 = 7, 22      # inner fill column of gauge.bmp at GAUGE_X=4
GAUGE_Y0, GAUGE_Y1 = 24, 74     # interior rows of the gauge sprite


class BalloonSpec(GameSpec):
    name = "balloon"

    def __init__(self):
        self._phase = "wait"
        self._phase_t = 0.0
        self._reps_seen = -1
        self._joy = (0.0, 0.0)

    # ------------------------------------------------------------------
    def extract(self, img, meta, ctx) -> dict:
        bars = E.Extractor(img, meta).bars()
        px = img.astype(np.int16)

        # envelope: red/orange hue — sky, clouds, birds, ground never match
        env = (px[..., 0] >= 170) & (px[..., 1] <= 175) & (px[..., 2] <= 110)
        env[:, :30] = False       # gauge sits here; balloon starts at x=30
        balloon = None
        env_blobs = [b for b in E.find_blobs(env, min_size=200) if b[2] >= 350]
        if env_blobs:
            cx, cy, size = max(env_blobs, key=lambda b: b[2])
            balloon = {"x": int(cx), "y": int(cy), "size": int(size)}

        # dead grey balloon while crashing (envelope colours are gone)
        grey = (np.all(np.abs(px - DEAD_REF) <= 18, axis=2)
                | np.all(np.abs(px - DEAD_DK_REF) <= 18, axis=2))
        grey[:PLAY_TOP, :] = False
        grey[PLAY_BOTTOM:, :] = False
        crashing = balloon is None and any(
            b[2] >= 200 for b in E.find_blobs(grey, min_size=200))

        # fuel from the gauge fill height
        band = px[GAUGE_Y0:GAUGE_Y1, GAUGE_X0:GAUGE_X1 + 1]
        fill = int((np.all(np.abs(band - FUEL_GREEN) <= 28, axis=2)
                    | np.all(np.abs(band - FUEL_RED) <= 28, axis=2)).sum())
        fuel = min(100.0, (fill / 16.0) * 100.0 / 46.0)

        # obstacles: white clouds (skip the "Passiert" text band) and dark birds,
        # restricted to the playfield band so firmware chrome never counts
        band = np.zeros(px.shape[:2], dtype=bool)
        band[33:PLAY_BOTTOM, :] = True
        cloud = np.all(np.abs(px - CLOUD_REF) <= 22, axis=2) & band
        cloud[:, :30] = False
        bird = (np.all(np.abs(px - BIRD_REF) <= 30, axis=2)
                | np.all(np.abs(px - OUTLINE_REF) <= 25, axis=2)) & band
        bird[:, :30] = False

        obstacles = []
        for cx, cy, size in E.find_blobs(cloud, min_size=60):
            obstacles.append({"x": int(cx), "y": int(cy),
                              "r": min(20, max(8, int(math.sqrt(size / 2.6) + 0.5))),
                              "kind": "cloud"})
        for cx, cy, size in E.find_blobs(bird, min_size=40):
            obstacles.append({"x": int(cx), "y": int(cy),
                              "r": min(20, max(8, int(math.sqrt(size * 0.74) + 0.5))),
                              "kind": "bird"})

        return {
            "bars": bars,
            "balloon": balloon,
            "crashing": crashing,
            "fuel": fuel,
            "obstacles": obstacles,
            "task": ctx.get("task", {}),
        }

    # ------------------------------------------------------------------
    def rulebot(self, state, ctx) -> list:
        now = ctx["now"]
        task = ctx.get("task", {})
        reps_done = task.get("reps_done", 0)
        reps_left = self._reps_left(task)
        target_s = task.get("time_s", 5.0)
        actions = []

        if reps_done != self._reps_seen:
            self._reps_seen = reps_done
            self._phase = "done" if (reps_left is not None and reps_left <= 0) else "wait"
            self._phase_t = now

        fuel = state.get("fuel")
        want_blow = self._phase == "wait" and (reps_left is None or reps_left > 0)
        if want_blow and (now - self._phase_t >= 1.0 or (fuel is not None and fuel < 30)):
            actions.append(a_pressure(100.0))
            ctx["blowing"] = True
            ctx["blow_start"] = now
            self._phase = "blow"
            self._phase_t = now
        elif self._phase == "blow" and (now - self._phase_t) > target_s + 1.2:
            actions.append(a_pressure(0))
            ctx["blowing"] = False
            ctx["blow_end"] = now
            self._phase = "wait"
            self._phase_t = now

        joy = self._steer(state)
        if joy != self._joy:
            actions.append(a_joystick_xy(joy[0], joy[1]))
            self._joy = joy
        return actions

    # ------------------------------------------------------------------
    def _steer(self, state) -> tuple:
        b = state.get("balloon")
        if not b or state.get("crashing"):
            return (0.0, 0.0)
        bx, by = b["x"], b["y"]
        threats = [o for o in state.get("obstacles", []) if bx - 30 < o["x"] < bx + 150]
        if not threats:
            # drift back toward the middle of the playfield
            if by < 80:
                return (0.0, 0.35)
            if by > 110:
                return (0.0, -0.35)
            return (0.0, 0.0)
        t = min(threats, key=lambda o: abs(o["x"] - bx))
        dy = t["y"] - by
        clearance = BALLOON_R + t["r"] + 8
        if abs(dy) >= clearance:
            return (0.0, 0.0)
        # move vertically away, bouncing off the ground strip and top edge
        up = dy > 0
        if up and by <= Y_MIN + 8:
            up = False
        elif not up and by >= Y_MAX - 8:
            up = True
        return (0.0, -0.8 if up else 0.8)

    # ------------------------------------------------------------------
    def llm_questions(self, state, ctx) -> dict:
        return {
            "danger_side": {
                "type": "choice",
                "instructions": "Where is the nearest cloud or bird relative to the balloon?",
                "criteria": {
                    "above": "Nearest obstacle is above the balloon",
                    "below": "Nearest obstacle is below the balloon",
                    "right": "Nearest obstacle is to the right and far away",
                    "none": "No obstacle close to the balloon",
                },
            },
            "blow": {
                "type": "noul",
                "instructions": (
                    "Should you blow now? Blowing completes the breathing task, "
                    "stokes the flame and adds fuel at the END of the blow. "
                    "Steer the balloon away from clouds and birds."
                ),
            },
        }

    def llm_actions(self, answers, state, ctx) -> list:
        actions = []
        side = answers.get("answers", {}).get("danger_side", {}).get("choice")
        joy = (0.0, 0.0)
        if side == "above":
            joy = (0.0, 0.8)
        elif side == "below":
            joy = (0.0, -0.8)
        if joy != self._joy:
            actions.append(a_joystick_xy(joy[0], joy[1]))
            self._joy = joy

        reps_left = self._reps_left(ctx.get("task", {}))
        now = ctx["now"]
        target_s = ctx.get("task", {}).get("time_s", 5.0)
        want = answers.get("answers", {}).get("blow", {}).get("noul", 0.5) > 0.5
        fuel = state.get("fuel")
        if ctx.get("blowing"):
            if (now - (ctx.get("blow_start") or 0.0)) > target_s + 1.2:
                ctx["blowing"] = False
                ctx["blow_end"] = now
                actions.append(a_pressure(0))
        elif (want or (fuel is not None and fuel < 30)) and (reps_left is None or reps_left > 0):
            if now - (ctx.get("blow_end") or 0.0) >= 1.0:
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
        b = state.get("balloon")
        pos = f"({b['x']},{b['y']})" if b else "-"
        return (f"{self.name}: fuel={state.get('fuel', 0):.0f} "
                f"balloon={pos} obstacles={len(state.get('obstacles', []))} "
                f"state={'crashing' if state.get('crashing') else 'flying'}")
