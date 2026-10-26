"""FastVerifier — in-loop visual cross-check with the decision model.

Two-tier verification, fast tier: while the policy loop plays, the
FastVerifier periodically asks the decision model (default
cloudflare_clef-flash, ~1.7 s/call) the two most robust frame-level
questions (gameplay UI present? change screen?) and compares the answers
against what the runner's own state machine *expects* for the current
context. A disagreement is recorded (never aborts the run) so the slow
tier — the full agent auditing the saved frames after the run — knows
exactly which moments to look at.

Rate limiting: periodic checks at most every `interval` seconds of
gameplay, plus forced checks on event frames (change-screen entry,
button drawn, win/loss) where the expectation is sharpest.
"""

import json
import os
import time

from .llm import LLMClient

# Validated against saved frames (vision spike): both questions answer
# correctly with >0.95 confidence at 1.6-1.8 s latency.
DEFAULT_QUESTIONS = {
    "has_game_ui": {
        "type": "choice",
        "instructions": ("Is the PEPit gameplay status bar (colored bar in the "
                         "bottom area of the screen) visible in this frame?"),
        "criteria": {
            "yes": "at least one colored status bar is visible",
            "no": "no status bar is visible",
        },
    },
    "is_change_screen": {
        "type": "choice",
        "instructions": ("Does this frame show a static change/information screen "
                         "(a photo of medical equipment, no game scene)?"),
        "criteria": {
            "yes": "a static equipment photo or information screen",
            "no": "an active game scene is visible",
        },
    },
}


class FastVerifier:
    def __init__(self, client: LLMClient | None, run_id: str,
                 frames_dir: str | None = None,
                 interval: float = 5.0, enabled: bool = True):
        self.client = client
        self.enabled = enabled and client is not None
        self.questions = dict(DEFAULT_QUESTIONS)
        self.interval = interval
        self._last_check = -1e9
        self.checks = 0
        self.mismatches = 0
        self.last = None
        self._path = os.path.join(frames_dir, "verify.jsonl") if frames_dir else None
        self._f = open(self._path, "a") if self._path else None

    def _expect(self, ctx: dict) -> dict:
        """What the runner's own state says this frame should contain."""
        exp = {}
        if ctx.get("in_rotate"):
            exp["has_game_ui"] = "no"
            exp["is_change_screen"] = "yes"
        elif ctx.get("bars_present"):
            exp["has_game_ui"] = "yes"
            exp["is_change_screen"] = "no"
        return exp

    def maybe(self, img, meta: dict, ctx: dict, reason: str = "loop") -> dict | None:
        """One verification step. Returns the record, or None if skipped.

        Event reasons (button/change-screen/win/ended) bypass the rate
        limit — those are the moments a wrong frame is most costly.
        """
        if not self.enabled or img is None:
            return None
        event = reason not in ("loop", None)
        now = time.monotonic()
        if not event and now - self._last_check < self.interval:
            return None
        self._last_check = now
        t0 = now
        data = self.client.decide(
            self.questions, state_text="", image=_png_b64(img))
        if "error" in data:
            rec = {"reason": reason, "error": data["error"]}
            self._record(rec, ctx, 0.0)
            return rec
        a = data.get("answers", {})
        got = {
            "has_game_ui": a.get("has_game_ui", {}).get("choice"),
            "is_change_screen": a.get("is_change_screen", {}).get("choice"),
        }
        exp = self._expect(ctx)
        ok = all(got.get(k) == v for k, v in exp.items()) if exp else None
        self.checks += 1
        if exp and not ok:
            self.mismatches += 1
        rec = {"reason": reason, "expected": exp, "got": got,
               "ok": ok, "dt": round(now - t0, 2)}
        self.last = rec
        self._record(rec, ctx, now)
        return rec

    def _record(self, rec: dict, ctx: dict, now: float) -> None:
        self.last = rec
        if self._f:
            rec = dict(rec)
            rec["ts_wall"] = round(time.time(), 3)
            rec["ctx_now"] = round(ctx.get("now", 0.0), 2)
            rec["phase"] = ctx.get("phase")
            task = ctx.get("task") or {}
            if task:
                rec["task_type"] = task.get("type")
                rec["reps_done"] = task.get("reps_done")
            self._f.write(json.dumps(rec, default=str) + "\n")
            self._f.flush()

    def close(self) -> dict:
        if self._f:
            try:
                self._f.close()
            except Exception:
                pass
            self._f = None
        return {"checks": self.checks, "mismatches": self.mismatches,
                "last": self.last}


def _png_b64(img) -> str:
    import base64
    import io
    from PIL import Image
    buf = io.BytesIO()
    Image.fromarray(img).save(buf, format="PNG")
    return "data:image/png;base64," + base64.b64encode(buf.getvalue()).decode()
