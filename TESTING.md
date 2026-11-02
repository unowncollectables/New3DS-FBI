# Hardware testing — `fast-installs`

Stacked on `batch-cias`. Artifacts under `output/3ds-arm/`.

## What changed
- Double-buffered CIA/file copy: SD read on New 3DS core 2 overlaps AM/dst write
- Install buffer size raised to **2 MiB** for CIA / ticket / URL installs (peak ~4 MiB with two buffers)
- Sync fallback if the prefetch thread cannot be created
- Safety rules unchanged (allowlist, header check, overwrite+cancel, no DeleteTitle/InstallFirm)

## Setup
- [ ] Same as `batch-cias`: several homebrew app `.cia` files under `sdmc:/cias`
- [ ] Also keep one CIA reachable from Files for single Install CIA

## Speed / progress
- [ ] Install CIA (Files) shows progress, speed, ETA; completes without hang
- [ ] Batch Install all shows overall `N / total` plus current file speed/ETA
- [ ] Subjectively faster than `batch-cias` tip on large CIAs (multi‑MB+); no crash
- [ ] B cancels mid-install; AM rolls back; no partial corrupt title

## Correctness
- [ ] Installed titles launch; appear under Titles
- [ ] Batch summary still lists succeeded / failed / skipped
- [ ] Mix bad/truncated CIA → error for that file, batch continues

## Safety (no bricks) — required
- [ ] System/firmware CIA refused (allowlist); never NAND
- [ ] Header validated before AM install
- [ ] `AM_StartCiaInstallOverwrite` + cancel on failure; no `AM_DeleteTitle` / no `AM_InstallFirm`
- [ ] After tests, Home Menu / Settings still work
