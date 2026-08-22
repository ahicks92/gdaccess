# The in-world windows: what they are built from and what exports feed them (static RE, 2026-08-22)

Two surveys (exe side with `tools/exe_dis.py`, export side with `tools/exports/*.txt` + `tools/dll_dis.py`),
cross-checked against the export listings. "Verified" = read in disassembly; "inferred" is marked. Nothing
below has been exercised live yet. RVAs are `exe+`; window offsets are from `InGameUI` (`[[main_obj+0x90]+0x2f0]`).
`docs/exe-ui-layout.md` stays the reference for framework A and the already-modelled windows.

## Cross-cutting findings (these change existing assumptions)

- **`GameEngine` IS an exported data symbol**: `?gGameEngine@GAME@@3PEAVGameEngine@1@EA` (Game.dll) and
  `?gEngine@GAME@@3PEAVEngine@1@EA` (Engine.dll). `GetProcAddress` + one dereference; the per-frame
  `GameEngine::Update` hook is then only a tick, not a pointer source. (CLAUDE.md's "not an exported singleton"
  is wrong; fix when switching.)
- **Localization needs no hook**: `LocalizationManager::Instance()` and `LocalizeWithoutParams(char const*)`
  (returns `unsigned short const*`, no hidden pointer) are public exports; also `Localize(tag, ...)`,
  `LocalizeStripColorTags`.
- **`GameTextLine` is the universal tooltip currency**, stride 0x40 (verified from `GameTextLineToString`):
  `+0x00` `GameTextClass` (dword), `+0x08` `basic_string<unsigned short>` (our `MsvcStringW` shape), `+0x28`
  bool, `+0x30` `GraphicsTexture const*` (inline icon), `+0x38` float indent. Every `GetUIDisplayText` /
  `GenerateUI*Text` builder appends to a `mem::vector<GameTextLine>` (pass a zeroed `{begin,end,cap}`).
  The vector and its strings come from the game's allocator (no exported free; free thunk Game.dll+0x5c4d4c
  inferred) -- build on demand from a key press, not per frame.
- **Call the `GetUIDisplayText` / `GetGameDescription` family virtually** (the export is the base impl;
  ItemEquipment, ItemRelic, ItemNote, OneShot_Potion... override). Same vtable-slot trick as
  `Object::GetRTTIClassInfo`.
- **Object id -> pointer is the one gap.** Inventory, equipment, lore and skill APIs hand back `unsigned int`
  ids; `ObjectManager::GetObject(id)` is NOT exported (Game.dll+0x19d20 internal, reached from
  `Singleton<ObjectManager>::Get`, verified from `Skill::ResolveValidateTarget`). Options: the exported
  `ObjectManager::GetObjectList(mem::vector<Object const*>&)` sweep + `Object::GetObjectId()` (pure export,
  O(all objects), cache per window open) or the RVA call (fast, patch-fragile). The sphere scan in
  `world.cpp` does not see inventory items (not in the region).
- Framework B vtable (every InGameUI window): `+0x10` Init, `+0x18` SetRecord(std::string), `+0x20` Render,
  `+0x38` HandleMouseEvent, `+0x48` Update, `+0x58` HandleKeyEvent (`return false` stub except the minimap),
  `+0xb0` Show(bool), `+0xb8` IsVisible, `+0xf0` OnControlEvent. Every window has a listener sub-object at
  `window+0x90` (its slot 0 = the same OnControlEvent); buttons register with `exe+0x12a800(registry,
  control, window+0x90)`. `InGameUI`'s real ctor is `exe+0x205a70` (constructs every member window);
  `Init` `exe+0x213840` hands each its `.dbr` path (field names of `records/ui/hud/hud_mastertable.dbr`).
- `Engine::GetAreaNameTag()` (via `gEngine`) is the minimap's area name (CLAUDE.md lists it as uncaptured).

## Which key opens which window (`InGameUI::HandleKeyAction` exe+0x211980, jump table on action id)

- 1 Character Window (C / I): HUD button `+0x93e8` through host `+0x7338` -> inventory/character `+0xbbf0`.
- 2 Skill Window (N): if devotion `+0x813a0` visible -> Show(false); else button `+0x9db0` -> skills `+0x3fc20`.
- 3 Codex (Q): button `+0x9730` -> codex `+0x285a0` (refused while its disabled byte `+0x99b1` is set).
- 4 Map (M): `+0xa0f0` -> minimap `+0x42260`. 5 Chat (Enter): `+0xaab8` -> chat `+0x4df00`.
- 6 Group (K): `+0xa430` -> party `+0x4b540` (network only). 7 Game Menu (G): `+0x9a70` via host `+0x72f8` -> exit `+0x4a300`.
- 8 Help (H): toggles the codex and presses its helpButton `+0x1278` through the window's registry `+0x220`.
- 0x21 Riftgate (L), 0x22 Switch Weapons, 0x2c..0x31 pets, 0x34 Factions (J): `+0xadf0` -> `+0x6c9b8`,
  0x35 Achievements (V): `+0xa778` -> `+0x7d150`, 0x36 Interact, 0x37 Pickup, 0x38 Loot Filter (O): `+0x8940` via `+0x72f8` -> `+0xab410`.
- `InGameUI::OnWindowVisibilityChanged` exe+0x213110 keeps each HUD button's pressed byte in sync, so
  **"is window X open" = a byte**: inventory `+0x966a`, codex `+0x99b2`, skills `+0xa032`, minimap `+0xa372`,
  chat `+0xad3a`, factions `+0xb072`, party `+0xa6b2`, achievements `+0xa9fa`, loot filter `+0x8bc2`,
  exit/prompt `+0x9cf2`. (Or the window's own `IsVisible`, vtable +0xb8.)
- Input priority (exe+0x212590): prompt box, exit, stack split, devotion, shrine, inventory, skills, codex,
  minimap, achievements, factions, loot filter, vendor, faction vendor, caravan, transmuter, enchanter,
  quest reward, trade, crafting, chat, inspect, party, altar, potions, item ascension, status manager.

## Window offsets (InGameUI+), ctor, vtable, size

- inventory/character `+0xbbf0` (ctor exe+0x263580, vt exe+0x31cc98, 0x1c9b0) -- MISSING from exe-ui-layout.md.
  `+0x52258` is a second instance of the same base class (inferred: the multiplayer Inspect window).
- codex/quest `+0x285a0` (exe+0x22ce50, vt exe+0x31b170, 0x1728); skills `+0x3fc20` (exe+0x279290, vt
  exe+0x31d8a8, 0x2640); devotion `+0x813a0` (exe+0x185640, vt exe+0x315d80, 0x2b38); factions `+0x6c9b8`
  (exe+0x1beda0, vt exe+0x317338, 0x10798); potions `+0x8a300` (exe+0x275910, vt exe+0x31d7a0, 0x17a8);
  minimap `+0x42260` (exe+0x1f41a0, vt exe+0x319eb8, 0x80a0); exit `+0x4a300` (as documented).
- trade `+0x29cc8` (exe+0x25ef90, vt exe+0x31c998); vendor `+0x2b538` and faction vendor `+0x2e188` (same
  class, exe+0x2716c0, vt exe+0x31d6a0, 0x2c50); caravan/stash `+0x4fd08` (exe+0x134590, vt exe+0x314788);
  enchanter `+0x30dd8` (exe+0x26bd00); crafting `+0x3aa80` (exe+0x269e80); transmuter `+0x85378`
  (exe+0x27d5c0); altar `+0x87628` (exe+0x115dd0); item ascension `+0x8baa8` (exe+0x110a90); shrine
  `+0x7da50` + `+0x7f6f8` (exe+0x1d0d20).
- party `+0x4b540` (exe+0x1ff2b0, vt exe+0x31a3c8); achievements `+0x7d150` (exe+0x10bca0, vt exe+0x312b80);
  quest reward `+0x8efd8` (exe+0x225440, vt exe+0x31ae08); objective tracker `+0x90390` (exe+0x1cfa40, vt
  exe+0x318038); stack split `+0x83ed8` (exe+0x1db730, vt exe+0x3188b0); loot filter `+0xab410`
  (exe+0x1c7c30, vt exe+0x317640); hot-slot select `+0x4fb78`; chat `+0x4df00`; notification manager
  `+0x4dbd8`; status manager `+0x4b408`; XP-bar rollover `+0x4de08`; survival `+0x90890`; endless dungeon
  `+0x91488`; challenge area `+0x92260`; gamepad HUD `+0x92570`; network address `+0x8fba8`; item spawn
  (dev) `+0x8e078`; info icon `+0x90298`.
- `+0xb138`/`+0xb158` are the `hudNpcDialogWindow` / `hudEndGameDialogWindow` record paths (the conversation
  window is built on demand, as documented).

## Per window: content source, actions, verdict

### Objective tracker (`+0x90390`) -- EASY, no widgets
Update exe+0x1d00f0 = `GameEngine::GetObjectives()` -> `mem::vector<std::string> const&`. Tracked quests =
`Quest2Repository::GetQuests(vec, 0x4)`. Nothing to press.

### Codex / quest log (`+0x285a0`) -- EASY
- Tabs: `+0x598` questButton, `+0x8d0` questCompleteButton, `+0xc08` codexButton (lore), `+0xf40`
  devotionButton, `+0x1278` helpButton (tutorials), through registry `+0x220`; close `+0x260` (registry
  `+0x1d8`); title `+0x98`. Pages are heap model/view pairs at `+0x190..+0x1d0`.
- Model, all by reference (no hidden-pointer returns at all): `Singleton<Quest2Repository>::Get()` ->
  `GetQuests(mem::vector<Quest2*>&, Filter)` (appends; filter bits: 0 all, 1 task in progress, 2 a completed
  task, 4 in progress and tracked, 8 blocked (inferred)); `Quest2::GetName/GetGroup/GetText(int)/IsTracked/
  SetTracked(bool)/IsComplete(bool)/InProgress(bool)/GetNumTasks/GetTaskByIndex`; `Quest2Task::GetName/
  GetDescription/GetState()` (1 available, 2 in progress, 3 complete; field `+0x90`, hidden byte `+0xc1`) /
  `GetObjectives()` / `GetRewards()`; `Quest2Objective::GetText(u16string&)`, `IsSatisfied()`;
  `Quest2Event::GetText(u16string&)`. Only action: `SetTracked`. (`Complete/Reset/BeginTask/Debug*` are
  server/dev mutators -- never call.)
- Lore tab: `Player::GetLoreCodex()` -> `mem::vector<unsigned> const&` of `ItemNote` object ids (Player
  `+0x4838`); `ItemNote::GetCodexTitleTag()` (`+0xca8`), `GetCodexSubHeadingTag()` (`+0xcc8`), body via
  `ItemNote::GetUIDisplayText` (virtual) or the .dbr `itemText` tag localized. Needs id -> object.
- Tutorials tab: `GameEngine::ShowTutorialPage / UnlockTutorialPage`.

### Factions (`+0x6c9b8`) -- EASY, read-only
`Character::GetFactionPack()` (Character `+0x19f0`); `FactionType` runs -3..46 (50-entry jump table in
`GetFactionTag`); per faction `FactionPack::GetFactionTag(type)` (static, std::string by value: hidden pointer
is the FIRST arg), `GetFactionInfoTag`, `GetValue`, `IsUnlocked` (`byte[pack+0xf3+type]`), `IsModified`,
`IsPositiveBoosted/IsNegativeBoosted`; visibility `GameEngine::IsFactionPlayerVisible`, `IsHiddenFaction`,
`IsStartingFaction`; tiers `GameEngine::GetNumFactionLevels`, `FactionValueToLevel`, `GetFactionLevelName(float,
u16string&)`, `GetFactionLevelBounds(float, int&, int&)`, `GetFactionTierReward`. Trap: `FactionPack::
GetDisplay(type)` ignores its argument (returns `byte[+0x4c8]`). 8 tiers in `records/game/gamefactions.dbr`.
Window: rows rebuilt each frame in Update exe+0x1c1ba0 from exactly these calls; close `+0xc58`.

### Skill bar / quickbar -- EASY, no window
`Player::GetPlayerHotSlotCtrl()`; 47 `HotSlotOption*` per skill set (`SkillSet+8`; health potion = 24, mana
potion = 25, evade = 46; sets in a map at ctrl `+0x88`, active `+0x98`, displayed `+0x9c`).
`GetHotSlotOption(i)`, `GetSlotStatus(i)`, `GetPrimarySlot/GetSecondarySlot`, `GetDisplayedSkillSetIndex`,
`NextDisplayedSkillSet`; per option `HotSlotOption::GetDisplayName(u16string&, bool)` (out-param, virtual),
`GetRolloverText(vector<GameTextLine>&)`, `GetType()` (SLOT_TYPE: 0 skill, 2/3 health/mana potion
(which is which inferred), 4 scroll, 5 evade; empty = skill option with id 0), `GetSkillId`,
`GetCooldownRemaining/GetCooldownCompletion`, `GetNumberAvailable`, `GetActiveCharges/GetMaxCharges`.
Act: `ActivateHotSlot(i, bool, bool)`, `ActivateHealthPotionSlot/ActivateManaPotionSlot/ActivateEvadeSlot(bool)`,
`SetHotSlot(i, option)` with the exported option ctors (`HotSlotOptionSkill(id)` 0x20 bytes,
`HotSlotOptionPotion(SLOT_TYPE)`, ...) + `HotSlotOption::SetPlayer`. HUD buttons exist (`InGameUI+0x70d0`,
40 pointers) but are not needed.

### Loot on the ground -- EASY, no window
`Region::GetEntitiesInSphere` (existing path) filtered to `Item` with `Item::PassLootFilter` + `IsPickupOk`;
names via `Item::GetGameDescription` (virtual) / `GetItemTextTag`; the exe's name helper is exe+0x1686f0,
pickup action exe+0x21c6c0. Hover name + health line builder is exe+0x10e940.

### Potions (`+0x8a300`) -- EASY
`PlayerInventoryCtrl::GetTotalPotions(SLOT_TYPE)`, `GetUniquePotionsOfType`, `GetPotionText`,
`GetPotionType`, `GetCurrentCooldown/GetTotalCooldown`, `UsePotionOfType`, `IsOneShotReady`. Controls:
`+0x120`/`+0x650` potions1/2 buttons, `+0xbe0` skillSelect, `+0x11d0` close, `+0x1508` title, rows from
templates `potionsContainer%u`/`potionsModifier%u`.

### Quest reward (`+0x8efd8`) -- EASY, modal
`+0x1b8` questTitleString, `+0x2b0` questNameString, `+0x388` acceptButton, `+0x7e0` XPValue, reward icons
`+0x8f0..+0xab0`. Rewards are `Quest2Task::GetRewards()` -> `Quest2Event::GetText`; there is nothing to
choose (no ChooseReward export exists), only Accept.

### Stack split (`+0x83ed8`) -- EASY, modal
`+0x160` ok, `+0x510` cancel, `+0xbf8` decrease, `+0xf30` increase; `GameEngine::GetItemMaxStackSize`.

### Shrine (`+0x7da50`) -- EASY, worth doing (progression gate)
`+0x540` title, `+0x638` info, `+0x8e0/+0xbd0/+0xec0` offering boxes, `+0x11f8` shrine button, `+0x15a8`
cancel, `+0x1958` close. Model: `StaticShrine::GetDevotionPoints/GetXpReward/IsCleansed/IsLocked/
IsActiveForMainPlayer`, `GetOffering1..3Id`, `GetOffering1..3DisplayName` (u16 by value); act
`ControllerCharacter::SendCleanseShrineCmd` / `StaticShrine::RequestToUse(id)`.

### Inventory / character (`+0xbbf0`) -- EASY-TO-MEDIUM content, MEDIUM actions; the biggest win
- Base class (shared with the inspect window; base ctor exe+0x1e09a0, loader exe+0x1e21f0, Show exe+0x1e16a0):
  `+0x100` characterView, `+0x238` close, `+0x5c0..+0x620` equip-slot control pointers (head, neck, artifact,
  chest, legs, feet, hands, right hand, finger1, finger2, waist, shoulders, medal; left hand in the same
  array), `+0x6e8`/`+0x82f8`/`+0x14bd8` the three stat-tab panes, `+0x196b8/+0x19998/+0x19c78` tab buttons,
  `+0x1a6d0` difficulty indicator.
- Inventory part (loader exe+0x264420): bag tab buttons 1..5 `+0x1a970/+0x1ac50/+0x1af30/+0x1b210/+0x1b4f0`
  (registry `+0x1c950`), sort `+0x1b7d0/+0x1bb08`, deposit components `+0x1be40`, weapon swap `+0x1c2b8`,
  title `+0x1c750`, gold text `+0x1c848`; grids = `std::vector<Grid*>` at `+0x1a760/+0x1a768`.
  Update exe+0x265a50 / Render exe+0x2650b0 re-read `ControllerPlayer::GetInventoryCtrl` every frame
  (nothing cached in widgets). Listener exe+0x2661d0: `SetSelectedSackNumber`, `SortPrimarySack/
  SortSecondarySack`, `DepositReagents`, `Player::SetAlternateConfig`.
- Model: `Player::GetSack(int)` / `PlayerInventoryCtrl::GetSack(int)` (max 6 sacks; vector at ctrl `+0x20`);
  `GetInventoryInSack(int)` -> `mem::map<unsigned itemId, Rect> const&`; `InventorySack::GetGridWidth/
  GetGridHeight/GetItemPosition/GetItemUnderPoint/GetButtonName` (tab label, u16 const&); `GetNumberOfSacks`,
  `GetSelectedSackNumber/SetSelectedSackNumber`; stash `Player::GetPrivateStash`, transfer
  `GameEngine::GetPlayerTransfer`.
- Equipment: two enums. `EquipmentCtrlLocation` (paperdoll) 1..14 = Head, Neck, Chest, Legs, Feet, Ring1,
  Ring2, Hands, RightHand, LeftHand, Relic, Waist, Shoulders, Medal, labels via static
  `EquipmentCtrl::GetEquipmentLocationTag` -> `tagSlotHead`...; `EquipLocations` (mesh attach) 0..14 = R Hand,
  L Hand, Neck, Medal, Finger, Finger, Waist, Head, Chest, Shoulders, Hands, Legs, Feet, Artifact, Clothing;
  convert with `EquipManager::GetEquipLocation/GetEquipCtrlLocation`. Contents `EquipmentCtrl::GetItemId(loc)`
  (`ControllerCharacter::GetEquipmentCtrl()`), `EquipManager::GetItemId`, `Character::GetAttachedItems()` ->
  `vector<EquipManagerContainer{Item*, EquipLocations, bool}>` (gives pointers directly), `GetIsAlternate()`.
- Item text: `Item::GetUIDisplayText(Character const*, vector<GameTextLine>&, bool)` (full tooltip, virtual),
  `GetSimpleUIDisplayText`, `GetFullItemDescription(u16string&)` (flat string), `GetUIRequirementText`,
  `GetUIQualityDescription`, `GetGameDescription(bool,bool)` (name), `GameEngine::GetItemColorText(classification)`
  (rarity word), `GetStackSize/GetMaxStackSize`, `GetItemCost(bool)`, `GetLevelRequirement`,
  `AreRequirementsMet(Character const*)`, sockets `ItemEquipment::HasRelic/GetRelic/HasEnchantment`, sets
  `ItemSet::GetUIDisplayText`. Money `Character::GetCurrentMoney()`.
- Actions: the game's drag state machine `CursorHandler` / `CursorHandlerItemMove` (`+0x28` market id,
  `+0x30` carried item id): `Item::CreatePrimaryCursorHandler(Character const*)`, `SetPlayer/SetId/SetSource/
  SetEquipId`, `PrimaryInitialize`, then `PrimaryInventoryActivate`, `PrimaryEquipActivate(loc)`,
  `PrimaryStashActivate(Vec2)`, `PrimaryMarketActivate` (= sell), `QuickDropInInventory(sack)` /
  `QuickDropInStash(sack)` / `QuickDropInTransfer(sack)` (the shift-click shortcuts: sack index, no cell),
  capability queries `IsEquipCapable/IsInventoryCapable/...`, `Cancel/Escape/IsComplete/DeleteThis`. No
  exported accessor for the game's CURRENT cursor handler. Direct alternatives: `ControllerCharacter::
  SendEquipAttachAction(id, EquipLocations, bool)` / `SendEquipDetachAction(id)`, `PlayerInventoryCtrl::
  UseItem/AddItem/RemoveItem`, `ControllerPlayer::UseItem(id, ItemSource, bool)`, `SendDropItemRandom(id)`,
  `InventorySack::Sort`.
- Obstacles: presenting a 2-D grid linearly; item moves are a sequenced state machine; id -> Item* sweep.

### Character sheet (stat tabs of the same window) -- MEDIUM (numbers yes, layout ours)
No single sheet builder exists (`Character::CreateUISummaryText` is the PET summary). Numbers:
`Character::GetTotalCharAttribute(CharAttributeType)` (58 members, dense index; name map needs one live
probe), `GetModifierPoints()` (unspent), `GetPointsSpent(type)`, `GetCharLevel/GetExperiencePoints/
GetNextLevelExperience`, `Player::CalculateDps(float&, unsigned)`, `GetAttackSpeed/GetRunSpeed/GetSpellCastSpeed`,
`CombatManager::DesignerCalculateOffensiveAbility/DefensiveAbility/CriticalChance`. Resistances/armor by
ENUMERATION: `CombatAttributeAccumulator acc` (exported ctor/dtor, size unknown -- allocate 512 zeroed bytes);
`Character::GetAllDefenseAttributes(acc)`; `acc.GetDefense()` -> `vector<CombatAttribute*>`; per attribute
vtable `+0x10` GetType, `+0x50` GetTotalDefense(float&, float&), `+0x58` GetTotalAbsorption (type 0x27 =
armor); localization tag at attribute `+0x58` (`char const*`). Class name `Player::GetClassNameA()` (u16 by
value), `GetPlayerName()` (raw u16 pointer). Act: `IncrementBaseStrengthConfigCmd(playerId)` etc. +
`Execute()`, `ResetAttributePointsConfigCmd`. Labels: hand-authored row order + `LocalizeWithoutParams`.

### Skills (`+0x3fc20`) -- MEDIUM
Controls: `+0x140` skillWheel, `+0x1568/+0x1918/+0x1cc8` mastery tabs, `+0x2108` close, `+0x2440` title;
skill icons = vector at `+0x128/+0x130`, stride 0x20, 0x51 entries (`skillCtrlPane1..80` + masterySelect).
Model: `Character::GetSkillManager()` (= Character `+0x850`); `GetSkillList()` -> `vector<Skill*>`,
`GetUISkillList()` ids, `GetSkillMasteries/Active/Allowed`, `Skill_Mastery::GetEnumeration()`,
`Character::GetSkillPoints()`, `GetNumMasteryPoints`, `GetCurrentSkillReclamationCost`; per skill (Skills are
Engine Objects: `GetObjectName` = record path) `GetSkillLevel/GetMaxLevel/GetUltimateLevel/GetMasteryLevelRequirement/
IsLocked/GetModifiers/GetSecondarySkills/GetCooldownRemaining/GetManaCost`, names `Skill::CreateUISkillName(bool)`
(u16 by value), `GetDisplayNameTag()`. Full tooltip: static `GameEngine::GenerateUISkillText(skill, out,
SkillReasons const* (zeroed 16 bytes, never null), false, bool, int levelDelta, GameTextClass 0x31, true)`
(call sites exe+0x2425c4, exe+0x242b92). Act (the exe's click handler exe+0x2483d5..): learn =
`Skill::IncrementSkillLevel(1)` (vtable `+0x48`) + `Character::SubtractSkillPoint()`; refund =
`DecrementSkillLevel(1)` (`+0x58`) + `SkillManager::UseReclamationPoints(1)`; or `SkillManager::IncrementSkill(id,
levels)` + `RecalculateSkills()`. Tree ORDER is data: `records/ui/skills/skills_mastertable.dbr` ->
`classNN/classtable.dbr` (`tabSkillButtons`) -> `skillNN.dbr` `skillName` path, joined with
`SkillManager::FindSkillId(path)` (`tools/arz.py`).

### Vendor (`+0x2b538`, faction vendor `+0x2e188`) -- MEDIUM
Tabs `+0x550/+0x888/+0xbc0/+0xef8/+0x1230`, grid `+0x2410`, search box `+0x2708`, close `+0x1b50`. Stock:
`GameEngine::GetMarketInventorySack(marketId, Market_TypeEnum)` (an ordinary `InventorySack`; enum values
unknown -- probe 0..N for non-null; market id inferred = merchant NPC object id; map at GameEngine `+0x40e0`),
`GetMarketItemStatus`, `GetMarketIsItemAffordable`; price text `CreateUIPlayerBuyText(marketId, itemId, out)` /
`CreateUIPlayerSellText`. Buy: `GameEngine::PlayerPurchaseRequest(marketId, itemId)`. Sell: only via
`CursorHandlerItemMove::SetMarketId + SetId + PrimaryMarketActivate()`.

### Caravan / stash (`+0x4fd08`) -- MEDIUM
Six tab buttons `+0xf58..+0xff8`, sub-windows `+0x13c8` stash, `+0x13d0` transfer, `+0x13d8` relic, `+0x13e0`
material, search `+0x1738`, close `+0x168`. `Player::GetPrivateStash`, `GameEngine::GetPlayerTransfer()` ->
`vector<InventorySack*>`, `SetTransferOpen`; moves via `QuickDropInStash/QuickDropInTransfer`.

### Achievements (`+0x7d150`) -- EASY-TO-MEDIUM
`+0x98` groupsList, `+0x7a8` achievementList, `+0x698` totalUnlocked, close `+0x220`.
`GameEngine::GetAchievementManager()` (reference) -> `GetGroups()` -> `vector<Group*>` (Group is opaque: one
struct to measure); `Achievement::GetTitle/GetDescription`, `IsUnlocked` (`byte[+0xc9]`), `GetHidden` (`+0xc8`).

### Party (`+0x4b540`) -- EASY, multiplayer only
`GameEngine::GetPartyManager()` -> `GetPartyMembers`, `IsPartyLeader`; `PlayerManagerClient::GetPlayerName/
GetPlayerLevel/GetPlayerPing(id)` (u16 by value). Trade (`+0x29cc8`): `GameEngine::GetTradeManager()` ->
`GetMyTradeState/GetHisTradeState` -> `TradeState::GetInventorySack/GetGoldAmount/GetFinalized`.

### Loot filter (`+0xab410`) -- MEDIUM
`Player::GetLootFilter(LootFilterOption)` / `SetLootFilter(opt, bool)` / `SetLootFilterDefaults()`; bitset at
Player `+0x4c00`, 42 options; NO exported label per option (map by hand from the UI records / localization).

### Stations -- MEDIUM-HARD (enchanter `+0x30dd8`: tabs Recover/Dismantle/Convert/Reroll `+0xa38/+0x1af8/+0x2d98/+0x5970`,
tab buttons `+0x8c08..+0x95b0`; crafting `+0x3aa80`; transmuter `+0x85378`; altar `+0x87628`; ascension `+0x8baa8`)
Model: `NpcCrafter::GetRecipes`, `GameEngine::GetPlayerFormulas`, `ItemArtifactFormula::GetReagent1..6Id/
Count/DisplayName`, `GetCreationCost`, `ControllerCharacter::SendCreateArtifactCmd`; enchanter
`MainPlayerCanUseDismantle/Reroll/Convert`, `SendEnchanterDismantleCmd/RecoveryCmd/TinkerCmd`; transmutes
`GameEngine::GetPlayerTransmutes()`, `SendTransmuteItemsCmd`; altar `GetAltarInclusiveRecipes`,
`SendAltarOfferCmd`, `AscendantAltarFormula::GetReagentText`. Opened by `GameEngine::Display*Window(npcId)`.

### Devotion (`+0x813a0`) -- HARD
The constellation map is drawn procedurally (Render exe+0x189450 with `GraphicsPrimitiveDrawer`; loader names
only `emptySkillBitmap +0x128`, `levelTextString +0x148`); no widget list. Stars ARE `Skill`s
(`records/skills/devotion/tierN_XXy.dbr`) in the normal skill list; state `Character::GetDevotionPoints/
GetTotalDevotionPoints/GetMaxDevotionPoints/GetAffinity(AffinityType)`, `SkillManager::GetNumDevotionPointsSpent`,
per star `Skill::GetDevotionLevel/GetDevotionMaxLevel/GetDevotionParent/GetConstellationDependencies/
GetAffinityBonus/GetAffinityDependencies`; text `GameEngine::GenerateUIDevotionText(star, parent?, out, reasons,
...)` (argument order inferred). Constellation structure is ONLY in `database.arz`
(`records/ui/skills/devotion/devotion_mastertable.dbr`, 87 constellations, `constellationDisplayTag`,
`affinityGiven/Required`, `devotionButton1..5`, `devotionLinks`). Act (exe commit path exe+0x18c0a0):
`Character::SubtractDevotionPoint`, `AddAffinity/SubtractAffinity`, `Skill::IncrementDevotionLevel` (vtable
`+0xa0`), `SkillManager::UseDevotionReclamationPoints`, `SendReclaimDevotionPointCmd`. A screen would be
built from the .arz graph + Skill state; the game's own hit-test (exe+0x18a820) is not needed.

### Minimap (`+0x42260`) -- HARD (raster); riftgate list maybe EASY
`+0xb08` aerialMap, `+0x7940` riftGateMap sub-object (the only window overriding HandleKeyEvent,
exe+0x1f4ea0). The riftgate destination list is unexamined.

## Dead record fields
`hudTeleportWindow`, `hudTutorialWindow`, `hudSlotConfigWindow`, `hudHealthPotionSlot`, `hudManaPotionSlot`,
`hudScoreText` appear in `hud_mastertable.dbr` but nowhere in the exe.
