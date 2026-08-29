# Evade (Space) — how it aims

The player presses Space to evade (a dodge/dash). This documents how the game aims it, the option that
controls it, and what the mod does (nothing, so far).

## What it is (static RE, Game.dll v1.3.0.8)
- A real skill: `records/skills/default/defaultevade.dbr`, `Class = Skill_Evade`, a **default skill** (like the
  basic attack), triggered by a hardcoded hotslot (`HotSlotOptionEvade`), not a quickbar slot.
- Params: `distanceProfile = Long` (~15 units), `maxMoveRatio 1.1` (~16.5u cap), `characterRunSpeedModifier
  250` (a fast dash along the navmesh, NOT a teleport), `skillCooldownTime 3.0`, `targetingMode Point`. A
  fixed-distance directional dash.

## The direction rule (`HotSlotOptionEvade::Activate`, Game.dll +0x263440, re-read 2026-08-28)
In order:
1. `target = [ControllerPlayer+0x440]` = the **mouse-repeat point** (`ControllerPlayer::SetMouseRepeatData`,
   NOT `SetTargetPoint`, which is +0x510): the world point under the cursor as written by the exe's per-frame
   mouse handler (exe+0x22384, `SetMouseRepeatData(pickedEntityId, point)`). The WASD movement path
   (exe+0x2c82f) resets it to a default (null-region) `WorldVec3` together with `SetCombatEnemy(0)`.
2. **"Evade To Cursor" branch**: if `GameEngine::GetInputMode() == 0` (keyboard+mouse, `gGameEngine+0x37718`)
   AND `Options::GetInt(IntName 0xd = movementType) != 2` (2 = Keyboard Only) AND
   `Options::GetBool(BoolName 0x4b = evadeFollowCursor)` → the cursor point from step 1 is used **as-is**;
   WASD is never consulted. A null-region point (`WorldVec3::GetRegion()` null) makes the evade do nothing.
3. Otherwise `dir` = the character's **facing** (the forward row of `Entity::GetCoords()`, WorldCoords+0x30),
   replaced by the controller movement vector `[+0x458]` when the movement length `[+0x464] > 0` (WASD held);
   `target = pos + dir * range` (range = the skill's vtable +0x248), then
   `NavManager::FindStraightMovePointOnSlopes` clamps it along the navmesh.
4. `ControllerPlayerState::RequestEvadeAction` (state vtable +0x270) with the point.

The enum ids were read off `Options::Options` (Engine.dll +0x180520): registrations are
`{name, id, type, default}`; `evadeFollowCursor` = id 0x4b type 3 (bool), `movementType` = id 0xd type 1 (int;
`tagMovementClassic` 0 / `tagMovementKeyboard` 1 / `tagMovementKeyboardOnly` 2).

The game's own option text (`tagGameplayOption36` / `tagGameplayDesc36`): "Evade To Cursor — Enable to have the
default Evade skill move the character towards the cursor. If disabled, the character will evade in the
direction they are facing. This setting is automatically toggled off when using Keyboard Only controls."

## What the player gets
- **Evade To Cursor ON (the default; `evadeFollowCursor = true`)**: Space dashes toward the cursor's world
  point. With a review target locked (and on screen) the mod's virtual cursor sits on it, so that is "toward the
  target"; with nothing locked it is wherever the real mouse last picked; and if the WASD path's reset was the
  last write in the frame, nothing happens. **Holding WASD does not steer it.** This was the user's "WASD +
  Space does not always work" (2026-08-28): it never did, it only coincided.
- **Evade To Cursor OFF**: WASD held → dash in the movement direction; standing → dash the way the character
  faces (which is toward the target while attacking it, otherwise the last movement direction). This is the
  behaviour the README documents. Set it in Options → Gameplay (through the mod's options screen).
- Do NOT use Keyboard Only (`movementType = 2`) to get the same effect: the same `!= 2` gate disables the
  per-frame cursor pick in the mouse handler (exe+0x21601 / +0x2236f) that the mod's J/I/locked-target scheme
  relies on.

The old version of this file claimed "standing still reads the cursor" and "WASD wins while moving"
unconditionally; both were wrong for the option-on case, and standing still is facing, not cursor, when it is off.

## If we ever want to force it
`Options::SetBool(0x4b, false)` is exported and `Activate` reads the option live each press, so the mod could
switch Evade To Cursor off on entering the world (the engine saves options.txt on exit). Not done: it silently
overrides a user setting; decided to let the player set it (2026-08-28, pending the user's live check).
`ControllerPlayer::EvadeAction(Character&, uint skillId, WorldVec3 const& point)` is exported (Game.dll
+0x14cc00) but is NOT on the live evade path — the exe calls the current state's `RequestEvadeAction`
(`ControllerPlayerState::DefaultRequestEvadeAction` +0x150630, state vtable +0x270) directly. A future override
would hook that and rewrite `point`. (A short-lived diagnostic hook on the EvadeAction forwarder was tried and
removed — it never fired, being off the live path.)
