# Hardware testing — New3DS FBI v3.5.0

## Builder / paths
- Pokemon 3DS Builder deploys HOME CIAs to **`sdmc:/cias/`** (`soulsilver3ds.cia`, `platinum3ds.cia`); optional secondary `sdmc:/fbi/ports/`
- Version: CIA title version + SMDH publisher (`SS3DS_VERSION` / “run N” on SoulSilver)

## Rebrand / version
- [ ] HOME Menu shows **New3DS FBI**; installs beside original FBI
- [ ] Top bar shows **Ver. 3.3.0** (not 2.6.1)
- [ ] SMDH description mentions **Based on FBI by Steveice10**
- [ ] Main menu title **New3DS FBI**; Steveice10 credit on top screen; **About / Credits**

## Kid-friendly UI
- [ ] Main menu uses plain labels (e.g. Install games from SD, My installed games, Update this app)
- [ ] Header/footer text is **white/near-white** on blue bars (readable)
- [ ] List rows are large with a clear selected highlight
- [ ] Top screen shows a short tip for the highlighted item
- [ ] Footers say **A = choose, B = back** (or START = exit on main)
- [ ] Confirm dialogs use Yes/No in plain words; errors say what to try next

## Advanced tools
- [ ] With default settings, CTR NAND / TWL NAND / Dump NAND / System Save Data are **hidden**
- [ ] Settings → Show advanced tools Off by default; enabling shows a one-time warning
- [ ] When On, **Advanced…** appears; items keep warning tooltips

## Update this app (self-update)
- [ ] Launch online: no popup; if newer release exists, subtle badge only
- [ ] Launch offline: no error shown for the check
- [ ] Settings can disable the check (persists in `sdmc:/fbi/selfupdate.cfg`)
- [ ] Skip this version hides the badge for that tag
- [ ] Install updates only title `0xF83FB` / `New3DS-FBI.cia`; settings preserved; prompts to restart

## Update my Pokemon ports
- [ ] Scans `/cias` then `/fbi/ports`; lists only SoulSilver / Platinum title IDs
- [ ] Empty → instructions mentioning builder `cias/` filenames
- [ ] Overwrite install; saves preserved; B cancels safely; no network

## Safety
- [ ] Unsafe CIAs refused; no DeleteTitle / InstallFirm on these paths
- [ ] Home Menu / Settings OK after tests

## FTP (Advanced)
- [ ] Hidden until Show advanced tools is On
- [ ] Label: Send files from your PC (FTP); tip mentions home Wi-Fi / SD only
- [ ] Screen shows big ftp://IP:5000 and simple PC steps
- [ ] Status updates: waiting → connected → transfer with progress
- [ ] B stops server and leaves; does not run in background after exit
- [ ] Can browse/upload/download only SD paths; cannot reach NAND
- [ ] Optional password from Settings enforced when set
- [ ] Keep screen awake toggle works (Settings + X on FTP screen)

## UI polish (v3.3.0)
- [ ] About is a full screen (not cramped prompt); top shows Steveice10 tip; body near-black; B/A back
- [ ] Prompt/confirm/error: body never under buttons; Up/Down scrolls long text; button labels readable
- [ ] Top bar storage shows "SD: X GB free" by default; NAND lines only with Advanced on; no clip off-screen

## v3.3.0 extras
- [ ] Install blocked with clear message when SD free space < CIA size
- [ ] Low battery + not charging warns/blocks before install
- [ ] Done screen shows time; Recent installs lists history
- [ ] FTP has no auto-install; upload only. Install requires picking a file and confirming
- [ ] My installed games: Y search, L/R letter, sizes shown; icon on top
- [ ] Advanced leftover finder lists unused user tickets + pending; delete confirms
- [ ] Sleep stays off during installs (existing dataop); lid-safe


## v3.5.0 — Save backup, SD files, About my 3DS

### Back up my saves
- [ ] Main menu shows **Back up my saves**
- [ ] Lists only user apps (`00040000`), with icons / names
- [ ] **Back up save now** writes `sdmc:/fbi/saves/<Name> [<TID>] COP<YYYY-MM-DD_HHMMSS>/`
- [ ] Game with no save shows a friendly “No save data” message (does not crash)
- [ ] **Restore a backup…** lists matching folders; **requires Yes**; makes a fresh safety backup first
- [ ] Restored save works in-game; safety copy remains on SD
- [ ] Extdata copied when present (read-only); missing extdata is fine
- [ ] System titles / NAND saves never appear

### My SD card files
- [ ] Main menu label **My SD card files**; browses SD only (no NAND from this entry)
- [ ] Copy, Cut (move), Paste here, Rename, Delete, Make a new folder all work
- [ ] **Y** marks/unmarks (`[*]`); **Delete marked items** asks Yes/No
- [ ] Delete / rename of `sdmc:/luma`, `Nintendo 3DS`, `boot.firm`, `boot.3dsx` shows a **Dangerous** warning
- [ ] Installing a `.cia` from here asks Yes/No and does not auto-start
- [ ] Sizes show on the top screen for selected files

### About my 3DS
- [ ] Shows model, system version, Luma (if detectable), region, battery, SD free/total, IP, MAC, free memory
- [ ] Does **not** show serial, keys, or friend code by default
- [ ] Read-only — B backs out; X refreshes

### Regression
- [ ] Advanced NAND browse still gated; FTP still Advanced + SD-only
- [ ] Self-update still non-pushy; no FTP auto-install
- [ ] Clean build produces `New3DS-FBI.cia` / `.3dsx` at 3.4.0
