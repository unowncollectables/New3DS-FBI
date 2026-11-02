# Hardware testing — New3DS FBI v3.1.0

## Builder / paths
- Pokemon 3DS Builder deploys HOME CIAs to **`sdmc:/cias/`** (`soulsilver3ds.cia`, `platinum3ds.cia`); optional secondary `sdmc:/fbi/ports/`
- Version: CIA title version + SMDH publisher (`SS3DS_VERSION` / “run N” on SoulSilver)

## Rebrand / version
- [ ] HOME Menu shows **New3DS FBI**; installs beside original FBI
- [ ] Top bar shows **Ver. 3.1.0** (not 2.6.1)
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
