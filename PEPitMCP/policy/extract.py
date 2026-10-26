"""Deterministic state extraction from 320x240 frames (numpy, no vision model).

Two layers:

  firmware chrome — drawn by physioProtocolHandler.cpp around every blow
  task, identical across all games:
    pressure bar : white border at (1,190) 180x10, fill = % of target
                   (color: >90 green, 75-90 yellow, 55-75 orange, else red)
    duration bar : white border at (1,215) 180x10, fill = blow progress
  game content   — per-game extractors in games/*.py (sprites, zones, bars)

The extractor only ever reports what the pixels show; policy decisions
built on it are auditable and reproducible.
"""

import numpy as np

W, H = 320, 240

# Firmware chrome (constants.h: PRESSURE_BAR_X=1, Y=190, W=180, H=10;
# the duration bar is drawn at Y+25=215, same size, greenOffset=0)
PRESSURE_BAR = (1, 190, 180, 10)
DURATION_BAR = (1, 215, 180, 10)
ROTATE_OK_ZONE = (230, 170, 80, 60)  # OK button of the between-task rotate screen

# TFT 565-quantized reference colors (tolerance absorbs the 565 rounding)
TFT_WHITE = (255, 255, 255)
TFT_GREEN = (0, 255, 0)
TFT_YELLOW = (255, 255, 0)
TFT_ORANGE = (255, 165, 0)
TFT_RED = (255, 0, 0)
TFT_BLACK = (0, 0, 0)
TFT_BLUE = (0, 0, 255)

FILL_COLORS = {
    "green": TFT_GREEN,
    "yellow": TFT_YELLOW,
    "orange": TFT_ORANGE,
    "red": TFT_RED,
}


def color_mask(img: np.ndarray, color, tol: int = 24) -> np.ndarray:
    """Boolean mask of pixels within tol of `color` (RGB triple)."""
    target = np.array(color, dtype=np.int16)
    diff = np.abs(img.astype(np.int16) - target)
    return np.all(diff <= tol, axis=2)


def count_color(img: np.ndarray, color, tol: int = 24) -> int:
    return int(color_mask(img, color, tol).sum())


def region(img: np.ndarray, x: int, y: int, w: int, h: int) -> np.ndarray:
    return img[max(0, y):y + h, max(0, x):x + w]


def bar_state(img: np.ndarray, box, tol: int = 24) -> dict:
    """Describe a bordered progress bar.

    The firmware draws a white 1px rect (drawRect), then fills
    (x+1, y+1, fillWidth-2, h-2) in a status color.

    Presence is decided by the *border*: the top and bottom edge rows of
    the box must be mostly exact-white. (Counting white pixels anywhere in
    the frame is not a discriminator — the between-task "change image"
    screen is a bright full-frame photo whose pixels can pass a loose white
    test. Measured on a real change-screen frame: 0.0% of the bar-edge rows
    are near-white there, 100% on gameplay frames.)
    """
    x, y, w, h = box
    if img.shape[0] < y + h or img.shape[1] < x + w:
        return {"present": False, "fill": 0.0, "color": None}
    px = img.astype(np.int16)
    top = px[y, x:x + w]
    bottom = px[min(y + h - 1, img.shape[0] - 1), x:x + w]
    near_white = lambda row: float((np.abs(row - 255) <= 16).all(axis=1).mean())
    present = near_white(top) > 0.6 and near_white(bottom) > 0.6
    r = region(img, x + 1, y + 1, w - 2, h - 2)
    if r.size == 0:
        return {"present": present, "fill": 0.0, "color": None}
    interior = r.reshape(-1, 3).astype(np.int16)
    # "non-black" = any channel substantially bright (a pure red fill has
    # two dark channels, so a per-channel AND would reject it)
    nonblack = np.abs(interior).max(axis=1) > 40
    fill = float(nonblack.mean()) if len(nonblack) else 0.0
    color = None
    if nonblack.any():
        pix = interior[nonblack]
        best, bestd = None, None
        for name, ref in FILL_COLORS.items():
            d = np.abs(pix - np.array(ref, dtype=np.int16)).max(axis=1)
            frac = float((d <= tol).mean())
            if bestd is None or frac > bestd:
                best, bestd = name, frac
        if bestd > 0.25:
            color = best
    return {"present": present, "fill": round(fill, 3), "color": color}


# OK button of the between-task "change image" screen (displayPhysioRotateScreen):
# yellow 1px frame at (230,170,80,60), filled 0x18db (= 24-bit 24,24,222),
# "OK" text inside. No lua game draws that corner. The touch zone is active
# from the moment the screen appears, but the button is only *drawn* after
# 5 s — both signatures are handled by the runner.
OK_BUTTON_BOX = (230, 170, 80, 60)
OK_BUTTON_BLUE = np.array([24, 24, 222], dtype=np.int16)
OK_BUTTON_YELLOW = np.array([255, 255, 0], dtype=np.int16)


def ok_button(img: np.ndarray, tol: int = 40) -> bool:
    """True if the change-screen OK button is drawn in the bottom-right box."""
    x, y, w, h = OK_BUTTON_BOX
    if img.shape[0] < y + h or img.shape[1] < x + w:
        return False
    b = img[y:y + h, x:x + w].astype(np.int16)
    blue = float((np.abs(b - OK_BUTTON_BLUE) <= tol).all(axis=2).mean())
    yellow = float((np.abs(b - OK_BUTTON_YELLOW) <= tol).all(axis=2).mean())
    return blue > 0.25 and yellow > 0.03


def find_blobs(mask: np.ndarray, min_size: int = 8):
    """4-connected components of a boolean mask -> [(cx, cy, size), ...].

    Pure Python BFS — the frames are 320x240, this is plenty fast.
    """
    seen = np.zeros_like(mask, dtype=bool)
    out = []
    h, w = mask.shape
    for sy in range(h):
        row = mask[sy]
        seenrow = seen[sy]
        for sx in range(w):
            if not row[sx] or seenrow[sx]:
                continue
            stack = [(sx, sy)]
            seenrow[sx] = True
            total = sx = sy = 0
            while stack:
                cx, cy = stack.pop()
                total += 1
                sx += cx
                sy += cy
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    nx, ny = cx + dx, cy + dy
                    if 0 <= nx < w and 0 <= ny < h and mask[ny, nx] and not seen[ny, nx]:
                        seen[ny, nx] = True
                        stack.append((nx, ny))
            if total >= min_size:
                out.append((sx // total, sy // total, total))
    return out


def in_zone(x: int, y: int, zone) -> bool:
    zx, zy, zw, zh = zone
    return zx <= x < zx + zw and zy <= y < zy + zh


def rotate_screen_ok_zone():
    return ROTATE_OK_ZONE


class Extractor:
    """Firmware-chrome state (game-agnostic part)."""

    def __init__(self, img: np.ndarray, meta: dict):
        self.img = img
        self.meta = meta

    @classmethod
    def from_shot(cls, shot) -> "Extractor":
        img, meta = shot
        return cls(img, meta)

    def bars(self) -> dict:
        return {
            "pressure": bar_state(self.img, PRESSURE_BAR),
            "duration": bar_state(self.img, DURATION_BAR),
        }

    def ok_button(self) -> bool:
        """Change-screen OK button drawn? (strongest rotate-screen signal)"""
        return ok_button(self.img)

    def is_rotate_screen(self) -> bool:
        """Between-task 'change image' screen: no chrome bars.

        The OK button is the strong signal; the missing bars are the weak
        one (a bright photo could fake a bar in principle, the border test
        above keeps that probability negligible). The runner treats
        *button present* as definitive and *bars missing* as a trigger to
        watch for the button.
        """
        return self.ok_button() or not self.bars()["pressure"]["present"]

    def system_state(self) -> int:
        return int(self.meta.get("systemState", -1))


def img_to_png_b64(img: np.ndarray) -> str:
    """Frame -> base64 data URL for the LM Studio decision endpoint."""
    import base64
    import io

    from PIL import Image

    buf = io.BytesIO()
    Image.fromarray(img.astype(np.uint8)).save(buf, format="PNG")
    return "data:image/png;base64," + base64.b64encode(buf.getvalue()).decode()


def format_state(state: dict) -> str:
    """Render an extracted state dict as the text block for text-mode LLMs."""
    lines = []
    bars = state.get("bars", {})
    p = bars.get("pressure", {})
    d = bars.get("duration", {})
    lines.append(
        f"Pressure bar: {'off' if not p.get('present') else f'{100*p.get('fill',0):.0f}% ({p.get('color')})'}"
    )
    lines.append(
        f"Duration bar: {100*d.get('fill',0):.0f}%"
        + (f" ({d.get('color')})" if d.get("color") else "")
    )
    for k, v in state.items():
        if k in ("bars",):
            continue
        if isinstance(v, dict):
            lines.append(f"{k}: " + ", ".join(f"{a}={b}" for a, b in v.items()))
        elif isinstance(v, (list, tuple)):
            lines.append(f"{k}: {v}")
        else:
            lines.append(f"{k}: {v}")
    return "\n".join(lines)
