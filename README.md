# GD Access

A screen-reader accessibility mod for Grim Dawn. It is a DLL injected into the game that hooks the engine's
own exported functions, models each game screen as a keyboard-navigable list, speaks through your screen
reader, and adds an audio picture of the world (wall tones, a sonar field for enemies and loot, positional
combat speech, named rooms).

Status: pre-release. It is playable through character creation, the first quests and the in-world windows,
and it is developed and tested against one exact game build. There is no installer yet; this README covers
building it from the repo and running it the way the author does. Everything here is Windows-only.

## Disclaimer

This is a hobby project, done well before AI can really do it indefinitely, and it is a mod of a closed-source
C++ game. Whether it runs on other machines is an open question, let alone whether it runs without crashing.
Any game update could break it forever. It is a low priority for me, so bugs are not going to get fixed
promptly; you will have to wait until whenever I have time. Whether this can ever be properly released is
itself an open question, and you may wake up one day to find I have redone how everything works.

In other words, do not think of this as a Factorio Access. I am not treating it like that. You get what you
get; hopefully you have fun, but it might also explode on you in ways no one can fix.

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
- Visual Studio 2022 to build it (see Building, at the end).

## Setup

I do not know whether this works with the DLC. The DLC may well break everything, it is expensive, and there
is a lot of game before you would benefit from it. We will find out eventually.

Do not launch the game directly (from Steam or the exe). If you do, you will need to restart your screen
reader: the game uses very old APIs in very odd ways, the net effect of which is to break the JAWS / NVDA key.
The mod fixes this, but only when it is injected at launch as described below.

On first launch, set Options -> Controls -> Movement Type to **Keyboard**: in the world press Escape, choose
Options Menu, Tab to the tab row and go Right to Controls, Tab into the page, arrow to Movement Type, Right
to Keyboard, then Apply. This is the game's own WASD mode and the mod's movement design assumes it. Do not
edit options.txt by hand; Steam cloud sync fights it. Keep the default key bindings (the mod's key scheme is built on
them) and leave "Display Damage" on (combat speech reads the floating numbers).

## Launching

Set up to build (see Building, at the end). Then, with the game NOT running:

```
powershell -File tools\inject.ps1 -Launch -Speak
```

This builds the mod if needed, starts the game with the DLL injected before it initializes, and waits for the
mod to come up (it prints a health line). The window comes up unfocused -- alt-tab to it. `-NoBuild` skips
the build, `-GameExe <path>` points at a non-default install. Two things it does to your settings: it sets
`inactiveUpdateRate` to 30 in options.txt if it is 0 (so the engine keeps running while the window is not in
front), and the injected DLL keeps the game from grabbing the foreground on its own, so the game never steals
focus from your screen reader. Both are development conveniences and will go away if this ever gets a proper
release.

Log: `%LOCALAPPDATA%\gdaccess\gdaccess.log`, truncated on each load; every spoken line appears as `[speak]`.

## Getting Started

The control scheme is a hybrid of a standard ARPG's and a few others. The game here is that you run around
and hit stuff. The mod mainly does a few things for you:

- Emulates mouse clicks on enemies, loot and so on.
- Makes the menus speak.
- Adds sonar and wall tones (not really optional).
- Telegraphs enemy attacks: as an enemy starts an attack, a short word says its shape (swing, stomp, wave,
  shot, ring) from where the enemy is, so you can step out of it. The game itself has no such markers.
- Tags the level data with GPS-like information that is announced as you run around (currently through
  act 1, and nowhere near fully hand-checked).

The basic flow: `.` cycles through enemies, `Alt+.` jumps to the closest one, and holding `J` attacks (`J`
and `Enter` are left clicks, `I` is a right click; in menus `Backspace` is the right-click equivalent). To explore, `V` cycles through the exits of the current
room. The game's "rooms" are more like map patches, each a few seconds to cross; a room does not imply
walls.

The sound scheme is Wrath of the Righteous's for now. `F1` opens the mod's menu anywhere; its sound glossary
lists every sound the mod plays and plays each one as you arrow over it.

The mod is quite playable but buggy around the edges. In particular it is not good at range or line of
sight: if you cannot click something the mod tells you, but it can, for example, fire a spell at something
still out of range. This is probably infeasible to fix, but it has not been much of a problem in practice;
walk closer.

Combat events are spoken by Mark and Zira. Mark is things happening to enemies; Zira is things happening to
you. `T` (or the F1 menu) opens the combat announcement settings: outgoing announcements off / brief / full,
incoming announcements, incoming hit announcements ("hit" for every attack that lands on you), and the telegraph
cues with a filter for who and which shapes speak. All of it is remembered between sessions.

The game assumes you already know what you are getting into, so a few things are worth knowing:

- `Ctrl+L` opens a rift. `N` to target it, `J` to interact, and you can return to town temporarily.
- Saving is automatic, but restarting the game reloads you at the last major riftgate and drops any rifts
  you opened -- so no returning to town, closing the game and coming back.
- To choose a class, reach level 2, press `Ctrl+N`, pick a mastery, Tab over, and select the mastery to
  spend a skill point on it. Every other build choice can be undone except this first mastery point:
  classes are permanent, skills are not. You get a second class at level 10 if you want one.
- Levelling up is not announced because it is already obvious: the big loud clonky scare chord.
- Health and energy potions are `R` and `E`. They are not items in your inventory; they are modelled as
  skills every character has, with a cooldown, so you never run out.
- `Space` reads tooltips in menus and evades in the world. You will not really need evade for a long time,
  if ever, but to dodge, first turn OFF "Evade To Cursor" in Options -> Gameplay (it is on by default and makes
  Space dash toward the mouse cursor, ignoring WASD). With it off:
  - Space while holding WASD dodges in that direction.
  - Space while not holding WASD dodges the way your character faces (toward the thing you are attacking, or
    the way you last walked).

## Controls

The scheme is not vanilla Grim Dawn: menus and windows are presented as lists, and in the world the keys are
arranged around a review cursor. The game's own bindings must stay at their defaults.

### Menus and windows

| Key | Description |
|---|---|
| Up / Down | Previous / next item |
| Home / End | First / last item |
| Shift+Up / Shift+Down | Previous / next group |
| Left / Right | Adjust a slider or drop-down; move along a tab row; expand or collapse a tree group |
| Tab / Shift+Tab | Next / previous panel (for example a window's tab row and its column) |
| Ctrl+Tab / Ctrl+Shift+Tab | Switch tabs from anywhere in a window. On the tab row itself, Left / Right open the tab you land on |
| Enter | Activate |
| Backspace | Secondary action: unequip an item; reclaim a skill point at a spirit guide |
| \ (backslash) | On an item in a bag, a vendor or the stash: what you have equipped in the slot it would go to -- the slot, the item and its tooltip ("nothing equipped" for an empty slot). The game's side-by-side comparison, spoken |
| Space | The game's tooltip for the item; Ctrl+Space the detailed one |
| Escape | Back / close |
| Letters | Type-ahead to a matching item, where the screen allows it |

The main menu has three Tab stops: the general buttons; the character list (only when there is more than one
character; Enter selects); then Start / difficulty / game mode / Delete.

### Moving and interacting

| Key | Description |
|---|---|
| W A S D | Move |
| Space | Evade (with Options -> Gameplay -> Evade To Cursor OFF: in the movement direction while moving, the way you face when standing) |
| J or Enter | Left mouse button at the reviewed thing: attack / talk / open / move, whatever a click does. Hold to hold (sustained attack). On a reviewed ground item, the game's walk-and-pick-up instead; on a reviewed door, ladder, chest, lever or shrine, the game's walk-and-use. "Too far away" when the camera does not show the thing |
| I | Right mouse button at the reviewed thing (the right-hand skill), same rules |
| U | Interact with the nearest usable thing within 10 units (door, chest, shrine, NPC), no aiming |
| G | Pick up the nearest item on the ground |
| E / R | Energy / health potion |
| F | Swap weapon set (announces "weapon set N" and the two hands) |
| Escape | Game menu |

The camera is fixed by the mod (far zoom, north up); there are no camera keys.

### Finding things: the review cursor

Each landing says "name, distance, clock bearing, i of n", parks the game's cursor on the thing so the game
itself hovers and targets it, and plays a route ping. Shift reverses; Alt jumps to the nearest of the group.

| Key | Description |
|---|---|
| . / Shift+. | Next / previous enemy, nearest first ("name level N", plus champion / hero / boss when it is one) |
| , / Shift+, | Next / previous among only the highest-rarity enemies nearby (find the boss) |
| N / Shift+N | Next / previous person or object: NPCs you can talk to; rifts, shrines, doors, levers |
| B / Shift+B | Next / previous bystander (NPCs without a conversation) or breakable (barrels, crates, jugs, quest destructibles -- hold J to smash) |
| M / Shift+M | Next / previous loot: items on the ground, containers |
| V / Shift+V | Next / previous exit of the current room ("blocked" if the way is shut) |
| ] / [ | Next / previous of your own pets ("Hellhound, aggressive, 2 away, 1 o'clock, 1 of 2") |
| Alt + . , N B M V ] | The nearest of that group, whatever is reviewed now |
| ; | Ping the reviewed thing again: one of three sounds (straight walk / path around / unreachable), panned, fading with distance. Also replayed automatically when the route kind changes |
| / | Inspect the target: health percent and status effects |
| \ | Sonar on / off: every nearby enemy, loot drop, breakable, devotion shrine (ruined shrines have their own sound; restored ones share the loot ping), dungeon entrance and other person or thing you can use (quest NPCs, merchants, doors, levers, riftgates, notes, graves) repeats its own ping, faster as it nears and panned to its side |
| Ctrl+M | The map as a flat nearest-first list of its markers (merchants, riftgate, spirit guide, quest markers); Enter picks one to follow |
| ' | Follow the picked map marker: route ping plus "name, distance, bearing" |

### Information

| Key | Description |
|---|---|
| K or Ctrl+Shift+P | Where am I: position, life, region |
| H | Health and energy in full |
| X | The current room: title and description |
| Q | Objectives of the tracked quests |
| F1 | G D Access menu (anywhere): sound glossary (every mod sound as a tree; landing on a row plays it) and, in the world, the combat announcement settings |
| T | Combat announcement settings. First Tab stop: outgoing announcements off / brief (just "hit", "crit", "miss", "blocked") / full (the numbers), incoming announcements (your health, effects on you) on/off, incoming hit announcements ("hit" for every attack that lands on you) on/off, and telegraph cues with four states: off, your target (only the enemy you are reviewing or fighting), highest tier (only the strongest kind of enemy nearby, so a pack's boss speaks and its adds do not), all. Enter cycles, Left/Right step. Second Tab stop: one on/off row per cue shape (swing, stomp, wave, shot, ring). Escape closes; everything is saved between sessions |

Spoken automatically, by position: damage numbers, misses, dodges and blocks from where they happen; your
health at every 10 % step; debuffs put on you; kills and experience; place changes ("Devil's Crossing, the
prison, cell block corridor" -- the first part is the game's own area name, exactly what the minimap shows a
sighted player there). The game's banners (level up, quest updated) and its "skill not ready" style
popups are read once each.

### Skills and the quickbar

| Key | Description |
|---|---|
| 1..9, 0 | Quickbar slots |
| Y | Switch quickbar (announces "quickbar N") |
| Ctrl+1..0 | Read quickbar slot 1..10 of the displayed bar: the skill and how it aims ("Cadence, at a target", "War Cry, around you", "Overguard, self") |
| Ctrl+- / Ctrl+= | Read the left / right mouse skill |
| Ctrl+` | Hotbar manager: both bars and the mouse buttons of the current weapon set; activate a slot to pick a learned skill, or clear / default |
| Alt (held) | Show item labels |
| O | Show all items on / off ("showing all items" / "loot filter on"): while on, the loot review group (M), the loot sonar and the game's own labels ignore your loot filter -- the same as holding Alt, latched |
| F2..F6 / F7 | Select pet 1..5 (toggle) / select all pets, announced; the selection applies to the next pet command only |
| Shift+Backspace | The selected pets (all, if none are selected) attack the locked target |

### Pets

Pets are announced as they come and go ("Hellhound summoned", "Hellhound down") and never count as enemies.
The game's own "Pet Attack" skill (all pets attack the cursor's target, or move to a point) can be put on a
quickbar slot from the hotbar manager and works against the locked target like any aimed skill.

| Key | Description |
|---|---|
| Backspace | The pet overlay: one row per pet, "name, stance, selected". Left / Right change the stance (normal, aggressive, defensive -- shared by every pet of that summoning skill, remembered across resummons), Enter toggles selected, Backspace disbands, Space says where it is. Below the pets: "attack locked target" and "recall", for the selected pets or all; a command closes the overlay |

### Windows

| Key | Description |
|---|---|
| Ctrl+C or Ctrl+I | Inventory: Equipment, one tab per bag, Stats. An item with a component attached reads "<name> with component" (Space names it). On an equipment slot Enter opens a picker of everything that fits, Backspace unequips. In a bag Enter is the game's right-click (equip / drink / read); on a component it opens a picker of the items it can be attached to (the ones you are wearing first, marked "equipped"). In Stats, Enter on Physique / Cunning / Spirit spends a point. **Bags:** a pickup joins an existing stack in any bag, else goes into bag 1, else into the **secondary** bag -- nothing else, so a full bag 1 reads "inventory full" until a secondary is set. **Ctrl+Enter on a bag tab makes it the secondary**; its tab then reads "bag 2, secondary". Bag 1 is always first, so making it the secondary just means none |
| Ctrl+N | Skills: one tab per mastery (Enter spends a point, refusing with the reason if the game would; "undo points" reverts everything spent since the window opened; refunding only at a spirit guide with Backspace, refused with the reason when the game would: modifiers still holding points, an attached celestial power, a skill that needs the mastery level, not enough iron bits), then **Constellations** (devotion: points and affinities on top, then every constellation as a tree -- "Bat, 2 of 5, celestial power Twin Fangs, gives Chaos 2, Eldritch 3"; expand for its stars in order, "star 3, needs star 2"; Enter spends a devotion point, Space reads the star or the constellation; at a spirit guide Backspace reclaims a star for iron bits and aether crystals) and, once you have one, **Celestial Powers** ("Twin Fangs, level 1 of 20, attached to Cadence, from Bat"; Enter picks the skill it triggers from, "none" detaches) |
| Ctrl+Q | Codex: quests (Enter toggles tracking; expand for tasks, objectives, rewards), completed quests, lore |
| Ctrl+J | Factions |
| Ctrl+L | Personal riftgate |
| Ctrl+1..0, Ctrl+J, Ctrl+I | Inside inventory / skills: put the focused skill (or, on a weapon slot, the weapon's basic attack) on quickbar slot 1..10 / the left mouse / the right mouse |
| Ctrl+O | Loot filter: the game's four columns (Quality, Type, Damage, Character) as four Tab stops, each a list of toggles in the game's order ending in "set to defaults" for that column. Enter flips a box -- it takes effect at once and is saved with the character; Space reads the game's own explanation of the box. Rarity and type must each match a ticked box; the Damage / Character boxes only start filtering once one of them is ticked (an item then needs one of the ticked stats), and Epics / Legendaries always show while "Always Show Uniques" is on. The filter decides which items get a label in the game, which items G picks up, and now also which items M and the sonar tell you about (O overrides) |
| (blacksmith) | Crafting: opens when you talk to a blacksmith. The smith's specialty is read under his name (what bonus his crafted gear gets, with ranges). Five tabs (Relics, Melee Weapons, Ranged Weapons, Armor, Accessories and Consumables) of the recipes you know, grouped as the game groups them; each row reads "name, can make N, cost iron bits, the reagents". Enter crafts one (into your bag, or straight onto an empty slot) when you can make it, otherwise says what is missing ("missing 2 Aether Crystal, 1,200 iron bits"); Space reads the result's tooltip (stat ranges -- the item is rolled when made), Ctrl+Space the details |
| (inventor) | The Inventor's window opens when you talk to an Inventor. Two tabs. **Salvage** lists the bag items that carry a component or an augment, each with its salvage cost; Enter opens a choice of what to do with it -- Keep Item (the component is destroyed), Keep Add-on (you get the component back, the item is destroyed), Remove Augment -- Space on a choice reads the game's warning; the game then asks Yes/No, and what you kept lands in your bag. **Dismantle** (once the Inventor has learned it) lists the bag items above common quality with their cost; the first line says how much dynamite and iron you have; Enter dismantles one (1 Dynamite + iron bits) and names what came out ("dismantled, Scrap, Vicious Jawbone"). Space = the item's tooltip, Escape closes. Equipped items do not appear -- unequip first |
| Ctrl+K, Ctrl+G, Ctrl+H, Ctrl+V, Ctrl+B, Ctrl+X, Ctrl+Z, Ctrl+P, Ctrl+], Ctrl+\, Ctrl+Enter | The game's own group, game menu, help, achievements, drop item, item tooltips, show items, pause, toggle UI, party display, chat |

NPC windows (vendor, stash, quest reward, shrine, riftgate travel, conversations) open when the NPC opens them
and follow the menu keys above. In a vendor's Sell tab, Enter sells the whole item (a stack entire);
Ctrl+Enter on a stack asks "sell how many of N" -- type the number and press Enter (Escape cancels).

## The dev loop (how the author iterates)

For development the game runs **visible but never focused, with game audio and speech muted**, and it is
driven over a local HTTP dev server inside the DLL (port 8791, `GDACCESS_PORT` to change) -- so iterating on
the mod never fights the developer's screen reader. Requires the game NOT to be running already, and
[uv](https://docs.astral.sh/uv/) for the Python tooling (`uv run` installs Python 3.12+ and the dependencies
from `pyproject.toml` on first use):

```
uv run tools/gd.py launch            # build, launch unfocused + muted with the DLL injected before init, wait for /health
uv run tools/gd.py launch --speak    # same, but audible
uv run tools/gd.py status            # running / CRASHED / hung
uv run tools/gd.py speech --since 0  # what the mod has spoken
uv run tools/gd.py key enter         # synthetic key events (also: keys "text", click X Y, cursor X Y)
uv run tools/gd.py log --since 0
uv run tools/gd.py kill
```

Do not restore or click the game window during a dev session: it activates itself and takes the keyboard.
`gd.py` without arguments lists every command; the dev routes, the hot-reload loop and the implementation
notes are in `CLAUDE.md`.

Environment variables read by the DLL: `GDACCESS_PORT` (dev server port), `GDACCESS_MUTE=1` (mute game audio
and speech), `GDACCESS_NOFOCUS=1` (block the game's own focus grabs, dev only), `GDACCESS_HOOK_WIDGETS=1`
(experimental, crashes the game -- leave unset).

## Building

- Visual Studio 2022 Community with the "Desktop development with C++" workload (MSVC 14.44 is what the
  author uses; the game's ABI is MSVC, so no other compiler will do). CMake and Ninja are installed by that
  workload and `tools/vsdev.cmd` finds them at the Community edition's default path. Another edition or
  path: edit the two paths in `tools/vsdev.cmd`.
- No other downloads: the prism speech SDK (the x64 headers, import library and `prism.dll` of release
  v0.18.1), Detours, miniaudio, SQLite, doctest, the room database and the audio assets are all in the repo.

From the repo root, in any shell:

```
tools\build.cmd
```

The first run configures a Ninja RelWithDebInfo build in `build\ninja\`; later runs just build. Output:

- `build\ninja\gdaccess.dll` -- the mod
- `build\ninja\gdinject.exe` -- the injector
- `build\ninja\prism.dll` (the screen-reader speech library) and `build\ninja\assets\` -- copied next to
  the DLL at build time; the DLL loads them from its own directory, so keep the folder together.
- `build\ninja\gdcore_tests.exe` -- unit tests for the engine-free core; run with
  `cmake --build build/ninja --target check` (inside `tools\vsdev.cmd`, or any VS developer prompt).

On success MSVC and Ninja print very little; check the exit code.

## License

The mod's own code is by Austin Hicks, under the zlib license (`LICENSE`). Third-party components and their
licenses are listed in `third_party/README.md`. `tools/exports/` holds the game's DLL export tables (symbol
names only), generated from the installed game.
