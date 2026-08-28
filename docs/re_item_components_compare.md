# Item names vs components/augments, and equip comparison (static RE 2026-08-27, Game.dll v1.3.0.8; NOT verified live)

Read-only research, uncommitted. "READ" = read in disassembly; "inferred" is marked.

## 1. Components (`ItemRelic`) and augments (`ItemEnchantment`) -- NOT in the item name

- `Item::GetGameDescription(bool,bool)` (Game.dll 0x311f00, vtable +0x350; `Weapon::` override 0x5743c0 same shape)
  is colour + display name (+0x440) + `tagItemNameAndStack` for stacks. It never reads the relic (+0x1558) or the
  enchantment (+0x1560). So the bag label, the ground label and `gameapi::item_name` cannot show a component. READ.
- Where the game shows it: (1) the grid ICON overlay -- `ItemEquipment::GetUIBitmapOverlay()` (0x3298a0, vt +0x450)
  returns `relic->GetRelicOverlayBitmap(false)`; no text equivalent. (2) The tooltip's quality/subtitle line:
  `Armor::/Weapon::GetUIQualityDescription` prefix `tagEnchanted` = "Augmented " when an augment is attached (the
  relic is NOT in that line). (3) The tooltip body: `ItemEquipment::GetUIDisplayText` (0x3298c0, vt +0x468) ends with
  `relic->GetBoundUIDisplayText(character, lines, false, detail)` (exported, 0x334e10) and
  `enchantment->GetBoundUIDisplayText(...)` (0x3262f0).
- Component header tags (ItemRelic ctor 0x332e48): `tagComponentIncomplete` "Partial Component",
  `tagComponentComplete` "Component", `tagComponentNumber` "{%s0 - %d1 / %d2}", `tagComponentNumberRare`
  "{%s0}{%s1 - %d2 / %d3}", `tagComponentHelp` "[Right-Click to Apply]", `tagComponentError`, `tagComponentCombine`.
  Partial: "{Rare}{Partial Component - 2 / 3}"; complete: "Component". A loose component in a bag
  (`ItemRelic::GetUIDisplayText` 0x3343e0) uses the same. The completion bonus is not a tagged section: the relic's
  stat paks are emitted only when `IsComplete()`; `tagComponentBonus`/`tagComponentQualities` are unreferenced by code.
- Augment tags (exported data): `ItemEnchantment::kNameTag` = `tagEnchantmentName` "Augment", `kUsableTag`,
  `kUnusableTag`.
- Accessors (all exported, trivial field reads): `ItemEquipment::HasRelic` (+0x1558 != 0), `GetRelic`,
  `HasEnchantment` (+0x1560), `GetEnchantment`; `ItemRelic::IsComplete` (+0xc68 >= +0xc6c), `GetRelicLevel` (+0xc68,
  pieces held), `GetCompletionLevel` (+0xc6c, DBR `completedRelicLevel`), `GetParentItem` (+0x13e0).
  `ItemEnchantment::GetParentItem` is COMDAT-folded with `ItemRelic::GetRelicLevel` (same RVA 0x3251b0).
  No `GetRelicBonus/GetRelicSeed/IsRelicComplete/GetAugment*/HasComponent` exports exist.
  Decorated: `?HasRelic@ItemEquipment@GAME@@QEBA_NXZ`, `?GetRelic@ItemEquipment@GAME@@QEBAPEAVItemRelic@2@XZ`,
  `?HasEnchantment@ItemEquipment@GAME@@QEBA_NXZ`, `?GetEnchantment@ItemEquipment@GAME@@QEBAPEAVItemEnchantment@2@XZ`,
  `?IsComplete@ItemRelic@GAME@@UEBA_NXZ`, `?GetRelicLevel@ItemRelic@GAME@@UEBAIXZ`, `?GetCompletionLevel@ItemRelic@GAME@@UEBAIXZ`.
- Mod consequence: a "has a component / 2 of 3 / augmented" readout on the NAME would be our own wording from
  HasRelic + GetRelic + GetRelicLevel/GetCompletionLevel + HasEnchantment. Nothing in gameapi_items.cpp reads these yet.

## 2. Equip comparison

- Sighted UI: hovering an item shows a SECOND rollover panel headed by a compare tag: `tagItemCompareEquipped`
  "Currently Equipped" (also `...Vendor` "Item for Sale", `...Inventory`, `...Ground`), one panel object per owning
  window (ctors exe+0x1ad84d, 0x1bbd02, 0x1df1e9, 0x1e03fa; vendor 0x1fb23c; ground 0x28a50d; tag at +0xd0).
  Tutorial 49: side-by-side; weapons add a DPS comparison; Ctrl compares WITHOUT components.
- Rollover builder (exe+0x1ee8f8..0x1ef1f0): collects the player's equipped WEAPON ids into a mem::vector<uint>,
  `item->SetEquippedItems(vec, compareTag, bool)` (exported, virtual slot +0x5f0; body 0x330090 stores +0x1598 vec,
  +0x1588 tag, +0x1590 flag), then `detail = GetAsyncKeyState(VK_CONTROL) & 0x8000 || GameEngine::IsLeftTriggerDown()
  || Options::GetBool(0x44 = detailItemTooltips)`, then `item->GetUIDisplayText(player, lines, detail)` (slot +0x458).
  Option indices READ from Engine `Options::Options`: detailItemTooltips 0x44, autoItemTooltips 1, extraRollovers 4.
- The only numeric delta is DPS, and it is a real what-if simulation: `ItemEquipment::GetUIDisplayText` ->
  `ItemEquipment::CompareDPS(Character const*)` (0x330120, cached at +0x15c8, outputs floats +0x15cc/+0x15d0/
  +0x15d4/+0x15d8 and bool +0x15c9) -> `Player::CompareItems(itemId, f&,f&,f&,f&, bool&)` (0x3cacf0) which pulls the
  equipped weapons (`EquipManager::GetItemId(0/1)`, `Character::TakeItemFromCharacter`), equips the candidate via the
  controller vtable +0x780, runs `Player::CalculateDps(float&, skillId)` (skill id from the primary/secondary hot
  slot; 0 = default) after a SkillManager/CharacterBio recompute, also once with the candidate's relic+enchantment
  stripped, then restores. Outputs = truncate(A+0.5) - truncate(B+0.5) differences. Tooltip lines:
  `DamagePerSecond` "{%.0f0} Damage Per Second (If Equipped)", `DamagePerSecondOffHand` "... (Off-Hand)";
  `GetUIDisplayText_DPS` (0x32bb30) bails when the compare tag (+0x1588) is null or the equipped vector is empty.
  Inferred: which of +0x15cc/+0x15d0 is hovered vs equipped; the pairing of the four deltas.
- No armour/resistance delta, no green/red anywhere else, no `GetUIComparisonText`.
- **Mod consequence (actionable): `gameapi::item_tooltip` never calls `SetEquippedItems`, so our weapon tooltips are
  MISSING the game's "Damage Per Second (If Equipped)" line.** Fix: before `GetUIDisplayText`, call the exported
  `ItemEquipment::SetEquippedItems(item, {equipped weapon ids}, "tagItemCompareEquipped", flag)` (is-a
  ItemEquipment first), or call `CompareDPS(player)` and read the cached floats. `Player::CompareItems` temporarily
  unequips/re-equips on the calling thread -- game thread only, never from a per-frame render.
- "equipped: <name>, <tooltip>" needs nothing new: `gameapi::equipment()` for the slot's occupant (slot found by
  `can_equip` over locations 1..14), `item_name`, `item_tooltip(item, false, details)`.
  Decorated names if wired: `?SetEquippedItems@ItemEquipment@GAME@@UEAAXV?$vector@I@mem@@PEBD_N@Z`,
  `?CompareDPS@ItemEquipment@GAME@@QEBAXPEBVCharacter@2@@Z`, `?CompareItems@Player@GAME@@QEAAXIAEAM000AEA_N@Z`,
  `?CalculateDps@Player@GAME@@QEBAXAEAMI@Z`.
