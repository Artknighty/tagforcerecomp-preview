# Hello Mod (template)

The smallest behavior mod. Copy this folder to `plugins/src/MyMod`, rename the files, edit `mod.txt` and the `.cpp`, then:

```
sh tools/build/build_plugins.sh MyMod
```

The result is `gen/mods/MyMod/` (`plugin.dll` + `mod.txt`); turn it on in the launcher's Mods page. See `docs/MODS.md` (API) and `docs/HOOKS.md` (game functions you can hook).
