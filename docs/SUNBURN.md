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

`SMS_SUNBURN` is Eclipse's mod support ([ECLIPSE.md](ECLIPSE.md)) with a different game module: it fetches the same BetterSunshineEngine, Moveset and header sources and builds [platform/mods/sunburn/sunburn.cpp](../platform/mods/sunburn/sunburn.cpp) and Eclipse's `yoshi/green_mode.cpp` and `yoshi/common.cpp` in place of the rest of Eclipse.
The hooks the game source needs for it are in `decomp-patches/zz-modhook-55-sunburn.patch`.

## What the module is

The authors' source is not public. `sunburn.cpp` was written from the released `SuperMarioSunburn.kxe` (v1.0, 2025-02-07), which was read with a disassembler; its behaviour, not its text, is what is here.
The disc's other modules are BetterSunshineEngine and BetterSunshineMoveset (both built from the same sources as for Eclipse) and `MirrorMode.kxe`, which is not ported yet.

| Part | What it does | Where the game source hooks it |
| --- | --- | --- |
| Setting "Win Condition" | `70 Shines` or `All Shadow Mario` | |
| Setting "New Game Plus" | unlocked after 120 shines (saved with the setting); a back nozzle in Hover's place, half the health | `TNozzleBox::load`, `TWaterGun::changeNozzle`, `TWaterGun::init` |
| 70 Shines ending | scenario 9 once 70 shines are collected and flag `0x103AE` is not set | `TMarDirector::moveStage` |
| Red coins | also count in the stage's gold coin total | `TCoinRed::taken` |
| Z at an area exit | go back to the area just left | `TMarDirector::changeState` (shared with Eclipse's hook) |
| After Corona Mountain | gates open, Yoshi is unlocked | `TModelGate::loadAfter`, a stage callback |
| Yoshi | Eclipse's green Yoshi, as is | Eclipse's hooks (green_mode.cpp) |

## Not done

- `MirrorMode.kxe`, and the memory card banner and icons (blank here).
- The module calls that normalise a `bool` argument of `TMarDirector::fireStartDemoCamera` from `TNerveBGDie::execute` (no effect natively).
- The "Win Condition: All Shadow Mario" ending is read from a settings value only; what it changes beyond that value was not found in the module.
- Not yet built or played: see the branch's commits for the state.

The module code is derived from Eclipse's (GPL-3.0) for the Yoshi patches; keep that licence when using it.
