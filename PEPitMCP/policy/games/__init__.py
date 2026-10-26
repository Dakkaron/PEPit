"""Per-game specs for the policy runner.

Each GameSpec knows how to (a) extract game-relevant state from a frame,
(b) produce a deterministic rulebot policy, and (c) formulate/interpret
LLM decision questions. Games are the lua games on the SD card; the legacy
C games (race, monster) and the phasing-out horserace are intentionally
absent.
"""

from .base import (Action, GameSpec, a_joystick_btn, a_joystick_x, a_pressure,
                   a_tap, a_touch_down, a_touch_up)
from .miner import MinerSpec
from .plane import PlaneSpec
from .farm import FarmSpec

GAMES = {
    "miner": MinerSpec,
    "plane": PlaneSpec,
    "farm": FarmSpec,
}


def get_game(name: str) -> GameSpec:
    if name not in GAMES:
        raise KeyError(f"unknown game '{name}' (have: {sorted(GAMES)})")
    return GAMES[name]()


__all__ = [
    "GAMES", "get_game", "GameSpec", "Action",
    "a_pressure", "a_tap", "a_joystick_x", "a_joystick_btn",
    "a_touch_down", "a_touch_up",
]
