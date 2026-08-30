# The blacksmith / crafting window (mapped and BUILT 2026-08-29, verified live)

The one window a blacksmith (Angrim / Duncan, `NpcCrafter`) opens: the game's crafting window, "Forgemaster"
(`InGameUI+0x3aa80`). There is no second blacksmith window -- dismantling / transmuting are the Inventor's
(enchanter `+0x30dd8`, transmuter `+0x85378`), unmapped. The static RE is `docs/re_crafting_exe.md` (window) and
`docs/re_crafting_gamedll.md` (model); everything marked **live** below was checked on the running game with a
spawned Angrim (see "Getting a crafter for testing").

## What the player does
Talk to the blacksmith: the window opens on the left with the inventory beside it. Five icon tabs (Relics,
Melee Weapons, Ranged Weapons, Armor, Accessories + Consumables), a search box, and a recipe tree under the
tab: group headers ("AXES", "BELTS", "COMPONENTS", "EMPOWERED RELICS", ...) then rows "[N] Name", N = how many
you could make right now, the name coloured by the RESULT's rarity (yellow magical, green rare, cyan relic).
Selecting a row fills the panel below: the primary reagent (big left slot, "have/need", red when short), up to
six more reagents (2x3 grid), the result (right slot; hovering it = the result's tooltip with stat RANGES and
"+Random Stat(s)", since it is the unrolled template), "Cost: N" iron bits (red when unaffordable), and
Combine -- enabled only when every reagent and the cost are met. Combine takes the reagents (bags, then the
materials tab, then personal stash, then transfer stash), charges the bits, puts the item straight into the bag
and rolls the blacksmith's bonus into it (Angrim: pierce resistance / armor / physique; Duncan: energy
regeneration / defensive ability / physique -- shown as text on the crafter icon's rollover, not per row).
Recipes = the crafter's own list + every blueprint the character has learned (`GetPlayerFormulas`) + an
87-entry default list; neither blacksmith restricts. No level or faction gate anywhere.

## Model (Game.dll, all exported; no exe offsets needed for the content)
- A formula is an `ItemArtifactFormula` (an `Item`, type 12, `records/items/crafting/blueprints/**`). Known
  formulas: `GameEngine::GetPlayerFormulas()` = `mem::map<u32 objectId, u32>` of live formula-item instances at
  `gGameEngine+0x36d60`; the crafter's `NpcCrafter::GetRecipes()` = record paths (instantiate, keep if
  `IsBluePrintValid()`); `GameEngine::GetAwakenedRecipes()` when `LoadAwakenedRecipes()`; player formulas +
  the default list unless `RestrictsRecipes()`.
- Per formula: `GetArtifact()` = the object id of the unrolled template result item (tooltip = its own
  `GetUIDisplayText`, virtual); `GetReagentNId()` (1..7, 0 = unused; the slot is a LIST of acceptable records,
  the id is a private template item), `GetReagentNDisplayName(false)`, need = `GetReagentNQuantityForFormula()`,
  have = `GetReagentNCount(Character const&, bool)` (bags + materials + both stashes, NOT equipped);
  `reagentBase` is the big left slot; cost = `GetCreationCost(player)`; **"[N]" = `GetMaximumCraftable(player)`**
  (min over merged reagents of have/need, capped by money/cost; virtual vt+0x690). Row colour =
  `GameEngine::GetItemColorText(Item::GetDropClassification(result))`; the group = the result's
  `Item::GetItemType()` -> `tagCrafting*` (26 headers, table in re_crafting_exe.md).
- Craft = `ControllerCharacter::SendCreateArtifactCmd(CreateArtifactConfigInfo const&)` (0x2c0+ bytes: quantity,
  the result's `ItemReplicaInfo` incl. the rolled enhancement affix, 7 x {record, count}, cost, give-as flags).
  **It validates nothing** -- no reagent, money or bag check; bits are clamped at 0 and partial reagents are
  taken. Every guard is the window's (`GetMaximumCraftable >= 1`, `IsBluePrintValid`, not already crafting).

## Window (exe; **live**: vtable exe+0x31cf38, panel vtable exe+0x316140, ids, category, rows, flags)
- `CraftingWindow` = `InGameUI+0x3aa80`, ctor exe+0x269e80, size 0x51a0, visible byte `+0x68`. Frame only:
  NPC name text `+0x1c0`, subtitle `+0x2b8`, five tab Buttons in a RADIO registry `+0x4a0` (PressChild
  exe+0x12b330), close `+0x1a10`, `+0x98` player id, `+0x9c` crafter NPC id (vt+0xf0 SetCrafter, RTTI-checked).
  `+0x19a8` = current category, `+0x19b0` = the tab order vector **6, 2, 3, 1, 4** = Relics / Melee / Ranged /
  Armor / Accessories+Consumables (`tagCraftTab{Artifact,Melee,Ranged,Armor,Misc}A`, `...B` = description).
  Built from `records/ui/inventor/inventor_mastertable.dbr` (shared with the enchanter).
- **The crafting panel** is by value at `window+0x1e40` (size 0x3360, ctor exe+0x196220): `+0x60` selected
  formula id (rewritten from the list box every Update -- select THROUGH the list box), list box `+0x2fa0`,
  **rows `vector<Row>` at panel+0x3070/+0x3078, stride 0xd0**: `+0x00` u16 text (with `{^g}` colour codes),
  `+0x58` selected, `+0x5d` group expanded, `+0x5e` "new blueprint" badge, `+0x64` formula object id (0 =
  header). Reagent slots' have/need text = `tagCraftingQuantity` "{%d0}/{%d1}" (`tagCraftingLargeQuantity`
  "99+/..."), short = red style + slot byte `+0x61`. Combine button `panel+0x1ea8`, **enabled iff
  `panel+0x2129 == 0`** (exe+0x19eb80: selected, all slots satisfied, `panel+0x2f72 == 0` money ok,
  `IsBluePrintValid`, not creating). Press = `registry(panel+0x1e60)->vt[0x80](registry, panel+0x1ea8, true)`
  -> `DoCombine` exe+0x19e260 -> `SendCreateArtifactCmd`. Select = clear the old row's `+0x58`, then
  `exe+0x1f9f00(listbox, formulaId)` (SelectByData); current index `exe+0x1f9dd0`. The result slot is an item
  box: `SetItem(GetArtifact())`; the search box filters on the lowercased full summary text (name + stats).

## What was built (2026-08-29, verified live: a Ranger's Ribbon crafted through the screen)
- `src/screens/crafting.cpp` (`WindowScreen` over `kCrafting`, layer 14): tab strip = the five categories
  (`tagCraftTab*A`, landing presses the game's tab button via the radio registry; the game's current category is
  the truth, so a real click on a tab is followed), then the crafter's name and the rows from the window's list
  box in the game's order -- headers as lines that start a REGION (Shift+Up / Shift+Down jump group to group, decided with the user: no expand/collapse), recipes as "Name, can make N, cost iron bits, need x reagent, ..."
  (`gameapi::formula_info`: `GetMaximumCraftable`, `GetCreationCost`, `GetReagent{Base,1..6}{Id,DisplayName,
  QuantityForFormula,Count}`). Enter: N >= 1 -> `exe_ui::crafting_craft` = `ListBox::SelectByData` (signature-checked)
  + the panel's selected id and Combine-enabled byte written by us (what its next Update would derive from the same
  inputs; the gate `GetMaximumCraftable >= 1` was applied first) + the window's own Combine through its registry --
  all synchronous, no tick counting (the user's rule). **The finished item announces itself from a hook on
  `Player::GiveArtifactToCharacter(Item*)`** (attached in dllmain with the combat hooks; the screen sets the listener
  while focused): "crafted, <the rolled item's tooltip>" -- suffix and smith bonus included, not the template's ranges; N == 0 -> "missing 2 Aether Crystal,
  1 Bristly Fur, 4095 iron bits" (need - have per short reagent, cost - money). Space / Ctrl+Space = the
  result template's tooltip. Escape = Show(false).
- `src/exe_ui.cpp` crafting_*: rows, tab, press_tab, npc_name, selected, select, combine(_enabled), dump.
  `src/gameapi_crafting.cpp`: `formula_info`, `is_formula`, `dump_formula`. Dev: `/crafting`
  (`?formula= ?select= ?tab= ?combine=1`).
- Observed: the crafted item went straight onto the empty Medal slot (the game auto-equips like a pickup) with a
  rolled suffix ("of Scorched Ends"); reagents and bits were taken; the row's live value re-read "can make 0".
- The smith's bonus line (under his name): his `enhancementTag` lines localized, then one line per entry of his
  `enhancementTable` rendered by the GAME (`ObjectManager::CreateObjectFromFile` -> `LootRandomizerTable::GetAllEntries`
  -> `AttributeRange::LoadAffix` + `CreateText`, all exports; the table object is destroyed after) -- live: "Practiced
  Skill, Angrim uses his many years..., Crafted Weapons, Armor and Accessories are imbued with one of the following
  properties:, 3-7% Pierce Resistance, Increases Armor by 2-4%, +2-4% Physique". `gameapi::crafter_bonus`.
- Not done: the search box, the "new blueprint" badge, expand/collapse (all rows
  are read whether or not the game shows the group expanded).

## Getting a crafter for testing (the test character has not resolved "Tale of Two Blacksmiths")
The blacksmith NPCs (`records/creatures/npcs/merchants/blacksmitha01.dbr` Angrim, `blacksmitha02.dbr` Duncan)
only exist once the Devil's Crossing script swaps them in on a `BLACKSMITH_SERVICE*` token. Offline route used
2026-08-29 (Lua through `/lua`, the same calls the game's `TokenStateBasedObjectSwap` makes):
`local o = Entity.Create("records/creatures/npcs/merchants/blacksmitha01.dbr"); o:SetCoords(Entity.Get(<anyNpcId>):GetCoords())`
(`Character.Create(dbr, 0, origin)` and `Entity.Create(dbr, coords)` do NOT place the object). The Burrwitch
Outskirts refugee camp is at world (-466, 9, -909) next to the Outskirts riftgate (-496, -909); chunk
centres come from `tools/gdmap.mapfile.WorldMap().regions` (`location == "riftgatemap1a_burrwitchoutskirts"`),
`/teleport?check=1` polling loads the chunk. Review-lock the crafter (`/lock?id=`) and `/jkey` opens the
window natively (walks up, opens crafting + inventory). Spawned NPCs stand on top of each other -- move one
with `SetCoords` of a nearby prop's coords before clicking. Dev: `/peek`; `/lootfilter`-style routes for
crafting are not written yet.
