"""FrameSaver — persist gameplay frames for post-run visual verification.

Saves frames as PNG + one JSONL metadata record per frame under
/tmp/opencode/pepit_frames/<run_id>/, and writes a labeled contact sheet
(montage) at the end of the run. This gives an external agent (or a
post-run audit) the visual ground truth: what the game actually showed,
when, in which system state / task / decision context.

Two capture rates:
  - periodic frames, throttled to `interval` seconds (steady gameplay)
  - event frames (button drawn, rotate entered, win/loss, error) are
    always saved regardless of the throttle
"""

import json
import os
import time

import numpy as np

from PIL import Image, ImageDraw


class FrameSaver:
    def __init__(self, run_id: str, root: str = "/tmp/opencode/pepit_frames",
                 interval: float = 1.0, max_frames: int = 4000):
        self.dir = os.path.join(root, run_id)
        os.makedirs(self.dir, exist_ok=True)
        self.meta_path = os.path.join(self.dir, "frames.jsonl")
        self.sheet_path = os.path.join(self.dir, "contact_sheet.png")
        self.interval = interval
        self.max_frames = max_frames
        self.count = 0
        self._last = -1e9
        self._f = open(self.meta_path, "a")
        self._paths = []      # (frame_path, label) pairs for the contact sheet

    def save(self, img: np.ndarray, meta: dict, ctx: dict | None = None,
             force: bool = False) -> str | None:
        """Save one frame (PIL-free; img is an HxWx3 RGB ndarray).

        Returns the saved frame path, or None if throttled away.
        """
        now = time.monotonic()
        if not force and now - self._last < self.interval:
            return None
        if self.count >= self.max_frames:
            return None
        self._last = now
        self.count += 1
        name = f"{self.count:05d}.png"
        path = os.path.join(self.dir, name)
        try:
            Image.fromarray(img).save(path)
        except Exception:
            return None
        rec = {
            "frame": name,
            "ts_wall": round(time.time(), 3),
            "systemState": meta.get("systemState"),
            "device_ts": meta.get("timestamp"),
        }
        if ctx:
            rec.update(ctx)
        self._f.write(json.dumps(rec, default=str) + "\n")
        self._f.flush()
        label = self._label(rec)
        self._paths.append((path, label))
        return path

    @staticmethod
    def _label(rec: dict) -> str:
        parts = [f"t={rec.get('ctx_now', 0):.1f}s" if "ctx_now" in rec else ""]
        if rec.get("phase"):
            parts.append(rec["phase"])
        if rec.get("task_type"):
            parts.append(rec["task_type"])
        if rec.get("reps_done") is not None:
            parts.append(f"reps {rec['reps_done']}")
        if rec.get("blowing"):
            parts.append("blow")
        if rec.get("reason"):
            parts.append(rec["reason"])
        parts.append(f"st={rec.get('systemState')}")
        return " ".join(p for p in parts if p)

    def close(self) -> dict:
        """Flush metadata and write the contact sheet. Safe to call twice."""
        if getattr(self, "_closed", False):
            return self.summary()
        self._closed = True
        try:
            self._f.close()
        except Exception:
            pass
        self._write_contact_sheet()
        return self.summary()

    def _write_contact_sheet(self) -> None:
        if not self._paths:
            return
        # sample at most 64 frames, evenly, across the run
        paths = self._paths
        if len(paths) > 64:
            step = len(paths) / 64
            paths = [paths[int(i * step)] for i in range(64)]
        cols = 8
        rows = (len(paths) + cols - 1) // cols
        cell_w, cell_h = 320, 240
        label_h = 12
        scale = 1
        sheet = Image.new("RGB", (cols * cell_w, rows * (cell_h + label_h)), (32, 32, 32))
        draw = ImageDraw.Draw(sheet)
        try:
            from PIL import ImageFont
            font = ImageFont.load_default()
        except Exception:
            font = None
        for i, (path, label) in enumerate(paths):
            r, c = divmod(i, cols)
            try:
                tile = Image.open(path).convert("RGB")
            except Exception:
                continue
            if tile.size != (cell_w, cell_h):
                tile = tile.resize((cell_w, cell_h))
            x, y = c * cell_w, r * (cell_h + label_h)
            sheet.paste(tile, (x, y))
            if font:
                draw.text((x + 2, y + cell_h + 1), label[:60], fill=(220, 220, 220), font=font)
        sheet.save(self.sheet_path)

    def summary(self) -> dict:
        return {
            "dir": self.dir,
            "saved": self.count,
            "contact_sheet": self.sheet_path if os.path.exists(self.sheet_path) else None,
            "meta": self.meta_path,
        }
