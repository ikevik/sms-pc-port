# ROM hacks from xdelta patches

Some Sunshine hacks ship only as an xdelta patch for the North American disc (GMSE01).
`tools/mods/romhack.py` applies one to your own image, with no xdelta tool needed (it uses `tools/mods/vcdiff.py`):

```sh
python3 tools/mods/romhack.py list
python3 tools/mods/romhack.py install sunburn --patch ~/Downloads/super_mario_sunburn_v2.zip --iso path/to/sunshine.iso
```

`--patch` takes the `.xdelta` itself or the `.zip`/`.7z`/`.rar` it came in (7-Zip is needed for the last two).
The image goes to `mods/<id>/<name>.iso`; a hack listed in [tools/mods/romhacks.json](../tools/mods/romhacks.json) has its source and result MD5s checked.
Compressed or trimmed images (CISO, NKit, RVZ) cannot be patched.

To add a hack, add an entry to `romhacks.json` with the MD5 of the disc the patch makes (`md5 -q` of a patched image).
`--out FILE` patches with any xdelta file that is not listed.

## Hacks

| id | disc | state |
| --- | --- | --- |
| `sunburn` (Super Mario Sunburn) | GMSE03 | the patched disc is made and checked; its Kuribo code (win condition, back nozzle, Eclipse's Yoshi patches) is not ported to the native build yet |

A hack with code needs its Kuribo modules ported as for Eclipse ([ECLIPSE.md](ECLIPSE.md)); one that only changes data can be played once the port accepts its disc ID.
