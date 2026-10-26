"""Shared game-spec types (Action constructors + GameSpec interface)."""


class Action:
    """One control instruction the runner will apply.

    cmd:  pressure | touch_down | touch_move | touch_up | tap
          | joystick_x | joystick_btn
    args: depends on cmd, see ControlChannel
    """

    __slots__ = ("cmd", "args")

    def __init__(self, cmd: str, *args):
        self.cmd = cmd
        self.args = args

    def __repr__(self):
        return f"{self.cmd}{(' ' + ' '.join(map(str, self.args))) if self.args else ''}"


def a_pressure(pct: float) -> Action:
    return Action("pressure", pct)


def a_tap(x: int, y: int, hold: float = 0.4) -> Action:
    return Action("tap", x, y, hold)


def a_joystick_x(v: float) -> Action:
    return Action("joystick_x", v)


def a_joystick_btn(pressed: bool) -> Action:
    return Action("joystick_btn", pressed)


def a_touch_down(x: int, y: int) -> Action:
    return Action("touch_down", x, y)


def a_touch_up() -> Action:
    return Action("touch_up")


class GameSpec:
    """Interface every game implements (miner / plane / farm do)."""

    name = "game"

    def extract(self, img, meta, ctx) -> dict:
        raise NotImplementedError

    def rulebot(self, state, ctx) -> list:
        raise NotImplementedError

    def llm_questions(self, state, ctx) -> dict:
        raise NotImplementedError

    def llm_actions(self, answers, state, ctx) -> list:
        raise NotImplementedError

    def summary_line(self, state, ctx) -> str:
        """Short human-readable one-liner for status output."""
        bars = state.get("bars", {})
        p = bars.get("pressure", {})
        return (
            f"{self.name}: pressure={100 * p.get('fill', 0):.0f}% "
            f"duration={100 * bars.get('duration', {}).get('fill', 0):.0f}%"
        )
