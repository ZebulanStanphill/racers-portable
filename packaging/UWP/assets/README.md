# Xbox (UWP) tile and splash images

## Credits

The LEGO Racers logo and cover art are the LEGO Group's. LEGO and the LEGO logo are
trademarks of the LEGO Group, which does not sponsor, authorize or endorse this project.

**Splash screen** (`SplashScreen.*`):

- The **RACERS logo** (the letters, the chequered flag and the trophy) is a **fan-made
  recreation** of the 1999 LEGO Racers logo, not the original artwork. It is
  [SteamGridDB logo 90792](https://www.steamgriddb.com/logo/90792), uploaded by
  [Besli](https://www.steamgriddb.com/profile/76561197970967510) on 6 August 2023,
  whose notes credit it: **"Logo redone by Eastwood2002."** Thank you, Eastwood2002.
  It is used as published, apart from its LEGO brick, which is replaced.
- The **LEGO brick** is the LEGO Group's own logo: `LEGO_logo.svg`, published by the LEGO
  Group at `assets.lego.com`, from
  [Wikimedia Commons](https://commons.wikimedia.org/wiki/File:LEGO_logo.svg).

The same recreation was also uploaded as
[SteamGridDB logo 135278](https://www.steamgriddb.com/logo/135278) (MegaCrashFan96,
25 March 2025), with a different brick and an added drop shadow. That version is not
used here.

**Tiles** (`Square150x150Logo.*`, `LargeTile.*`, `Wide310x150Logo.*`): the LEGO Racers
cover art, from a 600 dpi scan of the front of the US PlayStation cover (SLUS-00581) by
[PSX Data Center](https://psxdatacenter.com/games/U/L/SLUS-00581.html).

**Small tile, app list icon and store logo** (`Square71x71Logo.*`, `Square44x44Logo.*`,
`StoreLogo.*`): the game's own desktop icon.

Each generated image also carries its credits in its own metadata: the splash PNGs in
text chunks (`Author`, `Source`, `Copyright`, `Comment`), the tile JPEGs in their
comment.

## The images

| Files | Size (scale-100 / scale-200) | Content |
| --- | --- | --- |
| `Square150x150Logo.*.jpg` | 150 / 300 | Cover art |
| `LargeTile.*.jpg` | 310 / 620 | Cover art |
| `Wide310x150Logo.*.jpg` | 310x150 / 620x300 | Cover art, centred, with a blurred, darkened copy of it filling the sides |
| `SplashScreen.*.png` | 620x300 / 1240x600 | The LEGO Racers logo, 90% of the width, on a transparent canvas over the app's background colour |
| `Square71x71Logo.*.png`, `Square44x44Logo.*.png`, `StoreLogo.*.png` | | The game's desktop icon, which reads better at these sizes |

The packaging tools reject any image the manifest references that is larger than
204,800 bytes (error APPX3207), so `make_assets.py` checks every image it writes against
that limit.

The app's background colour, `#020272` (both `BackgroundColor`s in
`Package.appxmanifest`), is the median colour of the game's menu backdrop, a LEGO-stud
gradient from about `#020256` to `#020299`. It shows around the splash logo and behind
the transparent small tile and app list icon. The tiles don't overlay the app name,
since the art shows it.

## How they were made

`make_assets.py` regenerates every generated image above (it downloads the sources,
checks their SHA-256 and writes the images here). Its output matches the committed files
byte for byte with Pillow 11.3, NumPy 2.0, PyMuPDF 1.26.5 and pyoxipng 9.1.1:

```bash
python3 -m venv /tmp/assets-venv
/tmp/assets-venv/bin/pip install pillow numpy pymupdf pyoxipng
/tmp/assets-venv/bin/python packaging/UWP/assets/make_assets.py --sources /tmp/racers-sources
```

**Tiles.** The US cover, cropped to the square art right of the PlayStation band
(1195x1195, otherwise unedited, including its "Build, Customize, Race and Win!"
tagline), downscaled with Lanczos. They are saved as JPEG at quality 95 without chroma
subsampling, like the source scan: as PNGs, the large tile alone would be 548 KB.

**Splash.** The recreation (3268x944) is scaled to 90% of the splash width and centred.
Its LEGO brick is cleared, and the LEGO Group's SVG is rendered (with PyMuPDF) straight
at the brick's final size and position, so it stays exactly the official logo and as
sharp as the display allows. The PNG's colour channels are rounded to 7 bits (each value
moves by at most 1 of 255; transparency is untouched) and recompressed with oxipng: the
logo's smooth shading otherwise leaves the scale-200 splash just over the size limit.

Earlier versions cut the logo out of the cover scans themselves. They were replaced
because the scans couldn't give clean edges: on the US cover the logo sits on its own
purple drop shadow, which is close in colour to the logo's navy outline, and the
European cover's (SLES-01207) logo art differs slightly from the US cover's, so an
outline traced on one doesn't fit the other.
