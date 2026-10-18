# Bundled skins

Four original classic skins. Each `.wsz` is a zip of BMP sheets plus `viscolor.txt`, `pledit.txt` and `region.txt`, laid out on the Winamp 2 sprite grid.

| Skin | File | Look |
|---|---|---|
| Aqua | `KoalaAmp-Aqua.wsz` | 2001 gel plastic, bondi blue |
| Laser | `KoalaAmp-Laser.wsz` | trance grid, cyan and magenta |
| Riot | `KoalaAmp-Riot.wsz` | brushed metal, hazard stripe, red |
| Starz | `KoalaAmp-Starz.wsz` | holographic pink, stars and bubbles |

```
koalaamp --skin skins/KoalaAmp-Laser.wsz
```

The title bars say **KOALAAMP**. The easter-egg title (the llama toggle) says **CHEW LOUDER**. Playlist windows carry a koala face in the title and, once the window is widened, in the visualizer strip.

`scripts/build_bundled_skins.py` rebuilds the `.wsz` files from `skins/art/`. The plates and faces there are original artwork made for these skins. No other skin's artwork is included.

The player does not load one of these automatically. Pass `--skin` with the path above.
