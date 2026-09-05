# The Inventor's window (mapped and BUILT 2026-08-30, verified live)

The window an Inventor NPC (`NpcEnchanter`, `records/creatures/npcs/merchants/inventora01.dbr`, "Darlet") opens:
the exe's **enchanter** window, `InGameUI+0x30dd8`. Static RE in `docs/re_inventor_exe.md`; everything below
marked **live** was checked on the running game with a Lua-spawned Darlet at the Devil's Crossing spawn.

Not this window: "Transmute" in this game is the Illusionist (item illusions, `NpcTransmuter`, `+0x85378`);
the Blacksmith's crafting window is `docs/crafting.md`.

## What the player does (sighted)
Talk to the Inventor: the window opens on the left with the inventory beside it. Two icon tabs in a base-game
install (the Ashes of Malmouth **Convert** and Forgotten Gods **Reroll** tabs are built by the same class but
their records are expansion-only; the game greys them unless the expansion is loaded, so we list them only
when the game enables them):
- **Salvage** ("Salvage" / `tagDividerTab01`): drag an equipment item that carries a **component** or an
  **augment** into the chamber. "Salvage Cost: N" = `trunc(0.05 x the item's value)` (`enchanterRecoveryFactor`
  in `records/game/gameengine.dbr` x `ItemEquipment::GetItemCost(false)`), red when unaffordable. Three buttons:
  **Keep Item** (the component is destroyed and any augments stripped; a soulbound status that came from an
  augment goes too), **Keep Add-on** (you get the component back, the item is destroyed), **Remove Augment**
  (the augment is destroyed). Each opens a Yes/No confirm box (`tagDividingKeepItemWarning` /
  `...KeepComponentWarning` / `...RemoveAugmentWarning`); Yes charges the bits and runs it. What you kept is put
  BACK INTO THE CHAMBER (the component after Keep Add-on, the item otherwise) and dragged out.
- **Dismantle** (`tagDividerTab02`): greyed ("The Inventor has not yet learned this ability") until the
  character has the quest token `DISMANTLING_UNLOCKED` (`gameengine.dbr dismantleToken`;
  `GameEngine::MainPlayerCanUseDismantle`). Chamber accepts equipment **above common** quality. Costs 1
  **Dynamite** (`records/items/questitems/quest_dynamite.dbr`, counted by `Player::GetCurrentDynamite`) plus
  iron bits = `itemLevel*10+150` (`dismantle_table.dbr dismantleCost`) + the salvage cost when a component is
  attached (the exe adds it). "Total Dynamite: N" shows the stock. The Dismantle button confirms only for
  epic/legendary items (`tagDismantleDestroyItemWarning`) or component-bearing ones
  (`tagDismantleDestroyItemComponentWarning` -- the component is kept); otherwise it acts at once. Results land
  in the panel's TWO output boxes: `Scrap` (`scrapmetal.dbr`, count by `itemWeights`) and a bonus component
  rolled from a rarity-weighted loot table (`mt_comp_dismantling_a01..e01`), or the kept component. The player
  drags them out.
- The chamber owns its item: dropping one in REMOVES it from the inventory (`PlayerInventoryCtrl::RemoveItem`),
  clicking the box (or closing the window / switching panel) gives it back (`ControllerPlayer::GiveItemToPlayer`,
  `SendDropItemRandom` if the bag is full).

## What the mod does (`src/screens/inventor.cpp`, layer 14 like crafting; **live**)
Item-first, not chamber-first (the chamber holds one item; every button acts on it, so making the player
operate it is three extra keys per action):
- Tabs = the game's tab buttons (pressed through the window's radio registry; the game's listener shows/hides
  the panels). A greyed tab reads "not learned" as its value and its info text (`tagDividerRoll02`) instead
  of items.
- **Salvage**: one row per bag item the chamber would accept -- "name, with component / with augment, salvage
  cost N iron bits" (+ "too expensive"). Enter opens the shared picker of the actions that apply (the game's
  own button captions; Keep Item / Keep Add-on for a component, Remove Augment for an augment; Space on a
  picker row = the game's warning text). Picking one puts the item in the chamber, presses the button through
  the panel's registry, the game's confirm box comes up (answered by the `message_box` screen), and afterwards
  the screen's `on_update` reads the outcome: chamber empty -> done, what you kept is taken out of the chamber
  into the bag and named ("salvaged, Serrated Spike"; "augment removed, <item>"); item still there -> the
  player said No -> back into the bag, "cancelled".
- **Dismantle**: a "N dynamite, N iron bits" line, then one row per above-common bag item -- "name[, with
  component], dismantle cost N iron bits" (+ "too expensive" / "no dynamite"). Enter dismantles (confirm box
  when the game asks); the two results are taken out of the output boxes into the bag and named
  ("dismantled, Scrap (4), Serrated Spike").
- Space = the item's tooltip, Ctrl+Space the details. Escape = the window's Show(false) (returns anything
  in a chamber).
- Verified live: Keep Add-on on a component-bearing sword (105 bits, the Razor destroyed, the Spike back in
  the bag); Keep Item answered No (item back, "cancelled", focus kept); Dismantle of a rare sword with a
  component (541 bits + 1 Dynamite, Scrap x4 + the kept Spike). The game's own cost texts matched the
  computed rows in every case (105, 191, 541).
- Not modelled: Convert / Reroll (expansion-only; would need their panels mapped), the "too expensive"
  state's red text (we compare against `money()` ourselves), and salvaging an EQUIPPED item (the game's chamber
  only takes bag items -- unequip first, as a sighted player does).

## Model (Game.dll, all exported)
- Eligibility: `ItemEquipment::HasRelic` (component), `ItemEquipment::HasEnchantment` (augment), after an is-a
  `ItemEquipment` check; `Item::GetItemClassification(true)` (virtual, vt+0x5b0) > 0 = above common;
  `ItemEquipment::GetRelic` names the component.
- Prices: `Item::GetItemCost(bool)` (virtual, vt+0x598; `ItemEquipment` overrides), `Item::GetItemLevel`,
  `Character::GetCurrentMoney`, `Player::GetCurrentDynamite`; gate `GameEngine::MainPlayerCanUseDismantle`
  (= `Player::HasToken(dismantleToken)`).
- Commands (sent by the exe's panels, executed by `Enchanter*ConfigCmd::Execute`):
  `ControllerCharacter::SendEnchanterRecoveryCmd(itemId, newObjectId, keepRelic, removeAugment)` -- Keep Add-on
  = (1,0), Keep Item = (0,0), Remove Augment = (0,1); the Execute recomputes the cost, charges it, removes the
  relic / enchantment, destroys or keeps the item (`TakeItemFromCharacter`) and hands the result to the window
  (`GameEngine::GiveRecoveredItemToEnchanterWindow`). `SendEnchanterDismantleCmd(itemId, scrapId, bonusId, ?)`
  after the exe rolled the results client-side (`Item::CreateItem` from the loot tables with
  `GameEngine::GetRandomSeed`); Execute charges bits + `Player::SubtractDynamite` and gives the results
  (`GiveDismantledItemToEnchanterWindow` / `...BonusItem...`). **Never send these directly** -- the panel's
  button is the gate (affordable, dynamite, eligible, the confirm); the mod presses the button.
- Chamber moves: `PlayerInventoryCtrl::RemoveItem(id, true)` on the way in, `ControllerPlayer::GiveItemToPlayer(id,
  false)` on the way out (the exe's own return helper exe+0x1ae9b0), plus the box's virtual `SetItem` (vt+0xa8).

## Getting an Inventor for testing
Spawn one like the blacksmith (docs/crafting.md): Lua through `/lua`,
`local o = Entity.Create("records/creatures/npcs/merchants/inventora01.dbr"); o:SetCoords(Entity.Get(<propId>):GetCoords())`
(a torch next to the spawn works), review-lock it (`/lock?id=`) and `/jkey` opens the window. Test items:
`Player.Get(<id>):GiveItem("records/items/gearweapons/swords1h/a01_sword002.dbr", 1, true)` (common),
`.../b007_sword.dbr` (rare), `records/items/materia/compa_serratedspike.dbr` (a component, attach with
`/inv?attach=<comp>&target=<item>`), `records/items/questitems/quest_dynamite.dbr`; the Dismantle tab needs
`p:GiveToken("DISMANTLING_UNLOCKED")` and a re-open of the window (the tab enable is computed in Show(true)).
Dev: `/inventor` (state + every bag item's eligibility and prices), `?tab=`, `?put=<id>`, `?take=0|1|2`,
`?press=keepitem|keepaddon|removeaugment|dismantle`.

## Timing lesson (2026-09-04, from a live session log)
The exe processes a confirm box's response on its NEXT update: Do* reads the item id from the chamber, clears the
box and sends the command only then. On the frame the box closes, the panel therefore still holds the item whatever
the answer was. The screen used to read that as "the player said No", speak "cancelled" and hand the item back
(`inventor_take`) while the game's salvage command was in flight -- the item was owned by the bag and the command at
once for a few frames, and both Keep Item runs of that session were mis-announced as cancelled (they had succeeded).
Now: a Yes given through our message box is known exactly (`exe_ui::last_dialog_answer`) and the screen waits for
the command (up to 180 frames); otherwise the item must stay in the chamber 20 frames after the box closed before
that means No. Built, not yet verified live. The `+0x10a8` pending byte read 0 during that window, so do not rely on it alone.
