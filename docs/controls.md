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
| N / Shift+N | Next / previous person (NPCs with a conversation -- `Npc::HasConversation`) |
| B / Shift+B | Next / previous bystander (NPCs without one) |
| M / Shift+M | Next / previous object: what the game's Interact key would use -- doors, chests, shrines, levers, items (`FixedActor`/`Item` whose `IsOfInterest()` says so) |
| J (or Enter) | Left mouse button at the reviewed thing (or the real cursor when nothing is reviewed): attack / talk / open / pick up / move, exactly as a click. HOLD to hold (sustained attack, skill, move). A reviewed thing the camera does not show: "too far away", nothing happens |
| I | Right mouse button, same rules (the right-slot skill; hold to hold) |
| U | The game's own Interact: uses the nearest usable object (door, chest, shrine ...) or NPC within 10 units of the character, no aiming, walks there if needed |
| (camera) | Locked by the mod: zoom at the far end of its range, north up (yaw 0); the game's wheel/rotate inputs are re-pinned every frame. Zoom was found not to change what you hear (2026-08-22) |
| ; (semicolon) | Ping the reviewed thing again: one of three sounds for the route from you to it (straight walk / path around / unreachable), panned toward it, fading with distance; also played on every landing |
| K, Ctrl+Shift+P | Where am I (position, life, region) |
| H | Health and energy in full ("health 250 of 250, energy 100 of 120"), in the player's own voice (Zira) |
| (automatic) | Hits you or your pets land: the number the game draws over the enemy ("17", "45 crit", "Miss", "Dodge", "Block"), spoken by Mark panned to where it happened, as many at once as there are hits. Needs the game's "Display damage numbers" option (`displayDamage`, default on). Health: "health N percent" in Zira each time it crosses a 10 % step, down or up |
| Ctrl+<game key> | The game's own windows and functions (see above) |

A landing speaks "name, [distant,] distance away, clock bearing, i of n" -- "distant" when the camera does
not show it, so it cannot be clicked right now -- and parks the game's cursor on the thing while it is on
screen, so the game hovers and targets it itself. (Decided 2026-08-22, deviating from wotr: the player is
always embodied here, perception and interaction are what the camera shows; the camera is the player's.) The review cursor remembers an object ID, never a pointer: each
step rebuilds the nearest-first list live and continues from that ID, or enters at the nearest if it is gone.

## Keys the mod must not take from the game in the world
WASD, 1-0, Q N C I M O K G H J V, E R L B U X Z Y P, Space, Enter, Tab (push-to-talk, harmless),
Alt/Ctrl/Shift (modifiers the game reads while clicking), Backspace, \, ], `,` `.` (camera), F2-F7.
Free on the keyboard: F1, F8, T, the bracket/semicolon/quote/slash keys, Insert/Delete/Home/End/PgUp/PgDn,
the arrow keys, numpad. Ctrl+letter chords arrive with flags and are unused by the game.
