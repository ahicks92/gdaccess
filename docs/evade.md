# Evade (Space) — how it aims (decision: left as-is)

The player presses Space to evade (a dodge/dash). This documents how it aims and why we do NOT mod it.

## What it is (static RE, Game.dll v1.3.0.8)
- A real skill: `records/skills/default/defaultevade.dbr`, `Class = Skill_Evade`, a **default skill** (like the
  basic attack), triggered by a hardcoded hotslot (`HotSlotOptionEvade`), not a quickbar slot.
- Params: `distanceProfile = Long` (~15 units), `maxMoveRatio 1.1` (~16.5u cap), `characterRunSpeedModifier
  250` (a fast dash along the navmesh, NOT a teleport), `skillCooldownTime 3.0`, `targetingMode Point`. A
  fixed-distance directional dash.

## The direction rule
`HotSlotOptionEvade::Activate` (Game.dll +0x263440) computes `target = base + dir * range`, choosing `dir` by
the controller's movement length `[ControllerPlayer+0x464]`:
- **length > 0 (moving on WASD)** → `dir` = the controller movement vector (`[+0x458]`/`[+0x460]`), i.e. the
  WASD direction. Cursor-independent.
- **length == 0 (standing still)** → a fallback that reads the **cursor** (via the game's cursor query).
`Character::GetMoveToPoint` normalizes `(point - playerPos)` and rescales to the evade range, so the point is a
pure direction source.

## Why we leave it as-is (decided 2026-08-25, with the user)
The mod already parks the **virtual cursor** on the player's locked review target (while it is on-screen;
`world::lock_target`/`lock_point`). Combined with the direction rule above, evade lands on a scheme that is
actually good for a blind player, no mod required:
- **WASD held → dodge in the movement direction.**
- **Standing still → dodge toward the locked target** (the standing-still fallback reads the cursor, which the
  mod has parked on the target).
Confirmed by the user in play. Edge case: standing still with **nothing locked**, the cursor is the stale real
mouse position, so the dodge direction is arbitrary — but in combat a target is virtually always locked.

## If we ever do want to force it
`ControllerPlayer::EvadeAction(Character&, uint skillId, WorldVec3 const& point)` is exported (Game.dll
+0x14cc00) but is NOT on the live evade path — the exe calls the current state's `RequestEvadeAction`
(`ControllerPlayerState::DefaultRequestEvadeAction` +0x150630, state vtable +0x270) directly. A future override
would hook `DefaultRequestEvadeAction` (or the per-state `RequestEvadeAction`) and rewrite `point` to
`footCoords + desiredDir * range`. (A short-lived diagnostic hook on the EvadeAction forwarder was tried and
removed — it never fired, being off the live path.)
