#!/usr/bin/env python3
"""Draws the application icon and writes it in every format the installers need.

    python scripts/make-icons.py            (needs Pillow: pip install pillow)

Writes resources/icons/ecudiagsim.png (1024 px), .ico (Windows, 16-256 px) and .icns (macOS).
The motif: a rev counter with its needle in the red, and an amber check-engine lamp below,
on the dark dashboard colours of the app.
"""
import math
import pathlib

from PIL import Image, ImageDraw, ImageFilter

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "resources" / "icons"
SS = 4  # supersampling factor
SIZE = 1024

BG_TOP = (34, 40, 51)
BG_BOTTOM = (14, 17, 22)
RIM = (74, 82, 98)
FACE = (10, 12, 16)
TICK = (228, 232, 238)
RED = (255, 77, 94)
NEEDLE = (255, 90, 77)
ACCENT = (74, 168, 255)
AMBER = (245, 165, 36)


def draw(size: int) -> Image.Image:
    s = size * SS
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))

    # Rounded square with a vertical gradient (the macOS / Windows 11 style tile).
    grad = Image.new("RGBA", (s, s))
    gd = ImageDraw.Draw(grad)
    for y in range(s):
        t = y / (s - 1)
        gd.line([(0, y), (s, y)], fill=tuple(int(a + (b - a) * t) for a, b in zip(BG_TOP, BG_BOTTOM)) + (255,))
    mask = Image.new("L", (s, s), 0)
    margin = int(s * 0.06)
    ImageDraw.Draw(mask).rounded_rectangle([margin, margin, s - margin, s - margin], radius=int(s * 0.2), fill=255)
    img.paste(grad, (0, 0), mask)

    d = ImageDraw.Draw(img)
    cx, cy = s / 2, s * 0.53
    r = s * 0.33

    # Gauge face and rim.
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=FACE + (255,), outline=RIM + (255,), width=int(s * 0.014))

    start, sweep = 135.0, 270.0  # degrees clockwise from 3 o'clock, like the app's gauges
    # Red zone.
    rr = r * 0.86
    d.arc([cx - rr, cy - rr, cx + rr, cy + rr], start + sweep * 0.8, start + sweep, fill=RED + (255,),
          width=int(s * 0.03))
    # Ticks.
    for i in range(9):
        a = math.radians(start + sweep * i / 8)
        r1, r2 = r * 0.93, r * 0.74
        d.line([(cx + r1 * math.cos(a), cy + r1 * math.sin(a)), (cx + r2 * math.cos(a), cy + r2 * math.sin(a))],
               fill=TICK + (255,), width=int(s * 0.018))
    # Blue sweep up to the needle (what the engine is doing).
    ra = r * 0.62
    needle_at = start + sweep * 0.86
    d.arc([cx - ra, cy - ra, cx + ra, cy + ra], start, needle_at, fill=ACCENT + (255,), width=int(s * 0.022))

    # Needle with a soft glow.
    a = math.radians(needle_at)
    tip = (cx + r * 0.84 * math.cos(a), cy + r * 0.84 * math.sin(a))
    tail = (cx - r * 0.14 * math.cos(a), cy - r * 0.14 * math.sin(a))
    glow = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    ImageDraw.Draw(glow).line([tail, tip], fill=NEEDLE + (160,), width=int(s * 0.05))
    img.alpha_composite(glow.filter(ImageFilter.GaussianBlur(s * 0.015)))
    d.line([tail, tip], fill=NEEDLE + (255,), width=int(s * 0.026))
    hub = s * 0.045
    d.ellipse([cx - hub, cy - hub, cx + hub, cy + hub], fill=(32, 37, 46, 255), outline=RIM + (255,),
              width=int(s * 0.008))

    # Check-engine lamp: an amber pill under the gauge.
    lw, lh = s * 0.2, s * 0.075
    ly = cy + r * 0.52
    lamp = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    ImageDraw.Draw(lamp).rounded_rectangle([cx - lw / 2, ly - lh / 2, cx + lw / 2, ly + lh / 2], radius=lh / 2,
                                           fill=AMBER + (200,))
    img.alpha_composite(lamp.filter(ImageFilter.GaussianBlur(s * 0.012)))
    d.rounded_rectangle([cx - lw / 2, ly - lh / 2, cx + lw / 2, ly + lh / 2], radius=lh / 2, fill=AMBER + (255,))

    return img.resize((size, size), Image.LANCZOS)


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    big = draw(SIZE)
    big.save(OUT / "ecudiagsim.png")
    # Small sizes are drawn separately rather than scaled down, so lines stay crisp.
    sizes = [16, 24, 32, 48, 64, 128, 256]
    images = {n: draw(n) for n in sizes}
    images[256].save(OUT / "ecudiagsim.ico", sizes=[(n, n) for n in sizes],
                     append_images=[images[n] for n in sizes if n != 256])
    big.save(OUT / "ecudiagsim.icns")
    for f in sorted(OUT.iterdir()):
        print(f"{f.stat().st_size:>9,}  {f.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
