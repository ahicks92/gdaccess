# Movement skills: sources, validation, and what the mod can predict (static RE 2026-09-14, NOT verified live)

Question that started it: "piano" builds lean on teleports; the mod has no way to aim one. What are the sources,
do they need line of sight, and can we tell beforehand whether a press will move the character?

Short answer: **no movement skill checks line of sight, anywhere.** The only gate is the navmesh, and every probe the
game runs is an export we can call with the same arguments before firing. Nothing is verified in-game yet; all
offsets are Game.dll v1.3.0.8 RVAs unless marked Engine.dll. Two Opus traces (point-targeted, enemy-targeted)
plus a database survey; the scratch scripts were not kept.

## 1. Where teleports come from (database survey, base + GDX1 + GDX2)

Six skill templates move the character. They split by what they aim at, which is the split that matters to us:

**Enemy-targeted** (`Skill_AttackWeaponBlink`, `Skill_AttackWeaponCharge`; `targetingMode` absent, target = an entity id):
- Shadow Strike (Nightblade, base) -- Blink. Blitz (Soldier, base) -- Charge.
- Item-granted: Chaos Strike (component), Spectral Strike / Dread Strike (Alkamos items), Blazing Charge (GDX1 relic).
- GDX2 medal runes: 5 strike runes (Vanish, Chaos Strike, Sudden Strike, Vampiric Shadows, Dark Desires), 5 charge runes
  (Charge, Scorpion Strike, Soul Strike, Stormtitan's Charge, Blazing Charge).

**Point-targeted** (`targetingMode = Point`, `waveDistance` = the reach in units, `distanceProfile = Long`):
- Vire's Might (Oathkeeper, GDX2) -- `Skill_AttackPathCharge`, 12 u. The only ground-targeted class skill.
- GDX2 medal runes (`records/skills/itemskillsgdx2/runes/`, one rune = one augment on the medal slot):
  12 teleports (`Skill_AttackRadiusTeleport`: Rift Tear, Dreeg's Vector x3 tiers, Arcane/Burning/Bladed/Astral Rift,
  Disorder, Nadaan's Strike, Ultos' Arrival, Displacement; 13-14 u, cd 3.2-5.5 s), 12 leaps (`Skill_AttackRadiusLeap`:
  Leap, Korvan Swiftness x3, Seismic Leap, Stormclap, Manticore Pounce, Upon Rylok Wings, Ulzuin's Fall, Eldritch
  Instability, Dread's Wake, Dreeg's Wounds; 16-18 u), 10 rushes (`Skill_AttackPathCharge`: Rush, Rahn's Might x3,
  Wendigo Rush, Ramming Speed, Incorporeal Winds, Amatok's Breath, Will of the Fallen Kings, Violent Delights; 13-16 u),
  10 disengages (`Skill_AttackRadiusDisengage`: Disengage, Outfox, Phoenix Rise, Ghastly Retreat, Assassin's Evasion,
  Typhoon, ...; 15 u, move AWAY from the point).
- Arcanist has no teleport. Every rune tooltip says "towards a target location", never "to" -- that wording is literal (s.3).
- `Skill_AttackSpellTeleport(Self)` exists but only bosses (Valdaran, Bysmiel) and sandbox records use it.

Runtime facts: `Skill::GetMoveType()` = `*(int*)(skill+0xc4)`, set as a constant per concrete ctor: 1 default (Blink and
Charge never write it), 2 PathCharge, 3 Leap, 4 Teleport, 5 Disengage, 6 `Skill_AttackRadiusSpin`, 7 `Skill_Evade`.
`Skill::GetMoveTypeStateString()` (0x47c500): 2 -> "ChargeToUseSkill", 3/5 -> "JumpToUseSkill", 4 -> "UseSkill",
6 -> "MoveAndUseSkill", 7 -> "EvadeTo", else "MoveToUseSkill". `Skill::MoveToPointSkill()` true for 2,3,4,5,7.
**All four point classes set `SkillActivated::GetTargetType()` (`+0x5c0`) = 4** -- a value `world::skill_aim` does
not know (docs/skills-targeting.md lists 0..3), so the hotbar reads misdescribe them today.

## 2. The one request path: `ControllerPlayerState::DefaultRequestSkillAction` (0x1508a0..0x151675)

Every player skill press goes through it (`ControllerPlayer::SendSkillAction` 0x14cb60 -> state vt+0x260). In order:
1. Skill must be-a `SkillActivated`, else return false.
2. Gamepad aim block (0x1509fc..0x150c69): the only navmesh call the request makes on its own,
   `NavManager::FindStraightMovePointOnSlopes` at 0x150c4f. Skipped whenever a target id is present and gated on
   `Options::GetInt(0xd)` (movementType) plus `MoveToPointSkill()`. Mouse/keyboard play with a target never enters it.
3. Target by `GetTargetType()`: 2 (offensive) takes the combat ENEMY `[controller+0x468]` (`SetCombatEnemy` writes it;
   corrected 2026-09-23, this line had enemy and ally swapped); if 0 and the combat ALLY `+0x46c` (`SetCombatAlly`) is
   set but `skill->vt[0x5a0]()` false -> return false. 3 uses `+0x46c`, 1 the caster, 4 `+0x468`. The id argument is
   overwritten by these (kept only for type 0).
4. Availability `*(int*)(skill+0xb0)`: 2 = not enough mana (`Player::PlayNotEnoughManaVox`), any nonzero -> false.
5. `skill->vt[0x4b0]` = `GetValidTarget`. Enemy-targeted: `SkillActivatedWeapon::GetValidTarget` (0x505e20) ->
   `Skill::GetValidMeleeTarget` (0x4829a0) -> `Skill::ValidateEnemy` (0x4827b0): Destructible ok, else must be a
   Character passing faction/PvP. No navmesh, no LOS, no range. Point-targeted: the four classes override it with the
   folded `return true` stub (0x16d00; Disengage's 0x4827a0 also zeroes the entity id) -- they deliberately SKIP the
   base `SkillActivatedSpell::GetValidTarget` (0x493f15..) which does IsPointOnPathMesh -> `Skill::GetPointInLOS`
   -> FindStraightMovePointOnSlopes for projectile/pet/drop skills.
6. Destination = `Character::GetMoveToPoint` (s.3). Region-less result -> return false (0x150f3a/0x150f78/0x15106c).
7. Close enough? `|target - me| <= Character::GetTargetDistance(myId, targetId, skillId)` (0x5dd70 = both collision radii
   (vt+0x1c8) + `Skill::GetRange()`, the distanceProfile value) + `GetSkillUseTolerance()` (0.5). For an is-a
   `Skill_AttackWeaponCharge` "must move first" = !closeEnough (0x151222..0x151257); close enough -> straight to
   `SetState("UseSkill")`, a swing on the spot, no charge.
8. Move branch (0x151315): region-less point -> return TRUE, nothing happens. Then **`Player::CanMoveTo` (vt+0x550,
   0x3ba2e0) = `CharacterMovementManager::FindPath` (0x8dfd0) -> `NavManager::FindPath` == 0`; on failure the request is
   dropped silently (0x151548): no state change, no cooldown, no mana, no message.** Else `SetState(stateName, {targetId,
   targetId, skillObjectId, movePoint})`, where "MoveToUseSkill" becomes "ChargeToUseSkill" for a Charge/Blink (0x151468).

## 3. Destination: `Character::GetMoveToPoint(uint targetId, uint skillId, WorldVec3 const&) const` -> WorldVec3 (0x5e880, exported; hidden return pointer 2nd)

The single place a point-targeted destination is computed. `DefaultRequestSkillAction` calls it with targetId 0 for a
pure point and hands the result to the controller state unchanged.
- moveType 5 (Disengage, 0x5eb65): direction = unit(target - me) NEGATED, scaled by `range`; then
  `NavManager::FindStraightMovePointOnSlopes(me, awayPoint, out, false)` (0x5ebe6), re-extended by `range`.
- moveType 2/3/4 (0x5ed86): `rangePoint = me + unit(target - me) * range`; if `|target - me| <= range` keep the raw
  point else use `rangePoint` (0x5edd7) -- **a hard clamp along the straight line at the skill's range.**
- then `CharacterMovementManager::FindPath(goal, ..., &out, ...)` (0x5eead); 0 -> return `out` (a REACHABLE goal, whatever the
  pathfinder resolves, not the raw cursor point).
- on failure (0x5eef0): `NavManager::FindStraightMovePoint(me, goal, straightPt)`; NavResult == 1 -> re-path to
  `straightPt` (0x5ef32); success -> that point.
- both fail (0x5ef3f): **return the character's own position** -> the skill fires in place. Never a refusal.
- `range` = a float at `+0x70` of the struct `[skill vt+0x2e8]` fills (waveDistance is the obvious backing field --
  UNCONFIRMED; `Skill::GetRange()` vt+0x240 = the distanceProfile value is the competitor). moveType 6 uses
  `Skill::GetGamepadRange()` instead.
- entity-target branch (0x5ec8d, what Blink/Charge get): `CharacterMovementManager::GetPointAwayFromGoal` with a
  stand-off = both bounding radii.

Real geometric LOS exists (`Skill::IsTargetInLOS` vt+0x4d0 -> `World::CheckLOS(WorldRay const&, float)`, Engine.dll;
`Skill::GetPointInLOS` 0x483700 -> `World::GetAllIntersections`) and is called only from the base fix-up in s.2 step 5
and from cone/beam/wave/spawn-pet skills. A full scan of `World::GetIntersection` / `Actor::GetIntersection` call
sites in Game.dll finds none on any movement path.

## 4. Transport per class (after the request)

- **Teleport** (moveType 4 -> "UseSkill"): `ControllerPlayerStateUseSkill::OnBegin` (0x155ec0, point at
  `GetCurrentStateData()+0x10`) -> `Skill_AttackRadiusTeleport::ActivateNow` (0x4c0080: builds a `WorldCoords` from the
  point as the first 0x40 bytes of the AttackInfo, `GameEngine::GetTargetsInRadius`, hands off to the combat manager)
  -> `TargetResult` (0x4c0390), whose first call `caster->vt[0x428](attackInfo)` = `Character::TeleportToLocation`
  (0x5b090: `ControllerCharacter::Teleport` -> NetPacket + `WorldVec3::TranslateToFloor` + `NavManager::ResetObject`,
  or PutOnFloor + `World::SetCoords` + ResetObject). No mesh test. The per-level float table read at 0x4c04bb is the
  3rd arg of `CreateVisualEffect` (FX), not the destination.
- **Leap** (3 -> "JumpToUseSkill"): `OnBegin` (0x154f60) allocates a `JumpAttackAction` (vftable 0x6b4288; destination
  at action+0x50, caster coords +0x88) -> `JumpAttackAction::Execute` (0x847c0) -> `Character::JumpToUseSkill` (0x6b780,
  exported): checks only `GetRegion(dest) != null` and that the animation started; PutOnFloor on start (`char+0xdd8`) and
  destination (`char+0xdf0`), speed scale `char+0xdcc` from the animation's root motion. No probe. Landing damage in
  `ActivateNow` (0x4b3450) / `TargetResult` (0x4b3740). Mid-flight re-probing: not traced (UNCONFIRMED; `Character::
  PhysicsUpdate` calls FindStraightMovePoint at 0x65902 but that reads as the generic ground clamp).
- **Rush / Vire's Might** (2 -> "ChargeToUseSkill"): `OnBegin` (0x154b60) = `ControllerAI::MoveTo(point, targetId,
  skillId, AnimationSet_Type 7, 1.0f)` (0x111040) -- **an ordinary pathfound navmesh move at the charge animation; it goes
  ROUND obstacles.** `Skill_AttackPathCharge::Update` (0x4a45d0) does the along-the-way damage, `ActivateNow` (0x4a3fc0)
  the end damage, `SwipeAction` is an empty stub.
- **Disengage** (5 -> "JumpToUseSkill"): the leap machinery with the moveType-5 destination; `ReverseRotateDirection` =
  `return true` (face forward while leaping back); `ActivateNow` (0x4b1590) damages around where you land.
- **Blink and Charge** (Shadow Strike, Blitz, all strike/charge runes): **Shadow Strike is not a teleport.**
  `Skill_AttackWeaponBlink` derives from `Skill_AttackWeaponCharge` (ctor 0x4cf6f0 calls the Charge ctor 0x4cf890;
  classInfo 0xa27130 parent 0xa27190; vtables differ only in `Cancel` +0x490, `WarmUpStart` +0x6b0, `WarmUpEnd` +0x6b8).
  The difference is `Actor::SetVisibility(false)` in `Blink::WarmUpStart` (0x4cf740) and `true` in `WarmUpEnd`/`Cancel`:
  **an invisible run along the navmesh.** Both enter `ControllerPlayerStateChargeToUseSkill`: `OnBegin` -> `MoveTo`
  (mode 7) -> `MoveToAction::Execute` (0x829d0) -> `Character::MoveTo` (0x5f780) + `WarmUpStart` (Charge's 0x4cfc70
  stores the start in `skill+0x618` and sets `Character+0xdc8 = 1`, the "charging" byte). `OnUpdate` (0x154bf0) only
  checks target dead (`IsAlive` vt+0x460) / Destructible broken (`+0x66c`) -> "Idle". Arrival:
  `ControllerPlayerStateMoveToUseSkill::EndOfPathReached` (0x154820) -> "UseSkill" -> `WarmUpEnd` then
  `Charge::ActivateNow` (0x4cf940, pure combat). Range never refuses: Blitz (Melee profile) nearly always charges,
  Shadow Strike (Moderate, 9 u) swings in place inside ~9 u + radii + 0.5. `MaxSkillDistance` (0x4cfe70, "travelled
  more than `maxDistanceBuffer` (`skill+0x610`, DBR field) past the target") is inert for the player: blitz1.dbr and
  shadowstrike.dbr carry no maxDistanceBuffer. `Player::CanMoveTo` passes `*(float*)(player+0x4bf0)` to FindPath --
  likely a max path length (UNCONFIRMED).

## 5. Failure signals

- Refused (request returns false, 0x151644): not a SkillActivated; type-2 with no cursor target; ValidateEnemy false
  (enemy-targeted only); unavailable / no mana; region-less move point; the repeat gate vt+0x4a8.
- Silently dropped (returns true, nothing observable): `CanMoveTo` path failure or a region-less point in the move
  branch. This is "the key did nothing".
- Point skills with no usable point: NOT a failure -- the skill runs at your own position (s.3).
- Mid-charge: target dies -> "Idle" (`ChargeToUseSkill::OnUpdate`). Path breaks -> `ControllerAI::PathFailed` (0x110540)
  -> `ControllerPlayerStateMoveToUseSkill::PathFailed` (0x154920) -> `skill->vt[0x558]` `OnPathFailed`; Charge's is the
  folded `return true` (0x16d00) -> `SetState("UseSkill", {target 0, skillId, me + facing})`: **the charge degrades into a
  swing at empty air one unit ahead, target id 0**, and still pays its cost. `Charge::WarmUpEnd` (0x4cfcc0) with a dead
  target -> `Cancel` (Blink's restores visibility; both clear `Character+0xdc8` and `Skill::DisableWeaponTrails`).
  `StopSkill` is a one-line jump to `Cancel`. `ControllerPlayerStateJumpToUseSkill::PathFailed` (0x1553c0) -> "Idle".
- Nothing to hook: `OnPathFailed`, Leap's `OnPathFailed`, `GetValidTarget` are the 202-/525-way COMDAT stubs hooks.cpp
  refuses. Unique bodies safe to hook if ever wanted: `Blink::Cancel` 0x4cf810, `Blink::WarmUpStart` 0x4cf740,
  `Charge::WarmUpEnd` 0x4cfcc0, `Charge::ActivateNow` 0x4cf940, `Skill_AttackRadiusTeleport::TargetResult` 0x4c0390.
- Detecting after the fact: the controller state name (`ControllerAI::SetState` 0x110a50 takes the string; state data via
  `ControllerAI::GetCurrentStateData` 0x112700: +4 targetId, +8 skill object id, +0x10 WorldVec3). ChargeToUseSkill never
  appearing = refused/dropped; -> Idle = target died; -> UseSkill with target id 0 = swung at nothing. `Character+0xdc8`
  = charge in progress; `Character::IsJumping()` (vt+0x458 action state) for a leap in flight; else sample the player's
  position before and ~10 frames after.

## 6. What the mod can call BEFORE firing (all exports, game thread)

1. Enemy-targeted: `Skill::ValidateEnemy(skill, Character& caster, uint targetId)` (0x4827b0) -- the exact gate.
2. `Character::GetMoveToPoint(targetId, skillId, point)` (0x5e880) -- the exact destination. Point skills: targetId 0,
   the cursor point; result == the player's position means "will not move you". Enemy skills: the stand-off point.
   Re-check `WorldVec3::GetRegion(result) != null` yourself (the one refusal it does not include).
3. `Player::CanMoveTo(WorldVec3 const& dest, float, void*)` (vt+0x550, 0x3ba2e0) -- the exact reachability gate for
   Blink/Charge (a full corridor query with portals, NOT the IsPointOnPathMesh trap). Feed it the point from 2.
4. Charge or swing: `Character::GetTargetDistance(myId, targetId, skillId)` (0x5dd70) + 0.5 vs `World::GetDistance`;
   or `Skill::IsTargetInRange` (0x482fd0). Display range: `Skill::GetRange` (vt+0x240) / `GetRangeProfile` (vt+0x238).
5. Classification: `Skill::GetMoveType()` (vt+0x258), `MoveToPointSkill()` (vt+0x268), `GetMoveTypeStateString()`
   (vt+0x260); is-a `Skill_AttackWeaponCharge::classInfo` (0xa27190) catches Blink too (`Blink::classInfo` 0xa27130 to
   say "vanish"). Point-movement skills: `GetTargetType()` == 4.
6. Raw probes if wanted: `NavManager::FindStraightMovePointOnSlopes(from, to, out, bool)` (void),
   `FindStraightMovePoint(from, to, out)` -> NavResult where the game treats **== 1** as usable,
   `CharacterMovementManager::FindPath(goal, float, float, float, float, WorldVec3* out, float*, float, bool)` -> 0 = ok.
   Do not bother with LOS.

## 7. Layouts measured along the way

- `Skill`: +0xb0 availability (0 usable, 2 no mana), +0xc4 moveType, +0x5c0 targetType, +0x5f0 ranged flag, +0x610
  maxDistanceBuffer, +0x618 charge-start WorldVec3 / warm-up flag (+0x618/+0x619 on the radius classes), +0x66c PathCharge
  "still charging" flag.
- `Skill` vtable: +0x238 GetRangeProfile, +0x240 GetRange, +0x248 GetGamepadRange, +0x258 GetMoveType, +0x260
  GetMoveTypeStateString, +0x268 MoveToPointSkill, +0x2e8 skill-data fill, +0x490 Cancel, +0x498 StopSkill, +0x4b0
  GetValidTarget, +0x4b8 IsTargetInRange, +0x4d0 IsTargetInLOS, +0x558 OnPathFailed, +0x578 GetSkillProfile, +0x620
  MaxSkillDistance, +0x6b0 WarmUpStart, +0x6b8 WarmUpEnd, +0x6d0 GetTargetType, +0x6d8 ActivateNow, +0x720 CreateVisualEffect.
- `Character`/`Player` vtable: +0x1c8 collision radius, +0x1f8 `Actor::SetVisibility`, +0x428 TeleportToLocation, +0x458
  GetActionState, +0x460 IsAlive, +0x550 CanMoveTo. `Character+0xdc0` = `CharacterMovementManager*`, +0xdc8 charging byte,
  +0xdcc jump speed scale, +0xdd8 jump start, +0xdf0 jump destination.
- `ControllerPlayer`: +0x430 repeat flag, +0x458..+0x464 joystick dir + magnitude, +0x468 combat enemy id, +0x46c combat
  ally id (corrected 2026-09-23), +0x43c / +0x440 mouse repeat data (id, WorldVec3: what a hot slot fires at), +0x510 stored move/target WorldVec3. Player-state vtable: +0x10 CloseEnoughToUseSkill, +0x18 MaxSkillDistance,
  +0x70 EndOfPathReached, +0x78 PathFailed, +0x220 OnBegin, +0x258 GetSkillUseTolerance (0.5), +0x260 RequestSkillAction.
- `GetSkillProfile()+0xe80` is compared against 1 and 3 in the base fix-up -- almost certainly targetingMode (UNCONFIRMED).

## 8. Design notes (nothing built; decided 2026-09-14 to keep this and not act yet)

- Enemy-targeted movement (Shadow Strike, Blitz, the strike/charge runes) probably already works through the review
  lock: the skill reads the cursor target like any attack. Untested. The silent `CanMoveTo` drop is the case to speak
  ("no path") instead of leaving a dead key; steps 1-3 above give it before the press.
- Point-targeted movement (44 runes + Vire's Might) needs an aim: a direction-and-distance point, or the exits lock
  (`world::lock_point`) so a rune fires at the opening the cursor is parked on. `GetMoveToPoint` then tells exactly where
  it lands, so the announcement can be honest ("teleport 9 units north" vs "would not move you").
- `world::skill_aim` must learn targetType 4 first, or the hotbar keeps calling these skills passive.
