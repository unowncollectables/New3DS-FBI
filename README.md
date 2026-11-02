<div align="center">

# New3DS FBI

**A modernized FBI title manager — New 2DS / New 3DS only.**

[![Latest release](https://img.shields.io/github/v/release/unowncollectables/New3DS-FBI?label=release&logo=github)](https://github.com/unowncollectables/New3DS-FBI/releases/latest)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE.txt)
[![Platform](https://img.shields.io/badge/platform-New%203DS-brightgreen.svg)](https://github.com/unowncollectables/New3DS-FBI)

<img src="meta/icon_3ds.png" alt="New3DS FBI icon" width="96" height="96">

</div>

## What it does

New3DS FBI is a New 2DS XL / New 3DS (XL) fork of [FBI by Steveice10](https://github.com/Steveice10/FBI). It keeps the familiar title-manager job — install CIAs, manage files and saves — with a clearer UI, safer confirms, and extras that use New 3DS hardware. It installs **alongside** original FBI (different UniqueId), not over it.

Old 3DS / 2DS models are refused at launch.

## Features

### Installing

- Install from a URL or a GitHub release asset
- Batch install every CIA in `sdmc:/cias/` with a progress display
- Faster installs on New 3DS (804 MHz + L2 via system speedup)

### My ports updater

- Updates known local port CIAs from `sdmc:/cias/` only
- **Nothing is downloaded** — no network, no GitHub for this path

### Saves

- Back up and restore game save data (user titles)
- Restore always asks Yes/No and makes a fresh safety backup first

### File manager

- Browse the SD card: copy, move, rename, delete, new folder
- Install a `.cia` only after an explicit confirm

### System info

- Read-only “About my 3DS” (model, firmware, region, battery, SD space, and similar)
- No serials, keys, or friend codes by default

### FTP

- Built-in FTP under **Advanced**
- SD card only
- Runs **only while its screen is open** (B stops it)

### Look

- Dark mode
- Accent colours: Blue, Green, Purple, Orange
- Easy-read UI with top-screen tooltips

### Safety

- Nothing installs automatically
- Every install needs a Yes/No confirm
- Risky tools stay hidden until **Settings → Show advanced tools** is On

## Install

1. Download **`New3DS-FBI.cia`** from [Releases](https://github.com/unowncollectables/New3DS-FBI/releases/latest).
2. Install it with FBI or another CIA installer.

It uses a different UniqueId from original FBI, so both can sit on the HOME Menu together.

## Building

Needs [devkitPro](https://devkitpro.org/) with **devkitARM**, **libctru**, **citro2d**, plus **makerom** and **bannertool** on `PATH`.

```sh
export DEVKITPRO=/opt/devkitpro
export DEVKITARM=/opt/devkitpro/devkitARM
make clean && make -j$(nproc)
```

Artifacts land under `output/` (e.g. `New3DS-FBI.cia` and the `.3dsx`).

## Credits

**Huge thanks to [Steveice10](https://github.com/Steveice10)** — New3DS FBI is built on [FBI](https://github.com/Steveice10/FBI). That project is the foundation; please give credit where it’s due.

Thanks also to the **devkitPro** and **libctru** contributors who make 3DS homebrew builds possible.

## License

MIT — see [`LICENSE.txt`](LICENSE.txt). The original copyright notice (**© 2015–2019 Steveice10**) is kept and must remain in all copies.

## No piracy

This project does **not** include, host, or point at ROMs, encryption keys, or piracy sources. Put only CIAs you are allowed to use on your own SD card.
