# GD Access

A screen-reader accessibility mod for Grim Dawn. It is a DLL injected into the game that hooks the engine's
own exported functions, models each game screen as a keyboard-navigable list, speaks through your screen
reader, and adds an audio picture of the world (wall tones, a sonar field for enemies and loot, positional
combat speech, named rooms).

Status: pre-release. It is playable through character creation, the first quests and the in-world windows,
and it is developed and tested against one exact game build. There is no installer yet; this README covers
building it from the repo and running it the way the author does. Everything here is Windows-only.

## What you need

- Grim Dawn **v1.3.0.8, 64-bit, Steam build**, at its default install path
  `C:\Program Files (x86)\Steam\steamapps\common\Grim Dawn`. The mod reaches into the game's private UI
  objects by code layout; any other build says "game version not supported" once and does nothing else.
  A different install path works if you pass `-GameExe` to `tools/inject.ps1` (below).
- Windows 10/11 x64.
- A screen reader (NVDA, JAWS, or any that prism supports). Menu and window text goes to the screen reader.
- The Windows OneCore voices "Mark" and "Zira" (Settings -> Time & Language -> Speech -> Manage voices,
  English (United States)). These carry the in-world speech: Mark speaks at the enemy's position, Zira is the
  player and the room announcer. If they are missing the default OneCore voice is used for both.

### Build tools

- Visual Studio 2022 Community with the "Desktop development with C++" workload (MSVC 14.44 is what the
  author uses; the game's ABI is MSVC, so no other compiler will do). CMake and Ninja are installed by that
  workload and `tools/vsdev.cmd` finds them at the Community edition's default path. Another edition or
  path: edit the two paths in `tools/vsdev.cmd`.
- [uv](https://docs.astral.sh/uv/) for the Python tooling (`uv run` installs Python 3.12+ and the
  dependencies from `pyproject.toml` on first use). Only needed for the dev-loop client and the offline tools,
  not for building or playing.

No other downloads: the prism speech SDK (the x64 headers, import library and `prism.dll` of release
v0.18.1), Detours, miniaudio, SQLite, doctest, the room database and the audio assets are all in the repo.

## Building

From the repo root, in any shell:

```
tools\build.cmd
```

The first run configures a Ninja RelWithDebInfo build in `build\ninja\`; later runs just build. Output:

- `build\ninja\gdaccess.dll` -- the mod
- `build\ninja\gdinject.exe` -- the injector
- `build\ninja\prism.dll` and `build\ninja\assets\` -- copied next to the DLL at build time; the DLL loads
  them from its own directory, so keep the folder together.
- `build\ninja\gdcore_tests.exe` -- unit tests for the engine-free core; run with
  `cmake --build build/ninja --target check` (inside `tools\vsdev.cmd`, or any VS developer prompt).

Note that on success MSVC and Ninja print very little; check the exit code.

## Running

### One-time game setting

In the game: Options -> Controls -> Movement Type = **Keyboard** (`movementType = 1` in
`%USERPROFILE%\Documents\My Games\Grim Dawn\Settings\options.txt`). This is the game's own WASD mode and
the mod's movement design assumes it. Keep the default key bindings (the mod's chords are built on the
defaults) and leave "Display Damage" on (combat speech reads the floating numbers).

### Playing

With the game NOT running:

```
powershell -File tools\inject.ps1 -Launch -Speak
```

This builds, starts the game with the DLL injected before it initializes, and waits for the mod to come up
(it prints a health line). The window comes up unfocused -- alt-tab to it. `-NoBuild` skips the build,
`-GameExe <path>` points at a non-default install. Two things it does to your settings: it sets
`inactiveUpdateRate` to 30 in options.txt if it is 0 (so the engine keeps running while the window is not in
front), and the injected DLL keeps the game from grabbing the foreground on its own (`GDACCESS_NOFOCUS`), so
the game never steals focus from your screen reader.

Log: `%LOCALAPPDATA%\gdaccess\gdaccess.log`, truncated on each load; every spoken line appears as `[speak]`.

## Controls

The control scheme is a hybrid: in menus and windows the mod owns the keyboard entirely and presents each
screen as a list; in the world the frequent game keys go straight to the game, every other game function is
moved to **Ctrl + its default key**, and the freed plain keys are the mod's. The game's own key bindings must
stay at their defaults (the mod's chords are built on them). `docs/controls.md` has the full detail and the
game's default map.

### Menus and windows

- Up / Down: previous / next item. Home / End: first / last. Shift+Up / Shift+Down: previous / next group.
- Left / Right: adjust (sliders, drop-downs) or move along a tab row / expand a tree group.
- Tab / Shift+Tab: next / previous panel (e.g. the tab row and the column of a window). Ctrl+Tab /
  Ctrl+Shift+Tab: switch tabs from anywhere.
- Enter: activate. Backspace: secondary action (unequip an item, reclaim a skill point at a spirit guide).
- Space or F1: the game's tooltip for the item; Shift+Space: the detailed one.
- Escape: back / close.
- Type-ahead: letters jump to matching items where a screen allows it.
- Main menu: three Tab stops -- the general buttons, the character list (when there is more than one
  character; Enter selects), then Start / difficulty / game mode / Delete.
- In-world windows: Ctrl+C or Ctrl+I inventory (Equipment, one tab per bag, Stats), Ctrl+N skills, Ctrl+Q
  codex (quests, completed, lore), Ctrl+J factions, Ctrl+M map (a flat nearest-first list of the map's
  markers; Enter picks one to follow), Ctrl+L personal riftgate. NPC windows (vendor, stash, quest reward,
  shrine, riftgate travel, conversations) open when the NPC opens them. Inside inventory / skills: Ctrl+1..0
  put the focused skill (or a weapon slot's basic attack) on quickbar slot 1..10, Ctrl+J / Ctrl+I on the
  left / right mouse button.

### In the world -- passed straight to the game

W A S D move; 1..9 0 quickbar slots; Y quickbar switch (the mod announces the new bar); Space evade;
E energy potion; R health potion; U interact (the game's own: nearest usable thing within 10 units);
Escape game menu; Alt (held) show items; F2..F7 pets.

### In the world -- the mod's keys

Review cursor (each landing says "name, distance, clock bearing, i of n", parks the game's cursor on the
thing so the game itself hovers and targets it, and plays a route ping):

- . / Shift+. -- next / previous enemy, nearest first ("name level N", plus champion / hero / boss when it is one).
- , / Shift+, -- next / previous among only the highest-rarity enemies nearby (find the boss).
- N / Shift+N -- next / previous person or object (NPCs you can talk to; rifts, shrines, doors, levers).
- B / Shift+B -- next / previous bystander (NPCs without a conversation).
- M / Shift+M -- next / previous loot (items on the ground, containers).
- V / Shift+V -- next / previous exit of the current room ("blocked" if the way is shut).
- Alt + . , N B M V -- the NEAREST of that group, whatever is reviewed now.
- ; -- ping the reviewed thing again (three sounds: straight walk / path around / unreachable, panned, fading
  with distance; also replayed automatically when the route kind changes).
- / -- inspect the target: health percent and status effects.

Acting on it:

- J or Enter -- left mouse button at the reviewed thing (attack / talk / open / move, whatever a click does);
  hold to hold. On a reviewed ground item it is the game's walk-and-pick-up command instead. "Too far away"
  when the camera does not show the thing.
- I -- right mouse button, same rules.
- G -- pick up the nearest item on the ground (the game's own Pickup).
- F -- swap weapon set (announces "weapon set N" and the two hands).
- ' -- follow the marker picked in the map (Ctrl+M): route ping plus "name, distance, bearing".

Information (through the screen reader unless noted):

- K or Ctrl+Shift+P -- where am I. H -- health and energy in full. Q -- objectives of the tracked quests.
- X -- the current room: title and description. T -- note this place to `untagged_rooms.txt` (authoring aid).
- Ctrl+1..0 -- read quickbar slot 1..10 of the displayed bar (skill and how it aims). Ctrl+- / Ctrl+= -- the
  left / right mouse skill.
- Ctrl+` -- the hotbar manager: both bars and the mouse buttons of the current weapon set; activate a slot
  to pick a learned skill (or clear / default).
- \ -- sonar on / off: every nearby enemy, loot drop and dungeon entrance repeats its own ping, faster as it
  nears and panned to its side.
- Automatic (positional voices): damage numbers, misses, dodges and blocks from where they happen; your
  health every 10 %; debuffs put on you; kills and experience; place changes ("Devil's Crossing, the
  prison, cell block corridor"). The game's banners and "skill not ready" popups are read once each.

The camera is locked by the mod (far zoom, north up), so the game's camera keys do nothing.

### In the world -- the game's remaining functions, on Ctrl

Ctrl+C / Ctrl+I character, Ctrl+N skills, Ctrl+Q codex, Ctrl+M map, Ctrl+O loot filter, Ctrl+K group,
Ctrl+G game menu, Ctrl+H help, Ctrl+J factions, Ctrl+V achievements, Ctrl+L personal riftgate, Ctrl+B drop
item, Ctrl+X item tooltips, Ctrl+Z show items, Ctrl+P pause, Ctrl+Backspace pet display, Ctrl+\ party
display, Ctrl+] toggle UI, Ctrl+Enter chat, Ctrl+, / Ctrl+. camera rotate (inert while the camera is
locked).

## The dev loop (how the author iterates)

For development the game runs **visible but never focused, with game audio and speech muted**, and it is
driven over a local HTTP dev server inside the DLL (port 8791, `GDACCESS_PORT` to change) -- so iterating on
the mod never fights the developer's screen reader. Requires the game NOT to be running already:

```
uv run tools/gd.py launch            # build, launch unfocused + muted with the DLL injected before init, wait for /health
uv run tools/gd.py launch --speak    # same, but audible
uv run tools/gd.py status            # running / CRASHED / hung
uv run tools/gd.py speech --since 0  # what the mod has spoken
uv run tools/gd.py key enter         # synthetic key events (also: keys "text", click X Y, cursor X Y)
uv run tools/gd.py log --since 0
uv run tools/gd.py kill
```

`launch` is `powershell -File tools\inject.ps1 -Launch`, which also patches `inactiveUpdateRate` to 30 in
options.txt (the engine stops ticking while unfocused at 0 -- harmless for normal play). Do not restore or
click the game window during a dev session: it activates itself and takes the keyboard. `gd.py` without
arguments lists every command; the dev routes themselves (`/text`, `/ui`, `/entities`, `/room`, `/hotbar`,
...) are documented in `CLAUDE.md` alongside the implementation notes. `tools/stacks.py` dumps native stacks
of a crashed instance; `tools/gen_exports.py` + `tools/gen_names.py` regenerate the export tables and
`src/gd_names.h` after a game patch.

Environment variables read by the DLL: `GDACCESS_PORT` (dev server port), `GDACCESS_MUTE=1` (mute game audio
and speech), `GDACCESS_NOFOCUS=1` (block the game's own focus grabs, dev only), `GDACCESS_HOOK_WIDGETS=1`
(experimental, crashes the game -- leave unset).

## Repository map

- `src/` -- the DLL. `src/core/` is engine-free and unit-tested (`tests/`); `src/screens/` is one class per
  game screen; `src/exe_ui.cpp` reads the exe's private widget frameworks; `src/gameapi*.cpp` wraps Game.dll
  exports; `src/inject/` is the injector.
- `assets/` -- audio loops/pings and `rooms.db` (the authored room map, generated offline by `tools/rooms.py`).
- `docs/` -- design notes and reverse-engineering references (`controls.md` is the player-facing one;
  `rooms.md`, `exe-ui-layout.md`, `skills-targeting.md`, `lua.md` are the engineering ones).
- `tools/` -- build scripts, injector driver, the dev-loop client, offline readers for the game's archives
  and database (`arz.py`, `arc_unpack.py`, `gdmap/`), disassembly helpers (`dll_dis.py`, `exe_dis.py`) and
  the room-authoring pipeline.
- `third_party/` -- vendored dependencies and their provenance (`third_party/README.md`).
- `CLAUDE.md` -- the running engineering log: game facts, measured layouts, what is verified and what is not.

## License and provenance

The mod's own code is by Austin Hicks, under the zlib license (`LICENSE`). Third-party components and their licenses are listed in
`third_party/README.md`. `tools/exports/` holds the game's DLL export tables (symbol names only), generated
from the installed game.
