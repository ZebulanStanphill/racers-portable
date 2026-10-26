#!/usr/bin/env python3
# Regenerates the Xbox (UWP) tile and splash images in this directory. See README.md
# for the sources, their credits and why each step exists.
#
# Writes Square150x150Logo, LargeTile and Wide310x150Logo as JPEG and SplashScreen as
# PNG (scale-100 and scale-200), each with its credits in its metadata. The small tile,
# app list icon and store logo are the game's desktop icon and are not touched.
#
# Usage: make_assets.py [--sources DIR] [--out DIR]
#   --sources  where the source images are cached (downloaded there if missing)
#   --out      where to write the images (default: this directory)
#
# Needs Pillow, PyMuPDF (which renders the LEGO logo SVG) and pyoxipng.

import argparse
import hashlib
import urllib.request
from pathlib import Path

import numpy as np
import oxipng
import pymupdf
from PIL import Image, ImageFilter
from PIL.PngImagePlugin import PngInfo

HERE = Path(__file__).resolve().parent

# (file name, URL, SHA-256)
SOURCES = {
    # The front of the US PlayStation cover (SLUS-00581), a 600 dpi scan from PSX Data
    # Center, 1413x1418. Its art matches the PC box's.
    "cover": (
        "SLUS-00581-F-ALL.jpg",
        "https://psxdatacenter.com/images/hires/U/L/SLUS-00581/SLUS-00581-F-ALL.jpg",
        "bceae23021971eb06ceeb94398760382446422a859e3ffa82bd80f266dcb3778",
    ),
    # A fan-made recreation of the LEGO Racers logo, 3268x944 with transparency:
    # SteamGridDB logo 90792, uploaded by Besli, whose notes say "Logo redone by
    # Eastwood2002". Only its RACERS part (letters, flag, trophy) is used.
    "logo": (
        "steamgriddb-logo-90792.png",
        "https://cdn2.steamgriddb.com/logo/7c793dbd27b2b9e1a0d808f46decb06f.png",
        "f5b609a1981da662520e1ed8cc1919bdb548103fbc3bbe81f1b9572ac43e533f",
    ),
    # The LEGO logo as published by the LEGO Group (assets.lego.com), from Wikimedia
    # Commons' File:LEGO_logo.svg.
    "lego": (
        "LEGO_logo.svg",
        "https://upload.wikimedia.org/wikipedia/commons/2/24/LEGO_logo.svg",
        "e692c130a90f4e348fc484cce8e13d43e5275fef4cec1a3878b8e3d96071a6e0",
    ),
}

# The cover's art right of the PlayStation band, as a square (x0, y0, x1, y1).
COVER_CROP = (212, 0, 1407, 1195)
# The recreation's LEGO brick: its left, top and size.
LOGO_BRICK = (24, 28, 884)
# The brick's square in the LEGO SVG's 180-unit view box (from, to).
SVG_SQUARE = (1.223, 178.82)
# The logo's width on the splash, as a fraction of the splash's width.
SPLASH_LOGO_WIDTH = 0.9
# The packaging tools reject any image the manifest references that is larger than this.
MAX_IMAGE_BYTES = 204800
# The tiles are photographic, so they are JPEG (as their source scan is): even the
# 620x620 large tile is far over MAX_IMAGE_BYTES as a PNG.
TILE_JPEG_QUALITY = 95

COVER_CREDIT = {
    "Title": "LEGO Racers cover art",
    "Author": "The LEGO Group (artwork); PSX Data Center (scan)",
    "Source": SOURCES["cover"][1],
    "Copyright": "LEGO Racers artwork (c) The LEGO Group. LEGO is a trademark of the LEGO Group.",
    "Comment": "Cropped from a scan of the US PlayStation cover (SLUS-00581). "
    "See packaging/UWP/assets/README.md in racers-portable.",
}
SPLASH_CREDIT = {
    "Title": "LEGO Racers logo",
    "Author": "Logo recreation: Eastwood2002 (SteamGridDB logo 90792, uploaded by Besli); "
    "LEGO logo: The LEGO Group",
    "Source": f"{SOURCES['logo'][1]} ; {SOURCES['lego'][1]}",
    "Copyright": "LEGO Racers logo (c) The LEGO Group. LEGO is a trademark of the LEGO Group.",
    "Comment": "The RACERS part is a fan-made recreation of the 1999 logo, credited on "
    "SteamGridDB as 'Logo redone by Eastwood2002'; the LEGO brick is the LEGO Group's own "
    "logo. See packaging/UWP/assets/README.md in racers-portable.",
}


def fetch(key, sources_dir):
    name, url, sha256 = SOURCES[key]
    path = sources_dir / name
    if not path.exists():
        sources_dir.mkdir(parents=True, exist_ok=True)
        request = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0 (racers-portable make_assets.py)"})
        with urllib.request.urlopen(request) as response:
            path.write_bytes(response.read())
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    if digest != sha256:
        raise SystemExit(f"{path}: unexpected SHA-256 {digest}; the source has changed")
    return path


def wide_tile(cover, w, h):
    """The cover centred, with a blurred, darkened copy of it filling the sides."""
    background = cover.resize((w, w), Image.LANCZOS).crop((0, (w - h) // 2, w, (w - h) // 2 + h))
    background = background.filter(ImageFilter.GaussianBlur(h / 20)).point(lambda value: int(value * 0.7))
    background.paste(cover.resize((h, h), Image.LANCZOS), ((w - h) // 2, 0))
    return background


def splash(logo, lego_svg, w, h):
    """The recreation, centred on a transparent canvas, with its LEGO brick replaced by
    the LEGO Group's logo, rendered straight at the brick's final size and position."""
    lw = round(w * SPLASH_LOGO_WIDTH)
    scale = lw / logo.width
    lh = round(logo.height * scale)
    ox = (w - lw) // 2
    oy = (h - lh) // 2
    canvas = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    canvas.alpha_composite(logo.resize((lw, lh), Image.LANCZOS), (ox, oy))

    left, top, size = (value * scale for value in LOGO_BRICK)
    left += ox
    top += oy
    # Clear the recreation's brick (with a pixel to spare for its anti-aliasing).
    canvas.paste((0, 0, 0, 0), (int(left) - 1, int(top) - 1, int(left + size) + 2, int(top + size) + 2))

    page = pymupdf.open(lego_svg)[0]
    zoom = size / (SVG_SQUARE[1] - SVG_SQUARE[0])
    x = left - SVG_SQUARE[0] * zoom
    y = top - SVG_SQUARE[0] * zoom
    # Render with the fractional part of the offset, then paste at the whole pixel.
    matrix = pymupdf.Matrix(zoom, 0, 0, zoom, x - int(x), y - int(y))
    pixmap = page.get_pixmap(matrix=matrix, alpha=True)
    brick = Image.frombytes("RGBA", (pixmap.width, pixmap.height), pixmap.samples)
    canvas.alpha_composite(brick, (int(x), int(y)))
    return canvas


def check_size(path):
    size = path.stat().st_size
    if size > MAX_IMAGE_BYTES:
        raise SystemExit(f"{path}: {size} bytes, over the {MAX_IMAGE_BYTES}-byte limit for manifest images")


def save_tile(image, path, credit):
    """JPEG, without chroma subsampling, with the credits in its comment."""
    comment = "\n".join(f"{key}: {value}" for key, value in credit.items())
    image.save(path, "JPEG", quality=TILE_JPEG_QUALITY, subsampling=0, optimize=True, comment=comment)
    check_size(path)


def save_splash(image, path, credit):
    """PNG with the credits in text chunks. The logo's smooth shading compresses poorly,
    so the colour channels are rounded to 7 bits (each value moves by at most 1 of
    255), the colour of fully transparent pixels is cleared, and oxipng recompresses it.
    That keeps the scale-200 splash well under MAX_IMAGE_BYTES."""
    pixels = np.array(image)
    rgb = pixels[..., :3]
    # Even below 128 and odd from 128 up, so 0 and 255 stay exact.
    pixels[..., :3] = (rgb >> 1 << 1) | (rgb >> 7)
    pixels[pixels[..., 3] == 0, :3] = 0
    info = PngInfo()
    for key, value in credit.items():
        info.add_text(key, value)
    Image.fromarray(pixels).save(path, pnginfo=info)
    oxipng.optimize(path, level=6, strip=oxipng.StripChunks.none())
    check_size(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sources", type=Path, default=HERE / ".sources")
    parser.add_argument("--out", type=Path, default=HERE)
    args = parser.parse_args()

    cover = Image.open(fetch("cover", args.sources)).convert("RGB").crop(COVER_CROP)
    logo = Image.open(fetch("logo", args.sources)).convert("RGBA")
    lego_svg = fetch("lego", args.sources)

    args.out.mkdir(parents=True, exist_ok=True)
    for factor, scale in ((1, 100), (2, 200)):
        tiles = {
            "Square150x150Logo": cover.resize((150 * factor, 150 * factor), Image.LANCZOS),
            "LargeTile": cover.resize((310 * factor, 310 * factor), Image.LANCZOS),
            "Wide310x150Logo": wide_tile(cover, 310 * factor, 150 * factor),
        }
        for name, image in tiles.items():
            save_tile(image, args.out / f"{name}.scale-{scale}.jpg", COVER_CREDIT)
        save_splash(splash(logo, lego_svg, 620 * factor, 300 * factor), args.out / f"SplashScreen.scale-{scale}.png",
                    SPLASH_CREDIT)


if __name__ == "__main__":
    main()
