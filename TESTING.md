# Hardware testing — `ui-refresh`

Stacked on `master` (includes fast-installs). Artifacts under `output/3ds-arm/`.

## What changed
- citro2d linked for solid rects + smooth progress bars (lerp toward target at 60fps)
- New original theme PNGs + `textcolor.cfg` (cool slate chrome, readable indigo text)
- SD override unchanged: `sdmc:/fbi/theme/` still wins over romfs
- Clearer confirm dialogs (panel + wrapped text); safety refusals use danger color / clearer copy
- Top meta info panel spacing + accent/hint coloring for publisher / description
- No changes to install allowlist / AM safety paths

## Visual / theming
- [ ] Main menu, lists, and bars render without flicker at ~60fps
- [ ] Progress installs show a smooth blue bar (not stuttery crop jumps)
- [ ] Text is readable on top/bottom bars and prompts
- [ ] Copy custom PNGs / `textcolor.cfg` to `sdmc:/fbi/theme/` → overrides apply on next launch

## Dialogs / safety
- [ ] Yes/No prompts show wrapped text inside a light panel; buttons remain tappable
- [ ] Installing / batching a system or unsafe CIA shows a clear **safety refusal** (danger styling / wording); install does not proceed
- [ ] Batch “nothing safe” notify uses refusal styling

## Regression
- [ ] CIA install / batch / URL install still works for allowlisted homebrew
- [ ] Cancel (B) still rolls back; Home Menu / Settings OK after tests
- [ ] No new crashes when opening Titles / Files / Batch CIAs
