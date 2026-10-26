"""Ore Miner (games/miner, "Ore Miner").

A blow game with five sub-games, switched by the profile's task type:

  pepLong  digger   : robot digs; blow to advance the map, touch/joystick
                      left|right to walk 2 steps per dig, mines ore tiles
  pepShort laser    : blow extends a blue ray that breaks the rock (10
                      blows per rock image)
  pepEqual (digger, pepLong script)
  inhalation crosshair: touch aims a crosshair, blowing pushes ore targets
  trampoline         : time-based, jumps (no pressure channel — nothing to do)

Deterministic signals (from the sprite palettes in games/miner/gfx):
  step arrows  : orange #de9500 blobs at the map edges (3,100) / (291,100),
                 only while steps remain
  gas warning  : red+white blinking icon above the robot when the blow
                 has been idle 13s (lose 5 at 15s)
  laser ray    : blue (#0000ff..#ddddff) horizontal line at y 137-143
  rock         : white/gray sprite in the right-center area

Policy: all sub-games reward blowing, so the rulebot keeps a blow cycle
(go for target duration, release, pause, repeat) and walks the digger
left/right when its arrows allow.
"""

import random

from .. import extract as E
from .base import GameSpec, a_pressure, a_tap, a_joystick_x, a_joystick_btn

# zones / palettes (display coordinates)
ORANGE = (0xde, 0x95, 0x00)
RED = (0xff, 0x00, 0x00)
BLUE = (0x00, 0x00, 0xff)
WHITE = (0xff, 0xff, 0xff)

LEFT_ARROW_ZONE = (0, 92, 36, 34)
RIGHT_ARROW_ZONE = (284, 92, 36, 34)
ROBOT_ROW = (0, 15, 320, 100)
LASER_BAND = (40, 128, 180, 26)
ROCK_ZONE = (150, 55, 170, 125)

TAP_LEFT = (50, 120)
TAP_RIGHT = (270, 120)


class MinerSpec(GameSpec):
    name = "miner"

    def __init__(self):
        self._rng = random.Random()
        self._last_move = None
        self._move_counter = 0

    # ------------------------------------------------------------------
    def extract(self, img, meta, ctx) -> dict:
        bars = E.Extractor(img, meta).bars()
        left = self._zone_color(img, LEFT_ARROW_ZONE, ORANGE)
        right = self._zone_color(img, RIGHT_ARROW_ZONE, ORANGE)
        gas = self._zone_color(img, ROBOT_ROW, RED, tol=60)
        laser = self._zone_color(img, LASER_BAND, BLUE, tol=80)
        rock = self._zone_color(img, ROCK_ZONE, WHITE, tol=40)
        return {
            "bars": bars,
            "can_move_left": left > 15,
            "can_move_right": right > 15,
            # The real gas icon is the 41x44 sprite (999 red px) at the
            # robot's position; digger map tile art (a red-striped "ruin"
            # tile) puts ~16 red px in this zone permanently. 100 sits
            # between the two with a wide margin.
            "gas_warning": gas > 100,
            "laser_ray": laser > 20,
            "rock_visible": rock > 40,
            "task": ctx.get("task", {}),
        }

    @staticmethod
    def _zone_color(img, zone, color, tol=24) -> int:
        x, y, w, h = zone
        m = E.color_mask(E.region(img, x, y, w, h), color, tol)
        return int(m.sum())

    # ------------------------------------------------------------------
    def rulebot(self, state, ctx) -> list:
        actions = self._blow_cycle(ctx, gas_warning=state["gas_warning"])
        task_type = ctx.get("task", {}).get("type", "")
        # Only the digger reacts to left/right walking; the arrows are
        # digger-only, so tapping when they show is sub-game safe.
        if task_type in ("pepLong", "pepEqual") and (state["can_move_left"] or state["can_move_right"]):
            self._move_counter += 1
            if self._move_counter % 3 == 0:
                options = [d for d, ok in (("left", state["can_move_left"]),
                                            ("right", state["can_move_right"])) if ok]
                choice = self._rng.choice(options)
                if choice != self._last_move:
                    self._last_move = choice
                    x, y = TAP_LEFT if choice == "left" else TAP_RIGHT
                    actions.append(a_tap(x, y, 0.35))
        return actions

    # ------------------------------------------------------------------
    def llm_questions(self, state, ctx) -> dict:
        return {
            "subgame": {
                "type": "choice",
                "instructions": "Which miner sub-game is shown?",
                "criteria": {
                    "digger": "A tile map (brown tiles) with a small dark robot and orange edge arrows",
                    "laser": "A robot funnel on the left, a rock on the right, a blue laser beam",
                    "crosshair": "A circle outline with a crosshair and floating ore chunks",
                    "other": "Anything else (e.g. a big machine, a shop menu)",
                },
            },
            "gas_warning": {
                "type": "noul",
                "instructions": "Is a red-and-white gas warning symbol visible above the robot?",
            },
        }

    def llm_actions(self, answers, state, ctx) -> list:
        actions = self._blow_cycle(ctx, gas_warning=self._llm_says(answers, "gas_warning") or state["gas_warning"])
        sub, conf, probs = self._llm_choice(answers, "subgame")
        if sub == "digger":
            # Fall back to the pixel arrows — the LLM's spatial calls are
            # weaker than a deterministic scan.
            if state["can_move_left"] != state["can_move_right"]:
                move = "left" if state["can_move_left"] else "right"
                x, y = TAP_LEFT if move == "left" else TAP_RIGHT
                actions.append(a_tap(x, y, 0.35))
        return actions

    @staticmethod
    def _llm_choice(data, qid):
        a = data.get("answers", {}).get(qid, {})
        return a.get("choice"), a.get("confidence", 0.0), a.get("probabilities", {})

    @staticmethod
    def _llm_says(data, qid):
        return data.get("answers", {}).get(qid, {}).get("noul", 0.0) > 0.5

    # ------------------------------------------------------------------
    def _blow_cycle(self, ctx, gas_warning: bool = False) -> list:
        """Keep a blow rhythm: go ~target+1.2s, release, pause ~1.5s."""
        now = ctx["now"]
        task = ctx.get("task", {})
        target_s = task.get("time_s", 5.0)
        if ctx.get("blowing"):
            if gas_warning:
                pass  # keep blowing (that is the cure)
            elif (now - (ctx.get("blow_start") or 0.0)) > target_s + 1.2:
                ctx["blowing"] = False
                ctx["blow_end"] = now
                return [a_pressure(0)]
            return []  # keep the pressure held
        # idle
        if (now - (ctx.get("blow_end") or 0.0)) >= 1.5:
            ctx["blowing"] = True
            ctx["blow_start"] = now
            return [a_pressure(100.0)]
        return []
