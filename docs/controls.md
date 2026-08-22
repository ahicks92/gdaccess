# Grim Dawn default controls (v1.3.0.8, read from Options -> Controls on 2026-08-21)

The game's keys are remappable by the player in Options -> Controls (the keyboard-icon tab): click a row's
Primary or Secondary cell, press the new key, Apply. Changed maps are written to
`%USERPROFILE%\Documents\My Games\Grim Dawn\Settings\keybindings.txt` (and `alternate_keybindings.txt`);
with no changes the files do not exist and the defaults below are in effect. `Engine::GetKeymapPath()` /
`SetKeymapPath()` are exported, so the mod could point the game at its own map file. "Movement Type" on the
same page is `movementType` in options.txt (Mouse = 0, Keyboard = 1).

The list widget does NOT draw through the RenderText2d hooks (like text fields), so this was read from
screenshots. Rows in the game's order; "-" = unassigned.

| Function | Primary | Secondary |
|---|---|---|
| Forward / Backward / Left / Right | W / S / A / D | - |
| Character Window | C | I |
| Skill Window | N | - |
| Codex Window | Q | - |
| Map Window | M | Gamepad Down |
| Loot Filter Window | O | - |
| Chat Window | Enter | - |
| Group Window | K | - |
| Game Menu | G | - |
| Help Window | H | - |
| Factions Window | J | - |
| Achievements Window | V | - |
| Quickbar Slot 01..10 | 1..9, 0 | Gamepad A/X/Y/RTrigger/Left/Right (slots 1-6) |
| Camera Zoom In / Out | Wheel Up / Wheel Down | - |
| Camera Max Zoom In / Out | - | - |
| Camera Default View | - | - |
| Camera Rotate | Middle Mouse | - |
| Camera Rotate Left / Right | , / . | - |
| Drink Energy Potion | E | Gamepad RBumper |
| Drink Health Potion | R | Gamepad LBumper |
| Center Map (Map Window) | - | - |
| Drop Item | B | - |
| Personal Riftgate | L | Gamepad Up |
| Switch Weapons | - | Gamepad LThumb |
| Interact | U | - |
| Pickup | - | - |
| Evade | Space | Gamepad B |
| Show Items (No Filter) | Alt | Right Alt |
| Show Item Tooltips | X | - |
| Show Items (Filter Common) | Z | - |
| Toggle Hide All Items (Loot Filter) | - | - |
| Target Pet (Hold Key and Click) | Ctrl | Right Ctrl |
| Stationary Attack (Hold Key and Click) | Shift | Gamepad LTrigger |
| Pause Game (Single Player Only) | P | - |
| Toggle Pet Display | Backspace | - |
| Toggle Party Display | \ | - |
| Quickbar Switch | Y | Gamepad RThumb |
| Select Pet 1..5 | F2..F6 | - |
| Select All Pets | F7 | - |
| Push To Talk | Tab | - |
| Toggle UI | ] | - |

Not on this page: Escape = game menu (pause), left mouse = move/attack at cursor (in Keyboard movement mode:
attack/interact at cursor), right mouse = secondary skill slot, F1 = not bound by the game (F9-F12 are the
mod's dev hotkeys; F2-F7 are pets).

## How the mod uses this (2026-08-21)
In the world the mod owns the keyboard. Direct pass-through (the frequent keys): WASD, 1-0, Space, E, R, U,
Escape, Alt/Right Alt, F2-F7. Everything else in the table is LIFTED to Ctrl + its default key
(Ctrl+C character, Ctrl+M map, Ctrl+N skills, ... see the `game.*` actions in src/app.cpp): the chord is the
mod's, which injects the plain key into the game's poll. The game's own map is never changed, so a player's
rebinds in Options -> Controls do not matter to the mod (and would break the lifted chords if they moved a
default key; a later version reads keybindings.txt). Not yet handled: the hold-and-click modifiers (Shift
stationary attack, Ctrl pet targeting) and typing into the game's chat field.

## The mod's in-world keys (first cut, 2026-08-21; wotr's review-cursor layout)
| Key | Action |
|---|---|
| . / Shift+. | Next / previous enemy (nearest first from the player) |
| N / Shift+N | Next / previous person (NPCs you can talk to) |
| B / Shift+B | Next / previous bystander (flavour NPCs) |
| M / Shift+M | Next / previous object |
| I, Enter | Interact with the reviewed thing (a click on it: attack / talk / open / pick up) |
| ; (semicolon) | Ping the reviewed thing again: one of three sounds for the route from you to it (straight walk / path around / unreachable), panned toward it, fading with distance; also played on every landing |
| K, Ctrl+Shift+P | Where am I (position, life, region) |
| Ctrl+<game key> | The game's own windows and functions (see above) |

A landing speaks "name, distance away, clock bearing, i of n" and parks the game's cursor on the thing, so
the game hovers and targets it itself. The review cursor remembers an object ID, never a pointer: each
step rebuilds the nearest-first list live and continues from that ID, or enters at the nearest if it is gone.

## Keys the mod must not take from the game in the world
WASD, 1-0, Q N C I M O K G H J V, E R L B U X Z Y P, Space, Enter, Tab (push-to-talk, harmless),
Alt/Ctrl/Shift (modifiers the game reads while clicking), Backspace, \, ], `,` `.` (camera), F2-F7.
Free on the keyboard: F1, F8, T, the bracket/semicolon/quote/slash keys, Insert/Delete/Home/End/PgUp/PgDn,
the arrow keys, numpad. Ctrl+letter chords arrive with flags and are unused by the game.
