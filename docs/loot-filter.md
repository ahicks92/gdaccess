# The loot filter (mapped and BUILT 2026-08-29, verified through the loop)

The game's Loot Filter window (default key O, key action 0x38) is a per-character bitset of 42
`LootFilterOption`s that decides which ground items get a floating name label (and therefore what a
sighted player sees and clicks). This page is the one-stop map; the static RE it summarises is in
`docs/re_lootfilter_exe.md` (the window) and `docs/re_lootfilter_gamedll.md` (the model). Everything below
marked **live** was checked against the running game (window open, its check-box map walked, screenshot).

## Model (Game.dll, all exported)
- `Player::GetLootFilter(LootFilterOption)`, `Player::SetLootFilter(opt, bool)`, `Player::SetLootFilterDefaults()`.
  Storage: a 42-bit dynamic bitset at `Player+0x4c00` (`{u32* words, end, cap, numBits}`); the factory defaults
  are a second identical bitset at `Player+0x4c20`, written only by the ctor. **No range check in Get/Set** --
  clamp to 0..41 on our side. Saved per character in the `.gdc` (`Player::Save/LoadNewFormatData`), so nothing
  global and nothing to persist ourselves.
- **Defaults**: options 0..17 and 39 on, everything else off (**live**: matches the fresh character).
- `Item::PassLootFilter(ItemIgnore) const` is the consumer: three ANDed phases, ORed inside each --
  Quality (0..7, 38, 39) -> Type (8..17) -> Damage+Character (18..37, 40, 41). Phase 3 is skipped when no
  Damage/Character box is on ("no stat filtering", the default). Overrides: Broken items never show; potions,
  quest items, lore, formulas always show; Components (classification Relic) are decided by option 17 alone;
  "Always Show Uniques"/"Always Show Double Rare" return true before the Type/stat phases.
  `ItemIgnore`: 0 = filter normally, 1 = show everything (Alt held / filter option off), 2 = everything but
  Common (Z held), 3 = quest + lore only ("Toggle Hide All Items").
- **In the game only the Pickup key (our G) and the label/loot-beam pass consult it.** `ControllerPlayer::ItemAction`
  / `PickupItem` do not, so J on a reviewed item picks up hidden loot. The mod's M group and loot sonar now apply
  the same predicate (`PassLootFilter(0)`, decided with the user 2026-08-29) with O as the "show all" override --
  see "What was built".

## Window (exe, `InGameUI+0xab410`; **live**: vtable exe+0x317640, 41 boxes)
- Class: ctor exe+0x1c7c30, vtable exe+0x317640, size 0xd88; visible byte `+0x68`, `Show(bool)` vt+0xb0,
  `IsVisible` vt+0xb8, Escape (vt+0x68) presses Close. Player id `+0x98`.
- Controls: Close button `+0x190` and the **Defaults** TextButton `+0x4c8` (`tagLootFilterReset`) in the
  press registry `+0x108`; every check box in the toggle registry `+0x150`; title text `+0x878`
  (`tagLootFilterWindowTitle`), column headers `+0x970/+0xa68/+0xb60/+0xc58` (`tagLootFilterTitle01..04`
  = Quality / Type / Damage / Character).
- **The check boxes are heap objects in a `std::map<CheckBox*, int option>` at `window+0xd58`** (MSVC map:
  node key at +0x20, value at +0x28; **live**: size 41, walked). Check box = TextButton subclass, vtable
  exe+0x313b18, size 0x3b8: checked byte `+0x282`, disabled `+0x281`, caption tag `+0x338` (narrow),
  localized caption `basic_string<u16>` at `+0x358`, tooltip tag `+0x318` (`tagLootFilterNNInfo`).
- Refresh: `Show(true)` copies `Player::GetLootFilter` into every box's `+0x282`. A click flips `+0x282`
  through the toggle registry and `OnControlEvent` (exe+0x1c99b0) calls `SetLootFilter(map[ctrl], byte)`
  immediately -- no commit, nothing on close. Defaults = a `DialogManager` Yes/No (`tagLootFilterResetConfirm`,
  InterestedParty 0x1d) polled by `Update`, then `SetLootFilterDefaults` + the refresh loop.
- The separators are images in a vector at `+0xd68`; the on-screen grouping is authoring only (ctor order).

## The option table (index = enum value = bit; **live**: index, tag and state of all 41 boxes verified)
Quality (`tagLootFilterTitle01`):
- 0 `tagLootFilter01` Common (classification 0) -- default on
- 1 `tagLootFilter02` Magic (1) -- on
- 2 `tagLootFilter03` Rare (2) -- on
- 3 `tagLootFilter04` Monster Infrequent (the BASE record is Rare-classified) -- on
- 39 `tagLootFilter40` (Ascendant, `Item::HasAscendantBonus()`; **only built when `Engine::IsExpansion3Loaded()`**,
  absent here and its English string is not in the base Text_EN.arc) -- on
- 4 `tagLootFilter05` Epic (3) -- on
- 5 `tagLootFilter06` Legendary (4) -- on
- 6 `tagLootFilter07` Sets (item has a set name) -- on
- 7 `tagLootFilter08` Always Show Uniques (Epic or Legendary -> pass immediately) -- on
- 38 `tagLootFilter39` Always Show Double Rare (prefix and suffix both Rare -> pass immediately) -- off

Type (`tagLootFilterTitle02`):
- 8 `tagLootFilter09` 1h Melee (axe, sword, mace, spear) -- on
- 9 `tagLootFilter10` 2h Melee (`Weapon::IsTwoHandedMeleeWeapon`) -- on
- 10 `tagLootFilter11` 1h Ranged -- on
- 11 `tagLootFilter12` 2h Ranged -- on
- 12 `tagLootFilter13` Dagger/Scepter (dagger, scepter, staff) -- on
- 13 `tagLootFilter14` Caster Off-Hand -- on
- 14 `tagLootFilter15` Shield -- on
- 15 `tagLootFilter16` Armor (head, chest, legs, feet, hands, shoulders) -- on
- 16 `tagLootFilter17` Accessories (amulet, ring, belt, medal) -- on
- 17 `tagLootFilter38` Components (classification Relic; decided alone) -- on

Damage (`tagLootFilterTitle03`; each also matches "+% all damage"):
- 18 `tagLootFilter18` Physical -- off
- 19 `tagLootFilter19` Pierce -- off
- 20 `tagLootFilter20` Fire (also elemental) -- off
- 21 `tagLootFilter21` Cold (also elemental) -- off
- 22 `tagLootFilter22` Lightning (also elemental) -- off
- 23 `tagLootFilter23` Acid (the engine's Poison) -- off
- 24 `tagLootFilter24` Vitality (the engine's Life) -- off
- 25 `tagLootFilter25` Aether -- off
- 26 `tagLootFilter26` Chaos -- off
- 27 `tagLootFilter27` Bleed -- off
- 28 `tagLootFilter28` Pet Bonuses (`HasPetBonus`) -- off

Character (`tagLootFilterTitle04`):
- 29 `tagLootFilter29` My Masteries (`HasMastery(player, true)`) -- off
- 30 `tagLootFilter30` Other Masteries (`HasMastery(player, false)`) -- off
- 31 `tagLootFilter31` Speed (total / run / attack / cast speed) -- off
- 32 `tagLootFilter32` Cooldown Reduction -- off
- 33 `tagLootFilter33` Crit Damage -- off
- 34 `tagLootFilter34` Offensive Ability -- off
- 35 `tagLootFilter35` Defensive Ability -- off
- 40 `tagLootFilter41` Health -- off
- 41 `tagLootFilter42` Health Regeneration -- off
- 36 `tagLootFilter36` Resistances (any damage / stun / sleep / trap / freeze / petrify / reflect / slow resist) -- off
- 37 `tagLootFilter37` Retaliation -- off

Note the tag numbering is NOT enum+1 past 17: 17 -> tag38, 38 -> tag39, 39 -> tag40, 40 -> tag41, 41 -> tag42
(the later additions were appended to the enum but numbered by their window position). Tooltips are
`tagLootFilterNNInfo` with the tag's own NN.

## Related keys (not this window)
- "Toggle Hide All Items" (unbound by default): key action 0x39, flips `InGameUI+0x72f5` (announces
  `tagLootFilterToggleOn/Off` = "Items Shown"/"Items Hidden"); exe-side only, no Game.dll state.
- Alt held (action 0x23) = show everything; Z held (0x25) = everything but Common; X (0x24) = item tooltips.
  Modifier bytes at the actor capture `+0x129/+0x12a/+0x128`.
- The game option "loot filter" is `Options::GetBool(0x11)`; off = the filter never applies.

## What was built (2026-08-29, all verified live)
- `src/gameapi_loot.cpp`: `loot_filter(opt)` / `set_loot_filter(opt, on)` / `loot_filter_defaults(column | -1)` over the
  exports (clamped 0..41), `loot_filter_options()` = the window's order with tag, English fallback, column and factory
  default (option 39 only when `Engine::IsExpansion3Loaded()`), `item_passes_loot_filter(item)` =
  `Item::PassLootFilter(0)`, `entity_hidden(e)` = `Entity::GetVisibility() == 0`.
- `src/exe_ui.cpp`: `loot_filter_boxes()` walks the window's `std::map<CheckBox*, option>` at `+0xd58`;
  `loot_filter_mirror(opt, on)` writes the drawn box's `+0x282` after a set (the game only refreshes on Show);
  `set_show_all_items(on)` writes the actor capture's Alt byte (`[[main_obj+0x90]+0x110]+0x129`).
- `src/screens/loot_filter.cpp` (Ctrl+O = the game's own window, layer 13): **each column is a Tab stop** -- its
  header as a line, the toggles in the game's order ("Physical, toggle, off, 2 of 13"; Enter flips the bit at once
  and mirrors the box; Space = `tagLootFilterNNInfo`), then "set to defaults" for that column. Escape = Show(false).
  Speech seen live: "loot filter", "Quality, 1 of 11", "Type, 1 of 12", "Damage, 1 of 13", "Physical, toggle, off,
  2 of 13", "on", "off", "Show/Hide Two-handed Axes, Maces, Spears and Swords.", "set to defaults, 13 of 13".
- **The review groups and the sonar obey the filter** (`world::scan`): a ground `Item` that fails `PassLootFilter(0)`
  is not listed by M and not pinged, like its label; O (`world::toggle_show_all_items`, "showing all items" /
  "loot filter on") lifts that AND holds the game's Alt modifier every frame (`show_all_tick`), so labels and G
  agree with what you hear. Bare O was free (not in the in-game passthrough list); the window's lift stays Ctrl+O.
- **The ghost quest item** ("a Strange Key the player already has"): a placed quest item record carries
  `requiredQuestFile` / `requiredTaskUID` (`records/storyelements/questitems/cultistkey.dbr`); `QuestItem::InitialUpdate`
  calls `SetVisibility(GetQuestVisibility())`, and `GetQuestVisibility` is false once that task is in state 3 --
  so the entity stays in the world, invisible (`Entity+0x188 == 0`; a shown item reads 3), and `Item::IsOfInterest`
  (`+0xb40 == 0 && +0xb41`) still says yes. `world::scan` now drops every entity with visibility 0, in every group.
  Not yet seen live on a collected key (none near the spawn; the four hidden entities there are engine helpers --
  spawn point, patrol point, nav blocker, map POI). `/entities` marks `[hidden]` and `[filtered]`.
- Dev: `/lootfilter` (dump + the window's boxes), `?set=<opt>&on=0|1`, `?defaults=<col|-1>`, `?all=0|1` (the O latch).
- Decided (the user, 2026-08-29): the mod's filter does NOT treat a held Alt as a bypass. Alt is also our nearest-of-group
  modifier (Alt+. N B M V), so every chord would count as "Alt held" for its duration and glitch the sonar in and out.
  Only the O latch bypasses; holding Alt affects the game's labels alone.
- Observed: the capture's Alt byte was already 1 in the unfocused dev game before the latch was ever set (a lost
  Alt release, the game's own bookkeeping); turning the latch off writes 0, which clears that too.
