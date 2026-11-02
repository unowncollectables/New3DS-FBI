# Hardware testing — `batch-cias`

Stacked on `url-installs`. Artifacts under `output/3ds-arm/`.

## Setup
- [ ] Create `sdmc:/cias` (app also creates it) and copy several **homebrew app** `.cia` files

## Batch menu
- [ ] Main Menu → **Batch CIAs (/cias)** appears
- [ ] Empty `/cias` → clear “No .cia files” message
- [ ] With CIAs → shows count; options **Install all** / **Select which to install**

## Install all
- [ ] Choose Install all → asked “Delete after install?” → choose **No**
- [ ] Confirm install → overall `N / total` plus current filename, bytes, speed, ETA
- [ ] All safe CIAs install to SD; titles appear under Titles
- [ ] B cancels mid-batch; partial progress stops; summary still shown; no bricks

## Select subset
- [ ] Select which → A toggles (enabled/disabled colors); X starts install of enabled only
- [ ] Nothing selected + X → “No CIAs selected”

## Delete-after (optional)
- [ ] Default path with **No** leaves files in `/cias`
- [ ] With **Yes**, only **successfully** installed CIAs are deleted from `/cias`; failed ones remain

## Errors / summary
- [ ] Mix a renamed non-CIA or truncated file → per-file error, batch continues, summary lists failed count
- [ ] Summary shows succeeded / failed / skipped unsafe

## Safety (no bricks) — required
- [ ] System/firmware CIA in `/cias` is skipped or refused (allowlist); never installed to NAND
- [ ] Header validated via `cia_get_title_id` before AM install
- [ ] Uses `AM_StartCiaInstallOverwrite` + cancel rollback; no `AM_DeleteTitle` / no `AM_InstallFirm`
- [ ] After tests, Home Menu / Settings still work
