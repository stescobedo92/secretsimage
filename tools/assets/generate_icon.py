#!/usr/bin/env python3
"""Generate the secretsimage app icon (PNG / ICO / ICNS) deterministically.

Concept: a screenshot window (mountains + sun) crossed by a solid black
redaction bar - exactly what the tool does. Drawn at 4x and downscaled so
the small sizes (16-48 px) stay crisp. The editable vector source is
assets/icon.svg; this script is the rasterizer of record for the binaries
embedded in the app, so icon regeneration needs only Pillow:

    python tools/assets/generate_icon.py
"""
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
ASSETS = ROOT / "assets"
S = 4  # supersampling factor
SIZE = 512 * S

NAVY_TOP = (17, 32, 54)
NAVY_BOTTOM = (7, 13, 26)
FRAME = (226, 238, 248)
TEAL = (20, 184, 166)
TEAL_DARK = (13, 120, 110)
SUN = (245, 197, 66)
BAR = (8, 8, 10)
BAR_EDGE = (220, 38, 38)


def draw_icon(size: int) -> Image.Image:
    s = size / 512.0
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    def xy(*vals):
        return [v * s for v in vals]

    # Rounded-square background with a vertical gradient.
    grad = Image.new("RGBA", (size, size))
    gd = ImageDraw.Draw(grad)
    for y in range(size):
        t = y / max(size - 1, 1)
        gd.line([(0, y), (size, y)], fill=tuple(
            int(NAVY_TOP[i] + (NAVY_BOTTOM[i] - NAVY_TOP[i]) * t) for i in range(3)) + (255,))
    mask = Image.new("L", (size, size), 0)
    md = ImageDraw.Draw(mask)
    md.rounded_rectangle(xy(16, 16, 496, 496), radius=108 * s, fill=255)
    img.paste(grad, (0, 0), mask)
    d = ImageDraw.Draw(img)

    # Screenshot window.
    d.rounded_rectangle(xy(96, 110, 416, 380), radius=26 * s, outline=FRAME, width=int(14 * s))
    d.line(xy(96, 176, 416, 176), fill=FRAME, width=int(10 * s))
    for i, color in enumerate([(220, 38, 38), (245, 197, 66), (34, 197, 94)]):
        cx = 132 + i * 42
        d.ellipse(xy(cx - 11, 132, cx + 11, 154), fill=color)
    # Sun + mountains inside the window.
    d.ellipse(xy(330, 205, 382, 257), fill=SUN)
    d.polygon([tuple(xy(112, 364)), tuple(xy(215, 240)), tuple(xy(300, 364))], fill=TEAL_DARK)
    d.polygon([tuple(xy(235, 364)), tuple(xy(325, 262)), tuple(xy(400, 364))], fill=TEAL)

    # Solid redaction bar (slightly rotated) across the lower window.
    bar = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    bd = ImageDraw.Draw(bar)
    bd.rounded_rectangle(xy(70, 292, 442, 356), radius=14 * s, fill=BAR + (255,),
                         outline=BAR_EDGE + (255,), width=int(6 * s))
    bar = bar.rotate(-7, resample=Image.BICUBIC, center=(size / 2, size / 2))
    img.alpha_composite(bar)
    return img


def main() -> None:
    ASSETS.mkdir(exist_ok=True)
    master = draw_icon(SIZE)
    png512 = master.resize((512, 512), Image.LANCZOS)
    png512.save(ASSETS / "icon.png")
    master.resize((256, 256), Image.LANCZOS).save(ASSETS / "icon-256.png")

    ico_sizes = [(256, 256), (128, 128), (64, 64), (48, 48), (32, 32), (24, 24), (16, 16)]
    png512.save(ASSETS / "secretsimage.ico", format="ICO", sizes=ico_sizes)

    try:
        png512.save(ASSETS / "secretsimage.icns", format="ICNS")
        icns = "secretsimage.icns "
    except Exception as exc:  # Pillow without ICNS write support
        icns = f"(icns skipped: {exc}) "
    print(f"wrote icon.png icon-256.png secretsimage.ico {icns}to {ASSETS}")


if __name__ == "__main__":
    main()
