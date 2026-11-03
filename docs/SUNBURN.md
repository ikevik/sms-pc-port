# Super Mario Sunburn in the native port

[Super Mario Sunburn](https://gamebanana.com/) is a Sunshine ROM hack (disc `GMSE03`) that ships as an xdelta patch for the North American disc, with Kuribo code modules on it.
Nothing of the hack is in this repository.

## Getting the disc

```sh
python3 tools/mods/romhack.py install sunburn --patch PATH_TO_SUNBURN_DOWNLOAD --iso path/to/sunshine.iso
```

See [ROMHACKS.md](ROMHACKS.md). The disc goes to `mods/sunburn/Super Mario Sunburn.iso`.

## Building its code in

```sh
SMS_SUNBURN=1 ./build.sh          # or: cmake -DSMS_SUNBURN=ON ...
SMS_DISC_IMAGE="mods/sunburn/Super Mario Sunburn.iso" build/<os>-<arch>-sunburn/sms
```

`SMS_SUNBURN` uses the port's code-mod support ([ECLIPSE.md](ECLIPSE.md) describes it: the hook registry, the header shims and the fix-up of the engine's sources), with Sunburn's own modules and the engine revisions its disc carries:

- BetterSunshineEngine at `74dd35c` (v4.0.0, built 2025-12-23 on the disc) and BetterSunshineMoveset at `4f83dfe` (built 2024-12-14), not the newer ones Eclipse v1.1.0 needs. Their sources are fetched and fixed up as for Eclipse; rules and patch hunks for code the older sources lack are skipped with a warning (`SMS_FIXUP_LENIENT`, set for Sunburn builds only).
- [platform/mods/sunburn/sunburn.cpp](../platform/mods/sunburn/sunburn.cpp) (settings, ending, red coins, nozzle box, area exit) and [yoshi.cpp](../platform/mods/sunburn/yoshi.cpp) (the green Yoshi), both written from `SuperMarioSunburn.kxe`.

Nothing of Super Mario Eclipse is fetched or built. The hooks the game source needs are in `decomp-patches/zz-modhook-55-sunburn.patch` and the existing `modhook-*` patches.

## What the module is

The authors' source is not public. `sunburn.cpp` and `yoshi.cpp` were written from the released `SuperMarioSunburn.kxe` (v1.0, 2025-02-07), which was read with a disassembler; its behaviour, not its text, is what is here.
The disc's other modules are BetterSunshineEngine and BetterSunshineMoveset (the disc's own revisions, above) and `MirrorMode.kxe`, which is not ported yet.

| Part | What it does | Where the game source hooks it |
| --- | --- | --- |
| Setting "Win Condition" | `70 Shines` or `All Shadow Mario` | |
| Setting "New Game Plus" | unlocked after 120 shines (saved with the setting); a back nozzle in Hover's place, half the health | `TNozzleBox::load`, `TWaterGun::changeNozzle`, `TWaterGun::init` |
| 70 Shines ending | scenario 9 once 70 shines are collected and flag `0x103AE` is not set | `TMarDirector::moveStage` |
| Red coins | also count in the stage's gold coin total | `TCoinRed::taken` |
| Z at an area exit | go back to the area just left | `TMarDirector::changeState` |
| After Corona Mountain | gates open, Yoshi is unlocked | `TModelGate::loadAfter`, a stage callback |
| Yoshi | a green Yoshi that does not starve, rides over water, sprays and has a longer tongue (27 patches) | `TYoshi::entry`/`movement`/`thinkAnimation`, `TMario::checkCollision`/`gunExec`/`doSwimming`/`checkWet`/`thinkWaterSurface` |

## Not done

- `MirrorMode.kxe`, and the memory card banner and icons (blank here).
- The module calls that normalise a `bool` argument of `TMarDirector::fireStartDemoCamera` from `TNerveBGDie::execute` (no effect natively).
- The "Win Condition: All Shadow Mario" ending is read from a settings value only; what it changes beyond that value was not found in the module.
- Built (macOS arm64) and booted: Sunburn's 36 patches register with the disc's engine revisions; not yet played through.

