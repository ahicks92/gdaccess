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
| N / Shift+N | Next / previous person or object: the important non-loot things -- NPCs with a conversation (`Npc::HasConversation`) and what the game's Interact key would use that is not loot (rifts, shrines, doors, levers: `FixedActor` whose `IsOfInterest()` says so) |
| B / Shift+B | Next / previous bystander (NPCs without one) |
| M / Shift+M | Next / previous loot: items on the ground and containers (`Item` / `FixedItemContainer` whose `IsOfInterest()` says so) |
| Alt + . N B M V | The NEAREST of that group, whatever is reviewed now (the enemy that just ran up to you) |
| \ (backslash) | Sonar sweep on / off: the automatic pings of enemies, loot and dungeon entrances around you (wotr's sonar; Ctrl+\ is the game's party display) |
| J (or Enter) | Left mouse button at the reviewed thing (or the real cursor when nothing is reviewed): attack / talk / open / pick up / move, exactly as a click. HOLD to hold (sustained attack, skill, move). A reviewed thing the camera does not show: "too far away", nothing happens |
| I | Right mouse button, same rules (the right-slot skill; hold to hold) |
| U | The game's own Interact: uses the nearest usable object (door, chest, shrine ...) or NPC within 10 units of the character, no aiming, walks there if needed |
| (camera) | Locked by the mod: zoom at the far end of its range, north up (yaw 0); the game's wheel/rotate inputs are re-pinned every frame. Zoom was found not to change what you hear (2026-08-22) |
| ; (semicolon) | Ping the reviewed thing again: one of three sounds for the route from you to it (straight walk / path around / unreachable), panned toward it, fading with distance; also played on every landing |
| K, Ctrl+Shift+P | Where am I (position, life, region) |
| H | Health and energy in full ("health 250 of 250, energy 100 of 120") through the screen reader |
| (automatic) | Hits you or your pets land: the number the game draws over the enemy ("17", "45 crit", "Miss", "Dodge", "Block"), spoken by Mark panned to where it happened, as many at once as there are hits. Needs the game's "Display damage numbers" option (`displayDamage`, default on). Health: "health N percent" in Zira each time it crosses a 10 % step, down or up |
| Ctrl+<game key> | The game's own windows and functions (see above) |
| Q | The objective tracker: each tracked quest's open objectives ("Waking to Misery: Enter the Cave under Burial Hill, ...") |
| Y | The quickbar: "quickbar 1, 1 Cadence, 2 empty, ..., left mouse Cadence, right mouse empty" (the bar the HUD shows; Ctrl+Y still switches bars) |
| G | Pick up the nearest item on the ground (the game's own Pickup action: within 10 units, loot filter applied; auto-equips into an empty slot like the game does) |
| (automatic) | Place changes, in Zira: the region when it changes, the sub-region when it changes, then the room's title ("Devil's Crossing, the prison, cell block corridor"; "room 193" while a room has no title yet). See docs/rooms.md |
| X | The current room: title, then the authored description ("no description yet" until then), through the screen reader |
| V / Shift+V | Next / previous exit of the current room: one more review group like . N B M -- destination title (or "room N"), "blocked" if the live mesh refuses the opening, distance, clock bearing, "i of n"; the landing pings the route, ; re-pings, the cursor parks on the opening |

## The in-world windows (2026-08-22, first pass)
Each game window is a screen of the mod while the game shows it (Ctrl+C/I inventory, Ctrl+N skills, Ctrl+Q
codex, Ctrl+J factions; NPC windows when an NPC opens them). Layout rule: a tab list across the top (Left/Right
moves, Enter selects; **Ctrl+Tab / Ctrl+Shift+Tab switch tabs from anywhere**), one vertical column below it
(Up/Down; Tab moves between the tab row and the column). Escape closes the window. Everything is read from the
game's own objects, never from the screen.
| Window | Tabs | Rows and keys |
|---|---|---|
| Inventory (C or I) | Equipment, one per bag, Stats | Equipment: "slot, item"; Enter unequips into the bag, Space = the game's tooltip. Bag: "item, x N" in reading order; Enter = the game's right-click (equip / drink / read), Space = tooltip. Stats: the character sheet; on Physique / Cunning / Spirit, Enter spends an attribute point |
| Skills (N) | One per mastery slot | Without a class: the six masteries (Space = description, Enter chooses; "undo class selection" until the mastery takes a point). With a class: "skill points N", the mastery bar, then the skills in tier order ("Cadence, level 0 of 16, needs mastery 1"); Enter spends a point, Backspace refunds one, Space = the game's skill text. **Ctrl+1..0 put the focused skill on quickbar slot 1..10, Ctrl+J on the left mouse button, Ctrl+I on the right** |
| Codex (Q window) | Quests, Completed quests, Lore | Quests are groups (Right expands): "name, act, tracked"; Enter toggles tracking; inside: tasks, objectives ("done" when met), rewards. Lore notes: Enter or Space reads the note |
| Factions (J) | - | "faction, standing, progress of tier" |
| Vendor (NPC) | Buy tabs per stock type, Sell | Buy rows carry the game's price line; Enter buys. Sell = your bag; Enter sells. Untested live |
| Caravan (NPC) | Stash sacks, transfer sacks | Rows = items; Enter moves one to the bag. Untested live |
| Quest reward, Shrine | - | The window's text lines and buttons (Accept; Offer / Close). Untested live |
| Riftgate travel (use a rift under N, or Ctrl+L once the personal riftgate is yours) | - | The discovered riftgates by name: "Devil's Crossing Rift, 359 away, 5 o'clock, 1 of 2", "you are here" on the one you stand at. Enter travels (the game fades and closes its map), Escape closes. Verified live |

A landing speaks "name, [distant,] distance away, clock bearing, i of n" -- "distant" when the camera does
not show it, so it cannot be clicked right now -- and parks the game's cursor on the thing while it is on
screen, so the game hovers and targets it itself. (Decided 2026-08-22, deviating from wotr: the player is
always embodied here, perception and interaction are what the camera shows; the camera is the player's.) The review cursor remembers an object ID, never a pointer: each
step rebuilds the nearest-first list live and continues from that ID, or enters at the nearest if it is gone.

## Keys the mod must not take from the game in the world
(Historical, 2026-08-21: since the Ctrl lifts every game function the mod does not pass through is
reachable as Ctrl+key, and the plain letters are the mod's -- X and V are the rooms keys now.)
WASD, 1-0, Q N C I M O K G H J V, E R L B U X Z Y P, Space, Enter, Tab (push-to-talk, harmless),
Alt/Ctrl/Shift (modifiers the game reads while clicking), Backspace, \, ], `,` `.` (camera), F2-F7.
Free on the keyboard: F1, F8, T, the bracket/semicolon/quote/slash keys, Insert/Delete/Home/End/PgUp/PgDn,
the arrow keys, numpad. Ctrl+letter chords arrive with flags and are unused by the game.
