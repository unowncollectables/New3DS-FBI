# Hardware testing — `port-updater`

Stacked on `master` (`ui-refresh`). Artifacts under `output/3ds-arm/`.

## What changed
- Main Menu → **Update my ports**
- **100% local**: scans `sdmc:/fbi/ports/` only; never downloads; never contacts GitHub/network for ports
- Accepts only SoulSilver `000400000F7D4000` and Platinum `000400000F7D3D00`
- Shows installed version vs CIA version (SMDH name hint when present; else size fallback)
- One-tap update uses `AM_StartCiaInstallOverwrite` — **does not delete** the title first; save data preserved; cancel/failure rolls back
- Existing allowlist + CIA header validation still apply; no `AM_DeleteTitle` / no `AM_InstallFirm`

## Setup (local CIAs you built)
- [ ] Create `sdmc:/fbi/ports/` (app also creates it)
- [ ] Copy locally built SoulSilver and/or Platinum `.cia` files into that folder
- [ ] Do **not** expect the app to fetch or host anything

## Screen
- [ ] Main Menu shows **Update my ports**
- [ ] Empty / no matching titles → clear local-only instructions + title IDs
- [ ] Matching CIAs listed with `installed … -> CIA …` (and name hint when SMDH present)
- [ ] Colors: not installed (warning), different version (accent), same version (success)
- [ ] X refreshes installed-version labels after an update

## Update behavior (pt12u)
- [ ] Confirm text states overwrite install, **no delete**, saves preserved, rollback on failure
- [ ] A → Yes runs overwrite install with progress; B cancels mid-install safely
- [ ] After success, title launches; **save data still present**
- [ ] Wrong/other title CIA in `/fbi/ports/` is ignored by the filter (not listed)
- [ ] Truncated/invalid CIA refused with safety message; previous title untouched

## Safety
- [ ] No network activity from this screen (no GitHub, no URL installs, no seed download on this path)
- [ ] System/NAND/firmware CIAs never installed
- [ ] Home Menu / Settings still work after tests
