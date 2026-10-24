# Changelog

## v0.1.0-alpha (contentVersion 01.000.001)

First public alpha.

- Native PS5 title (PPSA20040) running the Source engine with Half-Life 2 from
  your own `steam_legacy` game files.
- OpenGL 4.6 rendering through ps5-opengl, with GPU paths for frame presents,
  refraction and screen-effect copies. Gameplay runs at about 50–60 fps at
  1920×1080.
- Audio through the PS5 audio output.
- DualSense layout written automatically, including look up/down, sprint
  toggle, gravity-gun swap, squad commands and quicksave.
- Writable data (configs, saves, logs) in `/download0`, so it survives updates.
- Home-screen icon, backgrounds and original selection music.
- Diagnostics: kernel-log mirror, crash and hang reports, sampling profiler.

### Known issues
- Quitting shows a system error before returning to the home screen.
- Short hitches the first time an area loads.
- Only Half-Life 2 has been tested; no online play.
