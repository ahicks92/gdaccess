# The Inventor ("enchanter") window in the exe (static RE 2026-08-30; ids, tab, texts and flags verified live)

Companion to `docs/inventor.md`. Read with `tools/exe_dis.py` (fn / xref / str / ptrs) on the 1.3.0.8 dump;
records with `tools/arz.py "^records/ui/inventor/"`; tags with `tools/arc_unpack.py`.

## Window: `InGameUI+0x30dd8`, ctor exe+0x26bd00, vtable exe+0x31d050, size ~0x9ca8
Record `records/ui/inventor/inventor_mastertable.dbr` (template `ingameui/enchanterwindow.tpl`) -- the SAME record
the crafting window uses; the loader exe+0x26cd80 (vt[3]) binds by field name:

| offset | field / meaning |
|---|---|
| `+0x68` | visible byte (WindowB) |
| `+0x90` | listener sub-object (vt exe+0x31d040; its click handler exe+0x26cad0 is the tab switch, offsets there are listener-relative = window - 0x90) |
| `+0x98` / `+0x9c` | player id / crafter NPC id (SetCrafter = vt+0xf0 exe+0x26dcc0, RTTI-checked Character) |
| `+0x100 +0x160 +0x1c0 +0x220` | tab background bitmaps (recover, dismantle, convert, reroll) |
| `+0x280` | `enchanterHeadingBar` |
| **`+0x2e0`** | `enchanterNameText` -- the NPC's name ("Darlet"); text class vt exe+0x31c4d0, u16 string at +0x40 |
| `+0x3d8` | `enchanterHeadingText` -- "Inventor" (`tagInventorNamePlate`), vt exe+0x31c2b0 |
| `+0x4d0` | heading rollover |
| `+0x5c0` | a ButtonRegistry (ctor exe+0x12a6d0) holding the close button |
| `+0x608` | close button (exe+0x10aee0 ctor) -> window vt+0xb0 Show(false) |
| `+0x940` | `enchanterWindowTitle` |
| **`+0xa38`** | **Salvage panel** (by value, size 0x10c0, below) |
| **`+0x1af8`** | **Dismantle panel** (size 0x12a0, below) |
| `+0x2d98` | Convert panel (ctor exe+0x1a5750; expansion 2 -- unmapped) |
| `+0x5970` | Reroll panel (ctor exe+0x1b3040; expansion 3 -- unmapped) |
| **`+0x8bc0`** | **current tab index** 0 Salvage / 1 Dismantle / 2 Convert / 3 Reroll |
| `+0x8bc8` | the tab buttons' RADIO registry (press a tab through it: `PressChild` -> the listener) |
| **`+0x8c08 +0x8f40 +0x9278 +0x95b0`** | tab buttons Salvage / Dismantle / Convert / Reroll (ctor exe+0x10aee0, a bitmap button of its own class; disabled `+0x281`, pressed `+0x282`, `+0x284` mirrors disabled) |
| `+0x98e8 +0x99d8 +0x9ac8 +0x9bb8` | tab rollovers (recover, dismantle, convert, reroll: `tagDividerTab01/02`+`tagDividerRoll01/02[B]`, `tagConvertInfoB`, `tagRerollInfoB`) |

**Show(true)** = vt[22] exe+0x26d920: greys the Dismantle tab (`+0x91c1`/`+0x91c4` = button+0x281/+0x284)
unless `GameEngine::MainPlayerCanUseDismantle()` (and swaps its rollover to `tagDividerRoll02B`), Convert unless
`MainPlayerCanUseConvert`, Reroll unless `MainPlayerCanUseReroll`; then shows the current tab's panel
(`+0x8bc0`) and sets its button pressed; **Show(false)** hides every panel (each panel's Hide returns its chamber
items), then `SetSaveEnabled` + `AutoSave` + `SaveTransferStash` + `SaveReagents`.

**Tab click** (listener exe+0x26cad0): button -> `+0x8bc0` = index, Show(true) on that panel (panel vt+0x90),
Show(false) on the others (vt+0x88 = Hide); the close button -> window vt+0xb0.

## Salvage ("recover") panel: window+0xa38, ctor exe+0x1b1a40, vtable exe+0x316b90, size 0x10c0
Record `records/ui/inventor/dividingpanel/dividing_table.dbr` (template `enchanterrecoverytab.tpl`); loader
exe+0x1b2700 (vt[3]) binds `recoveryBox, recoverRelicButton, recoverItemButton, removeAugmentButton,
recoverCostNumber, tooExpensiveCostNumber, recoverCostText, recoveredItemSound`.

| offset | meaning |
|---|---|
| `+0x30` | listener (vt exe+0x316b88; click handler exe+0x1b2390, offsets listener-relative = panel - 0x30) |
| `+0x38` | visible; `+0x3c` player id (SetPlayer = vt[16] exe+0x1b2ba0) |
| **`+0x40`** | **the chamber** item box (below); item id at `+0xe0` = box+0xa0 |
| `+0x268` | the panel's ButtonRegistry (press the buttons through it) |
| **`+0x2b0`** | TextButton **Keep Add-on** (`recoverRelicButton`, `tagDividingButtonKeepComponent`, warning `tagDividingKeepComponentWarning`) |
| **`+0x660`** | TextButton **Keep Item** (`recoverItemButton`, `tagDividingButtonKeepItem`, `tagDividingKeepItemWarning`) |
| **`+0xa10`** | TextButton **Remove Augment** (`removeAugmentButton`, `tagDividingRemoveAugmentWarning`) |
| **`+0xdc0`** | `recoverCostNumber` text (vt exe+0x31c4d0, u16 at +0x40): the cost, thousands-separated |
| `+0xeb8` | `tooExpensiveCostNumber` (same text, red style; the exe writes both) |
| `+0xfb0` | `recoverCostText` ("Salvage Cost:") |
| **`+0x10a8`** | **confirm dialog pending** byte (set by the click handler, cleared when the response was processed) |
| **`+0x10a9`** | **too expensive** (money < cost) |

**Update** (vt[9] exe+0x1b1f50, per frame while visible): cost = `trunc(GetDatabase()->float("enchanterRecoveryFactor")
x item->vt+0x598 GetItemCost(false))` into `+0xdc0`/`+0xeb8`; `+0x10a9` = money < cost; buttons: all disabled
(`button+0x281 = 1`) unless the chamber has an item and it is affordable, then Keep Item + Keep Add-on enabled iff
`ItemEquipment::HasRelic`, Remove Augment iff `ItemEquipment::HasEnchantment`; then, if `+0x10a8`, polls
`DialogManager::GetNumResponsesFor/GetResponseFor(0xe | 0xf | 0x10)` and on Yes (response byte +4 == 1) calls
DoKeepAddon exe+0x1b2d30 / DoKeepItem exe+0x1b2e10 / DoRemoveAugment exe+0x1b2ef0.

**Click** (exe+0x1b2390): Keep Add-on -> `DialogManager::AddDialog(1, 0, &tagDividingKeepComponentWarning, kind 0xe, ...)`,
Keep Item -> kind 0xf, Remove Augment -> kind 0x10; `+0x10a8 = 1`. **Do\*** (exe+0x1b2d30/e10/ef0): item id from the
box, box `SetItem(0)`, `ControllerCharacter::SendEnchanterRecoveryCmd(ctrl, itemId, ObjectManager::CreateObjectID(),
keepRelic, removeAugment)` = (1,0) / (0,0) / (0,1). The result (`GameEngine::GiveRecoveredItemToEnchanterWindow`)
is placed back in the chamber by the window.

**Hide** (vt[17] exe+0x1b2bc0 with dl=0): returns the chamber item (exe+0x1ae9b0) and clears `+0x10a8`.

## Dismantle panel: window+0x1af8, ctor exe+0x1a97b0, vtable exe+0x316950, size 0x12a0
Record `records/ui/inventor/dismantlepanel/dismantle_table.dbr` (template `enchanterdismantletab.tpl`):
`dismantleCost = itemLevel*10+150`, `itemGenerated = scrapmetal.dbr`, `itemWeights = [0,0,20,25,25,15,10,5]`
(scrap count distribution), `commonItemBonus..ascendantItemBonus` = `mt_comp_dismantling_a01..e01` with weights
10/55/90/100/100/100.

| offset | meaning |
|---|---|
| `+0x30` | listener (vt exe+0x316958, click handler exe+0x1aa620; offsets listener-relative = panel - 0x30) |
| `+0x38` | formula-variable resolver (vt exe+0x316940 exe+0x1abd20: "playerLevel" -> `+0x1198`, "itemLevel" -> `+0x119c`) |
| `+0x40` | visible; `+0x44` player id (SetPlayer = vt[18] exe+0x1ab2f0) |
| **`+0x108`** | **the chamber** (item id `+0x1a8`); accept filter vt[19] exe+0x1afe90 = is-a ItemEquipment, drop exe+0x1b0190 also requires `GetItemClassification(true) > 0` |
| **`+0x330` / `+0x558`** | **result boxes** 1 / 2 (scrap; bonus or kept component). Click = pick up onto the cursor, Shift-click = straight to the bag |
| `+0x780` | ButtonRegistry; **`+0x7c8`** TextButton **Dismantle** (`tagDividerTab02`, rollover `tagDismantleButton`), disabled `+0xa49` |
| `+0xb78` / `+0xc70` | `dismantleCostNumber` / too-expensive number (both written) |
| `+0xd68` / `+0xe60` | `dynamiteTotalNumber` / not-enough number |
| `+0xf58` / `+0x1050` | cost text ("Dismantle Cost:") / dynamite text ("Total Dynamite:") |
| **`+0x1148`** | confirm dialog pending (kind 0x12) |
| `+0x1149` | cannot (either of the next two); **`+0x114a`** not enough money; **`+0x114b`** no dynamite (< 1) |
| `+0x1158` .. `+0x1298` | the record's strings + weights (`dismantleCost` formula at `+0x1158`, compiled at `+0x1190`; loot tables + weights in pairs) ; `+0x11a0` = the attached component's salvage cost (added to the price) |

**Update** (vt[11] exe+0x1aa200): dynamite count text; itemLevel/playerLevel into the resolver; the component's
recovery cost into `+0x11a0`; total cost = exe+0x1abdc0 = eval(dismantleCost) + `+0x11a0`; flags; the button
enabled iff chamber has an item and not `+0x1149`; polls dialog kind 0x12 -> DoDismantle exe+0x1ab460.

**Click** (exe+0x1aa620): with a relic -> `AddDialog(..., tagDismantleDestroyItemComponentWarning, 0x12)`; else if
`GetItemClassification(true)` is 3 or 4 (epic / legendary) -> `tagDismantleDestroyItemWarning`; else DoDismantle
at once. **DoDismantle** (exe+0x1ab460): rolls the scrap count and the bonus table (`GameEngine::GetRandomSeed`,
`ObjectManager::CreateObjectFromFile` on the loot table, `Item::CreateItem`), `ItemEquipment::RemoveRelic` when a
relic is kept, `Character::TakeItemFromCharacter` + destroy the item, then
`ControllerCharacter::SendEnchanterDismantleCmd(itemId, scrapId, bonusId, ...)`; the Execute charges bits and
`Player::SubtractDynamite`, and the results come back through `GameEngine::GiveDismantled[Bonus]ItemToEnchanterWindow`
into the two result boxes.

**Hide** (vt[20] exe+0x1ab3b0 with dl=0): returns all three boxes' items.

## The item box ("enchanteritembox.tpl"), ctor exe+0x1ad7b0, size 0x228
Vtables: salvage chamber exe+0x316c28, dismantle chamber exe+0x3169f8, result boxes exe+0x316878.

| offset / slot | meaning |
|---|---|
| `+0x58` | player id; `+0x5c` "rejected" flash, `+0x5e` |
| **`+0x61`** | holds an item that is DETACHED from the inventory |
| **`+0xa0`** | the item id (0 = empty) |
| `+0x220` | item invalid (the object vanished; Update exe+0x1ae630 then clears the box) |
| vt+0xa0 [20] exe+0x1afc10 | HasItem: `+0xa0 != 0 && !+0x220` |
| **vt+0xa8 [21] exe+0x1afc30** | **SetItem(id)**: forwards to the inner slot's SetItem and sets `+0x220 = (id == 0)` |
| vt+0xb8 [23] | click: item on the cursor -> the drop (below); empty cursor + item in the box -> pick it up onto a new CursorHandler (exe+0x1ed960); with Shift/Ctrl held (`GetAsyncKeyState`) -> return to the bag (exe+0x1ae9b0) |
| vt+0x98 [19] | accept filter (salvage exe+0x1af8c0: ItemEquipment with HasRelic; dismantle exe+0x1afe90: ItemEquipment) |
| vt+0xc0 [24] | the drop (salvage exe+0x1afc70: HasRelic or HasEnchantment; dismantle exe+0x1b0190: classification > 0): `SetItem(id)`, `PlayerInventoryCtrl::RemoveItem(id, true)`, `+0x220 = 0`, `+0x61 = 1`, cursor handler `vt+0x410` (clear) |

**Return helper exe+0x1ae9b0(box)**: id = `+0xa0`; `SetItem(0)`; `+0x61 = 0`; `ControllerPlayer::GiveItemToPlayer(ctrl,
id, false)`, else `ControllerCharacter::SendDropItemRandom`. The mod's `exe_ui::inventor_put/take` replay exactly
the drop and the return, minus the cursor.

## Item vtable slots the panels use (Game.dll, named by export address; `scratchpad/rva2name.py`)
vt+0x590 `Item::GetItemReplicaInfo`, **vt+0x598 `Item::GetItemCost(bool)`** (`ItemEquipment` overrides),
vt+0x5a0 `ItemEquipment::DumpCostAttributes`, vt+0x5a8 `Item::GetFullItemDescription`, **vt+0x5b0
`Item::GetItemClassification(bool)`**, vt+0x5b8 `Item::OnPickup`.
