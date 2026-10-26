"""Tradewings (games/plane) — a flying game.

Mechanics (mainGame.lua):
  * the plane is drawn at screen center; blowing raises speed on each new
    repetition, speed decays on its own; altitude follows speed
    (> 50 climbs, < 50 sinks)
  * steering: joystick X (or the touch zones on the right edge)
  * earning: fly the target ship into reach (dist < 3) at low altitude;
    the target is marked by an orange down-arrow (far) / orange ring (near)
  * losing: speed -> 0 means altitude -> 0 = drowning (splash, -10,
    recover after 10s); storms push you down

Deterministic signals (sprite palettes in games/plane/gfx):
  target : orange blob (#de9500 arrow / #bd7108 ring), play area only
  plane  : red blob centered at (160,120) — orientation via PCA
  speed  : gray needle (#8b9194) around the dial center (288,148),
           angle/2pi * 600 = speed
  splash : cyan/white blob at (160,160) while drowning

Rulebot v1: steer the target toward screen center; blow while the target
is far (climb + approach), cut blowing once it is near (glide in low).
TURN_SIGN calibrates steering direction — verify empirically on first run.
"""

import math

import numpy as np

from .. import extract as E
from .base import GameSpec, a_pressure, a_joystick_x

# steering calibration: +1 = pushing the stick right moves the target
# toward the right edge of the screen; flip if E2E shows the opposite.
TURN_SIGN = 1.0
TURN_ON = 0.5      # stick deflection while correcting
CENTER_X = 160

ORANGE_LO = np.array([150, 80, 0], dtype=np.int16)
ORANGE_HI = np.array([255, 210, 110], dtype=np.int16)
RED_LO = np.array([140, 0, 0], dtype=np.int16)
RED_HI = np.array([255, 110, 110], dtype=np.int16)
NEEDLE = np.array([139, 145, 148], dtype=np.int16)
CYAN_LO = np.array([0, 180, 200], dtype=np.int16)
CYAN_HI = np.array([255, 255, 255], dtype=np.int16)

DIAL_CENTER = (288, 148)
DIAL_ZONE = (252, 112, 68, 68)
PLAY_AREA = (0, 0, 320, 185)   # above the firmware bars


class PlaneSpec(GameSpec):
    name = "plane"

    def __init__(self):
        self._last_turn = 0.0

    # ------------------------------------------------------------------
    def extract(self, img, meta, ctx) -> dict:
        bars = E.Extractor(img, meta).bars()
        px = img.astype(np.int16)

        # target: orange blobs in the play area
        tmask = np.all((px >= ORANGE_LO) & (px <= ORANGE_HI), axis=2)
        tmask[185:, :] = False
        target = None
        if tmask.sum() > 10:
            blobs = E.find_blobs(tmask, min_size=8)
            if blobs:
                cx, cy, size = max(blobs, key=lambda b: b[2])
                target = {"x": int(cx), "y": int(cy), "size": int(size),
                          "near": size > 200}  # ring (near) vs arrow (far)

        # plane orientation: red pixels around screen center
        rmask = np.all((px >= RED_LO) & (px <= RED_HI), axis=2)
        ys, xs = np.nonzero(rmask)
        plane_angle = None
        if len(xs) > 20:
            x0, y0 = 160, 120
            vx = xs - x0
            vy = ys - y0
            cov = np.cov(np.vstack([vx, vy]))
            evals, evecs = np.linalg.eigh(cov)
            ax, ay = evecs[:, -1]  # principal axis
            plane_angle = math.degrees(math.atan2(ay, ax))

        # speed: gray needle around the dial center
        nmask = np.all(np.abs(px - NEEDLE) <= 45, axis=2)
        nmask &= np.ones_like(nmask)
        y0, y1 = DIAL_ZONE[1], DIAL_ZONE[1] + DIAL_ZONE[3]
        x0, x1 = DIAL_ZONE[0], DIAL_ZONE[0] + DIAL_ZONE[2]
        nzone = np.zeros_like(nmask)
        nzone[y0:y1, x0:x1] = nmask[y0:y1, x0:x1]
        speed = None
        if nzone.sum() > 5:
            ys, xs = np.nonzero(nzone)
            dx = float(xs.mean() - DIAL_CENTER[0])
            dy = float(ys.mean() - DIAL_CENTER[1])
            if abs(dx) + abs(dy) > 3:
                ang = math.atan2(dx, -dy)  # 0 = up, clockwise positive
                ang = ang % (2 * math.pi)
                speed = round(ang / (2 * math.pi) * 600, 1)

        # drowning splash: drawn at (160,160) ALIGN_V_BOTTOM, so it grows
        # UP from y=160. The zone must stop above the firmware chrome bars
        # (y=190+) — the white bar borders otherwise match this mask and
        # make "drowning" permanently true.
        smask = np.all((px >= CYAN_LO) & (px <= CYAN_HI), axis=2)
        smask = smask[60:188, 80:240]
        splash = int(smask.sum()) > 100

        return {
            "bars": bars,
            "target": target,
            "plane_angle": round(plane_angle, 1) if plane_angle is not None else None,
            "speed": speed,
            "drowning": splash,
        }

    # ------------------------------------------------------------------
    def rulebot(self, state, ctx) -> list:
        actions = self._pressure(state, ctx)
        t = state.get("target")
        turn = 0.0
        if t is not None:
            err = t["x"] - CENTER_X
            if err > 22:
                turn = TURN_ON * TURN_SIGN
            elif err < -22:
                turn = -TURN_ON * TURN_SIGN
        if turn != self._last_turn:
            actions.append(a_joystick_x(turn))
            self._last_turn = turn
        return actions

    @staticmethod
    def _reps_left(task) -> int | None:
        """Reps still to complete in the active task; None = no rep count
        in the task (e.g. trampoline) so game logic applies."""
        reps = task.get("reps")
        done = task.get("reps_done")
        if reps is None or done is None:
            return None
        return max(0, reps - done)

    def _pressure(self, state, ctx) -> list:
        """Blow while the target is far (climb + approach fast); once it is
        near, glide (no blowing) so altitude sinks toward the ship.
        Critical: if we are drowning, blow hard to recover speed.

        A rep only counts when the blow ENDS (duration > target), and the
        count is position-independent — so while the task still has reps
        left, keep a fixed blow cycle (blow ~target+1.2s, release, brief
        pause, repeat) exactly like the miner does. Without this, a plane
        that has reached the target would glide forever and a multi-rep
        task would never finish. The near/far glide logic only applies
        once the rep count is satisfied.
        """
        now = ctx["now"]
        task = ctx.get("task", {})
        target_s = task.get("time_s", 1.0)
        t = state.get("target")
        near = bool(t and t["near"])
        reps_left = self._reps_left(task)
        if state.get("drowning"):
            want_blow = True
        elif reps_left is not None and reps_left > 0:
            want_blow = True  # complete the task on schedule
        elif near:
            want_blow = False
        else:
            speed = state.get("speed")
            want_blow = speed is None or speed < 420

        if ctx.get("blowing"):
            if (now - (ctx.get("blow_start") or 0.0)) > target_s + 1.2:
                ctx["blowing"] = False
                ctx["blow_end"] = now
                return [a_pressure(0)]
            return []  # keep the current pressure held
        if want_blow and (now - (ctx.get("blow_end") or 0.0)) >= 0.8:
            ctx["blowing"] = True
            ctx["blow_start"] = now
            return [a_pressure(100.0)]
        return []

    # ------------------------------------------------------------------
    def llm_questions(self, state, ctx) -> dict:
        t = state.get("target")
        if t:
            side = "left" if t["x"] < CENTER_X - 25 else (
                "right" if t["x"] > CENTER_X + 25 else "center")
        else:
            side = "center"
        return {
            "target_side": {
                "type": "choice",
                "instructions": (
                    "Where is the orange target marker (arrow or ring) relative to the plane?"
                ),
                "criteria": {
                    "left": "Target marker is clearly left of the plane",
                    "center": "Target marker is near the plane / not clearly to a side",
                    "right": "Target marker is clearly right of the plane",
                },
            },
            "blow": {
                "type": "noul",
                "instructions": (
                    "Should the pilot blow now? Blowing climbs and accelerates; "
                    "not blowing sinks and decelerates. Earning requires being LOW when "
                    "reaching the target ship, but too little speed means drowning."
                ),
            },
        }

    def llm_actions(self, answers, state, ctx) -> list:
        actions = []
        a = answers.get("answers", {})
        side = a.get("target_side", {}).get("choice")
        turn = 0.0
        if side == "left":
            turn = -TURN_ON * TURN_SIGN
        elif side == "right":
            turn = TURN_ON * TURN_SIGN
        if turn != self._last_turn:
            actions.append(a_joystick_x(turn))
            self._last_turn = turn

        want = a.get("blow", {}).get("noul", 0.5) > 0.5
        reps_left = self._reps_left(ctx.get("task", {}))
        if reps_left is not None and reps_left > 0:
            want = True  # complete the task on schedule, model can't stall it
        now = ctx["now"]
        target_s = ctx.get("task", {}).get("time_s", 1.0)
        if ctx.get("blowing"):
            if (now - (ctx.get("blow_start") or 0.0)) > target_s + 1.2:
                # reps need a blow END — release on schedule even if the
                # model keeps saying "keep blowing"
                ctx["blowing"] = False
                ctx["blow_end"] = now
                actions.append(a_pressure(0))
            elif not want:
                ctx["blowing"] = False
                ctx["blow_end"] = now
                actions.append(a_pressure(0))
        elif want and (now - (ctx.get("blow_end") or 0.0)) >= 0.8:
            ctx["blowing"] = True
            ctx["blow_start"] = now
            actions.append(a_pressure(100.0))
        return actions
