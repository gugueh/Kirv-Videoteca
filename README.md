# Kirv Videoteca

[Español](README.es.md) · **English**

MP4 video player and image viewer for the PS Vita, with a folder-based library, touch and button controls. It comes with a PC tool that prepares videos and sends them to the console.

## Why this look

The PS Vita had a visual identity of its own: glossy bubbles, blue gradients, translucent panels and the LiveArea. Many homebrew apps look like debug menus or borrow the style of other systems. Kirv Videoteca brings back the visual language of the console's heyday, with its own icons made to measure and inspired by that era, so it feels like part of the system rather than an add-on. The PC tool uses the same colours and icons.

> **Status:** v0.1, first public release. Video playback is in daily use on a real PS Vita; the image viewer, console pictures, file management and the PC tool were tested on the console.

Inspired by [Vita Media Player](https://github.com/SonicMastr/Vita-Media-Player) by SonicMastr. The application code was rewritten to fix the memory and path-handling bugs found while auditing the original, and it no longer uses plugins or prebuilt modules.

## Features

- **Video:** MP4 (H.264/AAC recommended), saved progress, resume, watched marks, autoplay next, external `.srt` subtitles, thumbnails captured when leaving a video.
- **Images:** PNG, JPEG and BMP (24/32-bit), including the console's own screenshots and photos (`ux0:picture`, read-only). The Images tab lists every picture newest first, with thumbnails. Zoom with X/square, pan with the d-pad or a finger, change picture with L/R, left/right, swipes or the screen edges.
- **Files:** tabs for All, Series, Movies, Images and Folders. Options → **New folder**, **Rename selected** and **Delete selected** (files and folders, with confirmation) using the system keyboard. Subtitles, thumbnails and saved progress follow renamed or deleted videos.
- **Spanish or English** interface (Settings → Language). Progress is saved every 10 seconds and when pausing.

## Two parts

- **PS Vita app** (`src/`, `assets/`): the player itself.
- **PC tool** (`pc/`): drop videos, it converts only what the Vita needs (for example HE-AAC audio to AAC-LC, or HEVC/1080p video to H.264 720p) and sends them over VitaShell's FTP, into a new or an existing folder on the Vita (files already there are skipped). An option re-encodes large videos for the Vita's 960×544 screen: a 300 MB 1080p episode drops to about 130 MB with no visible loss. Windows build with ffmpeg included in each Release.

## Install and use

1. Install the `.vpk` with VitaShell (or similar). The bubble is named **Kirv Videoteca**.
2. Copy your files to `ux0:data/VMPH00001/video/`:

```text
ux0:data/VMPH00001/video/
  Series/
    My show/
      01 - Episode.mp4
      01 - Episode.srt
  Peliculas/
  Imagenes/
    Trip/
      photo.jpg
```

`Series/` and `Peliculas/` are optional: if present, the matching tab starts there. A `cover.png` inside a folder is used as its cover.

### Controls

| | Library | Video | Image |
|---|---|---|---|
| X | open | pause | zoom in |
| O | back | back | back |
| Left/Right | move | seek | previous/next (pan when zoomed) |
| L / R | switch tab | seek | previous/next |
| Up/Down | move | volume | pan when zoomed |
| Square | grid/list view | subtitles | zoom out |
| Triangle / Start | options | hide controls | hide bar |

## Limits

- Video: SceAvPlayer, up to 1280 × 736, mono/stereo 16-bit PCM audio.
- Images: JPEG up to 16384 px per side and 48 MB (decoded scaled down); PNG and BMP up to 8 megapixels, 4096 px per side and 12 MB.
- Console pictures are read-only. If the system does not let the app read them, the Images tab shows the library images and the error code.
- Paths under 512 bytes, names up to 255 bytes, 1,024 entries per folder.
- SRT: UTF-8, up to 4 MiB and 4,096 cues.
- Names: no `\ / : * ? " < > |`, no leading/trailing space or trailing dot. Deleting a folder deletes its contents after a confirmation that shows how many files it holds.

## Security

This variant uses a restricted SELF (`authid=0x2F00000000000002`), no taiHEN, no networking and no bundled SUPRX files. It only writes inside `ux0:data/VMPH00001/` (preferences, log, thumbnails next to the videos, and the folders and files you create, rename or delete there). `tools/verify_vpk.py` rejects packages with other permissions or unexpected files; it is a structural check, not a full audit.

## Build

Requirements: [VitaSDK](https://vitasdk.org/) (with libvita2d, libpng, libjpeg-turbo and zlib), CMake and Ninja.

```powershell
tools/build.ps1 -VitaSdk C:\vitasdk
python tools/verify_vpk.py <path-to-vpk>
```

If the project path contains spaces, copy the repository to a path without spaces: `vita-pack-vpk` fails with them.

### PC tool

```powershell
cd pc
python -m pip install pywebview
python -m kirv_pc                                     # run from source (needs ffmpeg on PATH)
./build.ps1 -Ffmpeg C:/ffmpeg-essentials             # Windows build with ffmpeg bundled
python -m unittest discover -s tests
```

### Core tests (no VitaSDK needed)

```bash
cmake -S . -B build -G Ninja -DVMP_HOST_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

They cover paths, SRT, saved state, allocation failures, image detection and headers, folder-name validation and UTF-16 conversion.

## Layout

```text
src/       app (main, browser, UI, viewer) and portable core (core, library)
include/   headers
assets/    icons, backgrounds, LiveArea
tests/     core and package tests
tools/     build.ps1 and verify_vpk.py
pc/        PC tool: convert and send videos to the Vita
licenses/  third-party notices
```

## Credits

- [Vita Media Player](https://github.com/SonicMastr/Vita-Media-Player) by SonicMastr: idea and starting point.
- [libvita2d](https://github.com/xerpi/libvita2d) by xerpi (MIT), and [VitaSDK](https://vitasdk.org/).
- Author, application code and interface design: gugueh / Kirv Studio. The UI is my own design, inspired by the look of the PS Vita era.

© 2025–2026 gugueh (Kirv Studio). All rights reserved. See [LICENSE](LICENSE).
