# Hardware testing — `new3ds-only`

Build artifacts (from this branch):
- `output/3ds-arm/3ds/FBI/FBI.3dsx`
- `output/3ds-arm/FBI.cia`

## Device under test
- [ ] New 3DS / New 3DS XL / New 2DS XL only

## Boot / gate
- [ ] Install/run CIA or 3dsx on New 3DS family → FBI starts normally
- [ ] (If available) same build on Old 3DS/2DS → fatal error: requires New 2DS/3DS; press any button to exit; console does not brick
- [ ] After New 3DS boot, UI is responsive (804MHz + L2 via `osSetSpeedupEnable` / CIA RSF)

## Memory / buffers (smoke)
- [ ] Copy a large folder on SD (Files → paste) without OOM / hang
- [ ] Install a normal SD CIA from Files; progress completes; title appears under Titles
- [ ] Cancel (B) mid-install; install aborts cleanly; no half-installed broken title
- [ ] Network URL install of a small homebrew CIA still works (baseline before `url-installs`)

## CIA metadata (makerom RSF)
- [ ] Installed CIA reports New3DS extended memory / CPU flags as expected under title info tools if you have them
- [ ] Reboot console after CIA install; system boots normally (no firm changes in this branch)

## Safety / no-brick baseline
- [ ] This branch does not add firm/NAND writers; do not manually install system CIAs while testing
- [ ] After tests, Home Menu and system settings still work
