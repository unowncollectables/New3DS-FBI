# Hardware testing — `url-installs`

Stacked on `new3ds-only`. Artifacts:
- `output/3ds-arm/3ds/FBI/FBI.3dsx`
- `output/3ds-arm/FBI.cia`

Also run the `new3ds-only` TESTING.md checklist on this build first.

## Menu changes
- [ ] Remote Install no longer lists “Receive URLs over the network”
- [ ] Entries present: Scan QR Code, Manually enter URLs, **Install from GitHub release**, Repeat/Forget last request

## Typed URL install
- [ ] Enter `https://…/Something.cia` (known-good **homebrew app** CIA) → confirm → progress shows bytes, speed, ETA
- [ ] B cancels mid-download/install → “cancelled”; no half-installed title
- [ ] Bad URL / offline → clear error; retry prompt works; no crash
- [ ] `.3dsx` URL installs under `/3ds/...`; cancel deletes partial file

## QR install
- [ ] Scan QR containing a homebrew CIA URL → same install path as typed URL
- [ ] B returns from camera; X switches camera

## GitHub release install
- [ ] Enter `owner/repo` (public repo with `.cia`/`.3dsx` on latest release) → asset list with sizes
- [ ] Also accept `https://github.com/owner/repo`
- [ ] Reject garbage / invalid names with a clear message
- [ ] Select a `.cia` asset → confirm → installs with progress; B cancels safely
- [ ] Repo with no `.cia`/`.3dsx` assets → “no .cia/.3dsx assets”
- [ ] Unknown repo → “not found” (HTTP 404)
- [ ] If rate-limited (403/429) → rate-limit message (optional; hard to force)
- [ ] Low SD space (fill card or pick huge asset) → “Not enough free space” before download when size known

## Safety checks (no bricks) — required
- [ ] URL/GitHub install of a **system** CIA (category `00040010` / `00040030` / `00040138` NATIVE_FIRM, etc.) is **refused** with “Refused unsafe or invalid CIA…”
- [ ] Local Files → Install CIA of a system title is **refused** the same way
- [ ] Successful installs only land on **SD** (Titles list, not NAND system)
- [ ] Only categories allowed: `00040000` (app), `0004000E` (update), `0004008C` (DLC)
- [ ] CIA header must parse (`cia_get_title_id`); truncated/garbage CIA → refused
- [ ] Failed/cancelled CIA install uses `AM_CancelCIAInstall` rollback — title not left half-installed
- [ ] Install path never calls `AM_InstallFirm` / never writes NATIVE_FIRM
- [ ] Install path never calls `AM_DeleteTitle` (replace uses `AM_StartCiaInstallOverwrite`)
- [ ] Tickets for non-allowed title IDs refused on URL install
- [ ] After refused system CIA attempt: Home Menu, Settings, and system still boot normally
- [ ] Do **not** test with real system NAND CIAs beyond confirming refusal; do not force-install firmwares

## Regression
- [ ] Update FBI (self-update) still works with a normal FBI.cia/3dsx release
- [ ] Titles / Files menus still open
