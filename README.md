# New3DS FBI

**New3DS FBI is built on [FBI by Steveice10](https://github.com/Steveice10/FBI).**  
Steveice10’s work is the foundation of this app — please don’t forget that. This repository keeps FBI’s original `LICENSE.txt` and copyright notice intact.

New3DS FBI is a **New 2DS / New 3DS (XL) only** fork of FBI: a homebrew title manager with a refreshed UI, safer installs, and a **100% local** “Update my ports” screen for SoulSilver / Platinum CIAs you build yourself.

| | |
|---|---|
| Title ID | `000400000F83FB00` (UniqueId `0xF83FB`) |
| Product code | `CTR-P-N3FB` |
| Outputs | `New3DS-FBI.cia`, `3ds/New3DS-FBI/New3DS-FBI.3dsx` |

It installs **alongside** original FBI (different UniqueId), not over it.

## Requirements

- New 2DS XL or New 3DS (XL) — **old 3DS / 2DS are refused at launch**
- Custom firmware capable of running homebrew (e.g. Luma3DS)
- For builds: [devkitPro](https://devkitpro.org/) `3ds-dev` + `3ds-curl`, `3ds-zlib`, `3ds-jansson`, citro2d/citro3d

## Features

- Browse SD / NAND / TWL photo & sound, titles, tickets, pending titles, save data
- Install CIAs from the file browser, batch install from `sdmc:/cias/`, remote/URL/QR (with allowlists)
- **Update my ports** — local overwrite-update for known SoulSilver / Platinum builds (see below)
- Modernized UI (citro2d progress bars, theme via `sdmc:/fbi/theme/` override)
- New 3DS hardware: 804 MHz, L2, extended memory; faster double-buffered CIA copies

- **Update this app** — quiet background check for new releases (badge only, never a popup); one-tap install of `New3DS-FBI.cia` for title `0xF83FB` only
- Kid-friendly main menu labels, top-screen tips, Advanced tools hidden until Settings enables them
- **Send files from your PC (FTP)** — built-in FTP server (Advanced), SD card only, big on-screen address


## Update this app (self-update)

On launch, if Wi-Fi works and **Settings → Check for app updates** is On (default), New3DS FBI quietly asks GitHub for the latest release. That never blocks startup, never shows a popup, and never makes a sound. If a newer version exists, a small **Update available (vX)** line appears on the main screen and on **Update this app**.

Open **Update this app** to read the changelog and install `New3DS-FBI.cia` in one step (overwrite of title `000400000F83FB00` only; your settings stay). Then restart the app. You can **Skip this version** or turn the check off in Settings / the update screen.


## Send files from your PC (FTP)

Under **Advanced… → Send files from your PC (FTP)** (Advanced must be enabled in Settings). Opens a single easy screen with a large `ftp://<3DS-IP>:5000` address, short steps for File Explorer / FileZilla, live status (waiting / connected / transfer progress), and **B = stop**.

- Never auto-starts; only runs while that screen is open
- Access is limited to **`sdmc:/`** (your SD card) — no NAND or system paths
- Optional FTP password in Settings (default: none). Use **only on home Wi-Fi**
- Optional keep-screen-awake (default On) so the console does not sleep mid-transfer

The FTP server is an original MIT-licensed implementation written for this fork (not GPLv3 `mtheall/ftpd`, which is license-incompatible with FBI’s MIT license).

## Settings & Advanced tools

Everyday items stay on the main menu. Risky tools (CTR/TWL NAND, Dump NAND, System Save Data, …) live under **Advanced…**, which only appears when **Settings → Show advanced tools** is On (default Off). Turning it on shows a plain-language warning once.

## Safety / no-brick rules

- CIA installs use **`AM_StartCiaInstallOverwrite`** — update in place, **no `AM_DeleteTitle`**, save data preserved; cancel/failure rolls back
- Remote / URL / batch / port paths allow only SD app / update / DLC title types (`00040000` / `0004000E` / `0004008C`) targeting SD — **never** system / NAND / firmware (`AM_InstallFirm` unused)
- CIA headers are validated before install; unsafe titles are refused with a clear dialog
- Dump NAND and other power-user tools remain available; use them carefully

## Update my ports (local only)

Pokemon 3DS Builder (`unowncollectables/patch-builder`) copies finished HOME Menu installers to the SD root **`cias/`** folder, e.g.:

- `sdmc:/cias/soulsilver3ds.cia`
- `sdmc:/cias/platinum3ds.cia`

New3DS FBI scans **`sdmc:/cias/`** first (and optionally `sdmc:/fbi/ports/`), matches by title ID only:

| Port | Title ID |
|------|----------|
| SoulSilver | `000400000F7D4000` |
| Platinum | `000400000F7D3D00` |

Version display uses the CIA title version and, when present, SMDH publisher text from the builder (e.g. SoulSilver `SS3DS_VERSION` / “run N”). One-tap update overwrite-installs; **nothing is downloaded**, no GitHub/network for ports, no ROM/CIA hosting in this app.

## Build

```sh
export DEVKITPRO=/opt/devkitpro
export DEVKITARM=/opt/devkitpro/devkitARM
make clean && make -j$(nproc)
```

Artifacts:

- `output/3ds-arm/New3DS-FBI.cia`
- `output/3ds-arm/3ds/New3DS-FBI/New3DS-FBI.3dsx`

Install the CIA with any title manager, or run the `.3dsx` from the Homebrew Launcher.

## Legal

- **No ROMs, encryption keys, or piracy aids** are included or distributed here.
- SoulSilver / Platinum **ports are built locally by you** from your own dumps via your own tools; this app only installs CIAs you already placed on the SD card.
- Do not ask this project to host, fetch, or redistribute copyrighted game data.

## Credits

- **Steveice10** — [FBI](https://github.com/Steveice10/FBI), the original open-source 3DS title manager. New3DS FBI exists because of that work.
- Banner/logo credits from upstream FBI (OctopusRift, Apache Thunder, PabloMK7) where those assets remain; this fork’s HOME Menu icon/banner chrome is original for New3DS FBI.
- SPI / TWL save protocol information: [TuxSH](https://github.com/TuxSH/) (TWLSaveTool)
- Fork maintenance: unowncollectables

## License

See [`LICENSE.txt`](LICENSE.txt) — copyright **(C) 2015–2019 Steveice10**. That notice must remain in all copies. Additional New3DS FBI changes are provided under the same terms unless noted otherwise.
