# Picking up a ground item by call (static RE 2026-08-22, verified live)

Sighted players click an item's floating NAME LABEL (drawn only when the loot filter shows it or Alt is held); a
click on the model itself resolves no entity, and in keyboard-movement mode that is "attack here". So J on a
reviewed Item is not a click.

The label click ends in `GAME::ControllerPlayer::HandleActionFromMouse` (Game.dll 0x14ae60; exe call site
exe+0x22617) -> `ControllerPlayerState::SelectPrimaryAction` (0x14e970: an RTTI ladder over Item / Npc /
FixedActor) -> the controller's virtual `ItemAction(bool no_walk, bool unused, WorldVec3 const&, Item const*)`
(0x14be50, exported; NpcAction 0x14be80 / InteractAction 0x14beb0 are the siblings) -> state slot 0x1e8 =
`RequestItemAction` -> `ControllerPlayerState::DefaultRequestItemAction` (0x14f480):
- returns at once while `controller+0x430` (`IsCommandRepeated`) is set -- clear it with the exported
  `SetCommandRepeated(false)` first (our mouse hold leaves it on);
- requires `Entity::IsInWorld(item)`; the WorldVec3 argument is only a fallback, the item's own coords win;
- `MoveAndPickUpItem` (0x14f5a0): range = player radius + item radius + 1.0 (2.5 with the gamepad flag); in
  range -> `ControllerAI::SetState("PickupItem", {0, itemId, 0, _})`; out of range -> `Character::GetMoveToPoint`
  -> `SetState("MoveToItem", ...)`, whose EndOfPathReached sets "PickupItem". One call, the controller does the rest.
- `ControllerAIStateData` = {u32, u32 targetId, u32, pad, WorldVec3 @0x10}, size 0x28.
FixedActors (chests, doors, shrines): `InteractAction` -> `DefaultRequestInteractableAction` (0x14fcf0), range
adds `float [fixedActor+0x530]` (the record's interact range), states UseFixedItem / MoveToFixedItem. Npcs:
`NpcAction` -> `DefaultRequestNpcAction` (0x14f250), `player->vt[0x840](npcId, 3.0)` as the range test, states
MoveToNpc / TalkToNpc. The plain click already resolves those, so the mod keeps the click for them.

Mod: `world::mouse_key(1, held)` -- on the J press with an Item locked, `pickup_locked_item()` calls
SetCommandRepeated(false) + ItemAction(controller, false, false, coords, item) and swallows the hold; the dev
route `/jkey?down=1|0` presses J without the game seeing a J key. Verified: a Lua-spawned Gladius at 0 units
(PickupItem), the prison lore note from 16 units (MoveToItem -> PickupItem, 2 s).
