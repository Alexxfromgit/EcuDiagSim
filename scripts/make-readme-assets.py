"""Compose documentation artwork from real app captures (requires Pillow).

Run after refreshing docs/assets/vehicle-exterior.png. Source screenshot pixels
are only cropped/resized; no vehicle geometry or interface is synthesized.
"""

from pathlib import Path
import argparse
import os

from PIL import Image, ImageDraw, ImageFont, ImageOps


ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "docs" / "assets"


def font(size, bold=False):
    fonts = Path(os.environ.get("WINDIR", "C:/Windows")) / "Fonts"
    candidates = [
        fonts / ("segoeuib.ttf" if bold else "segoeui.ttf"),
        Path("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf" if bold else
             "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"),
        Path("/System/Library/Fonts/Supplemental/Arial Bold.ttf" if bold else
             "/System/Library/Fonts/Supplemental/Arial.ttf"),
    ]
    for candidate in candidates:
        if candidate.exists():
            return ImageFont.truetype(str(candidate), size)
    raise RuntimeError("Install Segoe UI, DejaVu Sans or Arial to render the artwork.")


def compose(source, output, size):
    width, height = size
    scale = width / 1920

    def px(value):
        return round(value * scale)

    canvas = Image.new("RGB", size, "#0b121a")
    draw = ImageDraw.Draw(canvas)
    # Subtle studio backdrop and a fine border keep the screenshot dominant.
    for y in range(height):
        t = y / height
        draw.line((0, y, width, y), fill=(11 + round(3*t), 18 + round(6*t), 26 + round(9*t)))

    def text(x, y, value, size=24, color="#e9f1f7", bold=False):
        draw.text((px(x), px(y)), value, font=font(px(size), bold), fill=color)

    text(64, 54, "ECU DIAGNOSTICS SIMULATOR", 23, "#88a5b9", True)
    draw.rounded_rectangle((px(64), px(120), px(384), px(164)), radius=px(8),
                           fill="#123236", outline="#235456", width=px(1))
    text(83, 126, "INTERACTIVE 3D  /  OFFLINE", 19, "#76e0d6", True)
    text(59, 203, "Explore the car.", 66, bold=True)
    text(59, 281, "Trace the fault.", 66, "#76e0d6", True)
    text(64, 391, "A virtual vehicle for learning", 28, "#a4b9c9")
    text(64, 433, "automotive electronics and diagnostics.", 28, "#a4b9c9")
    text(64, 528, "C++20  /  Qt 6  /  Three.js", 21, "#7995a9")

    # Source is the real native vehicle workspace, retained in full in the gallery.
    # This normalized crop isolates its stage, excluding the app frame/inspector.
    w, h = source.size
    stage = source.crop((round(w*.13), round(h*.30), round(w*.69), round(h*.81)))
    box = (px(760), px(96), px(1856), px(631))
    stage = ImageOps.fit(stage, (box[2]-box[0], box[3]-box[1]), Image.Resampling.LANCZOS)
    canvas.paste(stage, box[:2])
    draw.rounded_rectangle(box, radius=px(8), outline="#2b424f", width=max(1, px(2)))
    text(782, 655, "ACTUAL APP CAPTURE  ·  3D EXTERIOR VIEW", 17, "#88a5b9")
    draw.line((px(64), px(718), px(1856), px(718)), fill="#293b49", width=max(1,px(1)))
    text(64, 748, "CAN 500 kbit/s", 23, "#d7e5ef", True)
    text(395, 748, "OBD-II + UDS", 23, "#d7e5ef", True)
    text(735, 748, "32 guided lessons", 23, "#d7e5ef", True)
    text(64, 813, "Sedan: Fishboe / ministephen · adapted via sevnworks · CC BY 4.0", 17, "#718c9e")
    canvas.save(output, optimize=True)
    print(f"Created {output.relative_to(ROOT)} ({width}×{height})")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=ASSETS / "vehicle-exterior.png")
    args = parser.parse_args()
    with Image.open(args.source) as source:
        source = source.convert("RGB")
        compose(source, ASSETS / "hero.png", (1920, 880))
        compose(source, ASSETS / "social-preview.png", (1280, 640))


if __name__ == "__main__":
    main()
