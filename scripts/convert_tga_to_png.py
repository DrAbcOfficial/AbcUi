#!/usr/bin/env python3
"""One-shot asset migration: abcui TGA -> PNG (lossless, verified).

Converts every *.tga under assets/abcui to *.png with PIL:
- Alpha semantics are preserved (RGBA stays RGBA, opaque art saves as RGB,
  which FreeImage's ConvertTo32Bits re-widens to alpha=255 exactly like the
  old TGA path did).
- Every PNG is re-opened and its pixels compared byte-exact against the TGA
  decode; a mismatch aborts the script without deleting anything.

The C++ loader (framework/ImageLoader) detects formats from file content, so
the filename swap is the only consumer-visible change; all references are
updated separately in the same commit.

Usage:  python scripts/convert_tga_to_png.py [--keep-tga]
"""

import argparse
import sys
from pathlib import Path

from PIL import Image

ASSET_ROOT = Path(__file__).resolve().parent.parent / "assets" / "abcui"


def normalize(image: Image.Image) -> Image.Image:
    """Canonical form used for both the save and the verification compare."""
    has_alpha = image.mode in ("RGBA", "LA", "PA") or (
        image.mode == "P" and "transparency" in image.info
    )
    return image.convert("RGBA" if has_alpha else "RGB")


def convert(tga_path: Path) -> tuple[int, int]:
    """Returns (tga bytes, png bytes); raises on any verification failure."""
    png_path = tga_path.with_suffix(".png")
    with Image.open(tga_path) as tga:
        normalized = normalize(tga)
        expected = normalized.tobytes()
        normalized.save(png_path, "PNG", optimize=True)

    with Image.open(png_path) as png:
        if normalize(png).tobytes() != expected:
            raise RuntimeError(f"pixel verification failed: {png_path}")

    tga_bytes = tga_path.stat().st_size
    png_bytes = png_path.stat().st_size
    return tga_bytes, png_bytes


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--keep-tga", action="store_true",
                        help="leave the source TGA files in place")
    args = parser.parse_args()

    tga_files = sorted(p for p in ASSET_ROOT.rglob("*.tga") if p.is_file())
    if not tga_files:
        print(f"no .tga files under {ASSET_ROOT}")
        return 0

    total_tga = total_png = 0
    failures = 0
    for tga_path in tga_files:
        try:
            tga_bytes, png_bytes = convert(tga_path)
        except Exception as exc:  # noqa: BLE001 - report every file, then fail
            print(f"FAIL {tga_path.relative_to(ASSET_ROOT)}: {exc}")
            failures += 1
            continue
        total_tga += tga_bytes
        total_png += png_bytes
        saved = tga_bytes - png_bytes
        print(f"{tga_path.relative_to(ASSET_ROOT)}: "
              f"{tga_bytes} -> {png_bytes} bytes ({saved:+d})")
        if not args.keep_tga:
            tga_path.unlink()

    saved_total = total_tga - total_png
    print(f"\n{len(tga_files) - failures}/{len(tga_files)} converted, "
          f"{total_tga} -> {total_png} bytes total ({saved_total:+d}, "
          f"{100.0 * saved_total / total_tga:.1f}% saved)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
