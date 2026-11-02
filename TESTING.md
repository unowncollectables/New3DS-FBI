# Hardware testing — New3DS FBI (`port-cias` + rebrand)

## Builder / paths
- Pokemon 3DS Builder deploys HOME CIAs to **`sdmc:/cias/`** (`soulsilver3ds.cia`, `platinum3ds.cia`); optional secondary `sdmc:/fbi/ports/`
- Version: CIA title version + SMDH publisher (`SS3DS_VERSION` / “run N” on SoulSilver)

## Rebrand
- [ ] HOME Menu shows **New3DS FBI** (not FBI); installs beside original FBI
- [ ] SMDH description mentions **Based on FBI by Steveice10**
- [ ] Main menu title **New3DS FBI**; top screen credit line; **About / Credits** entry

## Update my ports
- [ ] Scans `/cias` then `/fbi/ports`; lists only SoulSilver / Platinum title IDs
- [ ] Empty → instructions mentioning builder `cias/` filenames
- [ ] Overwrite install; saves preserved; B cancels safely; no network

## Safety
- [ ] Unsafe CIAs refused; no DeleteTitle / InstallFirm on these paths
- [ ] Home Menu / Settings OK after tests
