# SK-1 · Bundled classic skins

- **ID / title:** `SK-1 · Bundled classic skins`
- **Milestone:** M1 · **Est:** 1d focused · **Depends:** SE-1
- **Status:** done

## Story
Ship a few original classic skins so KoalaAmp has something to wear without fetching a third-party `.wsz`. They should look like early-2000s Winamp skins and carry the KoalaAmp name.

## Tasks
- [x] Four classic-format skins: Aqua, Laser, Riot, Starz.
- [x] Generator `scripts/build_bundled_skins.py` plus the plates and faces it samples.
- [x] Loader test that each `.wsz` decodes with no warnings.

## Acceptance / Definition of Done
- `python3 scripts/build_bundled_skins.py` writes `skins/KoalaAmp-{Aqua,Laser,Riot,Starz}.wsz`.
- `koalaamp_skins_tests` case `bundled KoalaAmp skins decode cleanly` passes.
- `spike_fe3 --skin skins/KoalaAmp-Laser.wsz --state spectrum --scale 3 --out /tmp/laser.png` renders a main window whose title reads KOALAAMP.

## Out of scope
- Modern `.wal` skins.
- Shipping or remixing anyone else's skin artwork.
- Changing the default launch to auto-load a skin (the preset soak job is deliberately skinless).

## References
- `docs/spec.md` §5.1 — classic sheet set
- `src/skins/SkinSprites.hpp` — sprite rectangles
- `docs/research/skins.md` §1 — WSZ layout
