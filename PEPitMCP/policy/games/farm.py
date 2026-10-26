"""Bauernleben (games/farm) — a farm management game (touch + joystick,
no blowing mechanics in the game itself).

Two screens:
  OVERVIEW : energy bar (6 segments at y=145); tap the berry bush to pick
             (+10, costs 0.2 energy), tap the fish (lottery 0/5/12/20/30,
             costs 0.2), or enter the field; joystick selects an item
  FIELD    : a plow / water / seed grid; back arrow returns

Energy regens +0.1 per new blow repetition, so the rhythm is set by the
physio protocol: the rulebot still blows on the standard cycle (the task
needs successful blows to progress; the game ignores pressure) and spends
energy on the guaranteed berry pick whenever it can.

Deterministic signals (sprite palettes in games/farm/gfx):
  energy bar : yellow fill (#ffe231/#ffe26a) inside the bar at x 26..182,
               y 145..166 (empty segments are blue, caps frame it)
  screen     : sky-blue background (overview) vs brown field
"""

from .. import extract as E
from .base import GameSpec, a_pressure, a_tap

# the fill frame of energybar.bmp is 26x21, drawn stretched to 156px wide;
# resampling makes the yellow area scale roughly linearly with fill width.
ENERGY_ZONE = (26, 145, 156, 21)
ENERGY_FULL_AREA = 1500  # yellow px at 100% (calibrate on first run)
YELL_LO = (200, 180, 0)
YELL_HI = (255, 240, 160)

BERRY_TAP = (55, 115)    # zone (0, 96, 111, 38)
FISH_TAP = (248, 162)    # zone (175, 145, 146, 35)
FIELD_TAP = (85, 87)     # zone (0, 79, 170, 17)
BACK_TAP = (298, 200)    # zone (278, 190, 40, 225)


class FarmSpec(GameSpec):
    name = "farm"

    # ------------------------------------------------------------------
    def extract(self, img, meta, ctx) -> dict:
        import numpy as np

        bars = E.Extractor(img, meta).bars()
        x, y, w, h = ENERGY_ZONE
        zone = img[y:y + h, x:x + w].astype(np.int16)
        lo = np.array(YELL_LO, dtype=np.int16)
        hi = np.array(YELL_HI, dtype=np.int16)
        yellow = int((np.all((zone >= lo) & (zone <= hi), axis=2)).sum())
        energy = min(1.15, yellow / ENERGY_FULL_AREA)

        r, g, b = (int(v) for v in img[10, 10])
        screen = "overview" if b > r + 30 else "field"

        return {
            "bars": bars,
            "energy": round(energy, 3),
            "screen": screen,
        }

    # ------------------------------------------------------------------
    def rulebot(self, state, ctx) -> list:
        actions = self._blow_cycle(ctx)
        if state["screen"] == "overview" and state["energy"] >= 0.22:
            actions.append(a_tap(*BERRY_TAP, 0.4))
        elif state["screen"] == "field":
            actions.append(a_tap(*BACK_TAP, 0.4))
        return actions

    def _blow_cycle(self, ctx) -> list:
        """Same standard rhythm as the miner — the protocol needs the blows
        even though the farm game itself ignores pressure."""
        now = ctx["now"]
        task = ctx.get("task", {})
        target_s = task.get("time_s", 5.0)
        if ctx.get("blowing"):
            if (now - (ctx.get("blow_start") or 0.0)) > target_s + 1.2:
                ctx["blowing"] = False
                ctx["blow_end"] = now
                return [a_pressure(0)]
            return []
        if (now - (ctx.get("blow_end") or 0.0)) >= 1.5:
            ctx["blowing"] = True
            ctx["blow_start"] = now
            return [a_pressure(100.0)]
        return []

    # ------------------------------------------------------------------
    def llm_questions(self, state, ctx) -> dict:
        return {
            "action": {
                "type": "choice",
                "instructions": (
                    "What should the farmer do next on this overview screen? "
                    "Each pick costs 0.2 energy (energy is drawn in the bar)."
                ),
                "criteria": {
                    "berry": "Tap the berry bush (bottom left) — guaranteed +10",
                    "fish": "Tap the fishing spot (bottom right) — random 0/5/12/20/30",
                    "field": "Enter the field (center strip) — plow, water and seed crops",
                    "wait": "Do nothing yet (e.g. not enough energy, or just wait for it to regen)",
                },
            },
            "screen": {
                "type": "choice",
                "instructions": "Which screen is shown?",
                "criteria": {
                    "overview": "Farm overview with berry bush, fishing spot and field",
                    "field": "A grid of farm plots with tool icons on the right",
                },
            },
        }

    def llm_actions(self, answers, state, ctx) -> list:
        actions = self._blow_cycle(ctx)
        if state["screen"] == "field":
            actions.append(a_tap(*BACK_TAP, 0.4))
            return actions
        a = answers.get("answers", {}).get("action", {})
        choice = a.get("choice")
        if choice == "berry" and state["energy"] >= 0.22:
            actions.append(a_tap(*BERRY_TAP, 0.4))
        elif choice == "fish" and state["energy"] >= 0.22:
            actions.append(a_tap(*FISH_TAP, 0.4))
        elif choice == "field":
            actions.append(a_tap(*FIELD_TAP, 0.4))
        return actions
