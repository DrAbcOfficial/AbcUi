#!/usr/bin/env python3
"""Generates the itemhighlight panel art (D-19, P7).

The old panel shipped one 64x64 TGA (abcenchance/tga/item_highlight.tga:
hand-cut corner brackets in 145-gray with a fully transparent middle, tinted
per item through SetDrawColor). The port draws the same concept as Rml
elements and this script regenerates the PNG trio deterministically:

- marker.png      corner brackets (145-gray, old draw-color parity), tinted
                  per item via image-color; 64x64.
- marker_aim.png  the aim-state variant (white, longer/thicker arms) the old
                  code did not have - the port highlights the item the pickup
                  prompt refers to; 64x64.
- pickup_blur.png soft dark plate standing in for the old GaussianBlurPanel
                  behind the pickup notice (the 2D HUD path has no backdrop
                  blur; see D-19); 512x74.

Everything renders at 4x and downsamples (anti-aliasing). Usage:
    python scripts/gen_itemhighlight_assets.py
"""

from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

OUT_DIR = Path(__file__).resolve().parent.parent / "assets" / "abcui" / "img" / "itemhighlight"

SS = 4  # supersampling factor


def corner_brackets(size: int, arm: int, thickness: int, color: tuple) -> Image.Image:
    """Four L-shaped corner brackets (RGBA), anti-aliased by supersampling."""
    canvas = Image.new("RGBA", (size * SS, size * SS), (0, 0, 0, 0))
    draw = ImageDraw.Draw(canvas)
    arm_px = arm * SS
    thick_px = thickness * SS
    edge = size * SS

    def bracket(x0: int, y0: int, x1: int, y1: int) -> None:
        draw.rectangle((x0, y0, x1, y1), fill=color)

    # Top-left
    bracket(0, 0, arm_px, thick_px - 1)
    bracket(0, 0, thick_px - 1, arm_px)
    # Top-right
    bracket(edge - arm_px, 0, edge - 1, thick_px - 1)
    bracket(edge - thick_px, 0, edge - 1, arm_px)
    # Bottom-left
    bracket(0, edge - thick_px, arm_px, edge - 1)
    bracket(0, edge - arm_px, thick_px - 1, edge - 1)
    # Bottom-right
    bracket(edge - arm_px, edge - thick_px, edge - 1, edge - 1)
    bracket(edge - thick_px, edge - arm_px, edge - 1, edge - 1)

    return canvas.resize((size, size), Image.LANCZOS)


def pickup_plate(width: int, height: int) -> Image.Image:
    """Soft dark plate: rounded rect inset, alpha-blurred edge, supersampled."""
    canvas = Image.new("RGBA", (width * SS, height * SS), (0, 0, 0, 0))
    draw = ImageDraw.Draw(canvas)
    margin = 10 * SS
    radius = (height * SS - 2 * margin) // 2
    draw.rounded_rectangle(
        (margin, margin, width * SS - margin - 1, height * SS - margin - 1),
        radius=radius,
        fill=(0, 0, 0, 185),
    )
    canvas = canvas.filter(ImageFilter.GaussianBlur(6 * SS))
    return canvas.resize((width, height), Image.LANCZOS)


def main() -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    # Old marker parity: 145-gray arms (the old TGA's baked gray), alpha 255;
    # per-item RGBA rides image-color on top.
    corner_brackets(64, arm=18, thickness=3, color=(145, 145, 145, 255)).save(
        OUT_DIR / "marker.png", "PNG", optimize=True)
    corner_brackets(64, arm=23, thickness=5, color=(255, 255, 255, 255)).save(
        OUT_DIR / "marker_aim.png", "PNG", optimize=True)
    pickup_plate(512, 74).save(OUT_DIR / "pickup_blur.png", "PNG", optimize=True)

    for name in ("marker.png", "marker_aim.png", "pickup_blur.png"):
        with Image.open(OUT_DIR / name) as image:
            print(name, image.size, image.mode, (OUT_DIR / name).stat().st_size, "bytes")


if __name__ == "__main__":
    main()
