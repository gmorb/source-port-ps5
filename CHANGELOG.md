# Changelog

## v0.1.1-alpha (contentVersion 01.000.041) — 2026-10-27

- Menus can be used with the controller: the left stick moves an on-screen
  pointer, ✕ clicks (or presses the D-pad-selected item after D-pad use),
  the right stick scrolls and ◯ goes back. New Game,
  Options and Load/Save work without a mouse.
- New Game works: the chapter list was empty because the packaged game
  folder can't be listed on the console, so Start did nothing.
- Quitting returns to the home screen without a system error.
- Fixed a crash after about 25–30 minutes of play: every engine lock leaked a
  small system object until the PS5's thread memory ran out.
- Sound plays and the game runs at full speed from the start (the window's
  focus event was being dropped, which muted audio and throttled frames).
- USB keyboard and mouse support.
- Hold L1 for a weapon menu with weapon pictures (D-pad, R1, Square); it
  appears as soon as L1 is pressed.
- In menus, △ opens a command menu (about 220 commands in categories, with
  descriptions; your own in `cfg/ps5_commands_custom.txt`) and ▢ opens the
  console. A hint in the menus' corner shows both buttons.
- Hints show DualSense buttons, or keyboard keys while a keyboard and mouse
  are being used.
- After dying, any button reloads the last save; before, a toggled sprint or
  crouch kept the game waiting until the pause menu was opened.
- ◯ Circle toggles crouch instead of having to be held.
- `zmr.log` is cleared at 32 MiB and `console.log` is reset at start-up past
  16 MiB, so long sessions can't fill the app's data area (which would make
  saving fail).

- Graphics memory is released only after the GPU has finished with it.
- Freeze diagnostics: an optional freeze recorder (`ps5_freeze_recorder 1`, or
  `-ps5_freeze_recorder` in `args.txt`) keeps a record of the last draws that
  survives a freeze and is reported on the next launch. A "Developer tests"
  category in the command menu holds the freeze-hunting switches.

### Known issues
- The game can freeze when a character starts a scripted scene, most often at
  Barney's checkpoint (when Kleiner appears on the monitor) and in Dr.
  Kleiner's lab. Close it from the PS button menu and load the last save; a
  retry usually gets through, and once a scene has played it does not freeze
  again until the game is relaunched. Being investigated.
- Short hitches the first time an area loads.
- Only Half-Life 2 has been tested; no online play.

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
