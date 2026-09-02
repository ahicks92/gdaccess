# How the engine moves the player under WASD, and why it slides along walls

Static RE, 2026-09-01 (Opus agent trace, load-bearing constants re-read from the images by hand). No live
verification yet. Game 1.3.0.8, exe dump `build/GrimDawn.unpacked.bin`.

## 1. The command layer: an absolute point 1.25 units ahead, re-issued every frame

exe+0x2c5fe..0x2c708, once per frame while a movement key is held (`movementType` = `Options::GetInt(0xd)`
read at exe+0x2c563 / 0x2c5ab):

- exe+0x2c659 `ControllerPlayer::SetControllerDirection(dir)` -- the camera-relative WASD unit vector, stored at
  ControllerPlayer+0x458 (setter Game+0x14c960). exe+0x2c679 `SetControllerMovementLength(|stick|)` -> +0x464.
  These two are BOOKKEEPING (animation, evade); they do not steer the character.
- exe+0x2c686 `Entity::GetCoords(player)`; exe+0x2c69f loads `[exe+0x31e9cc]` = **1.25f**, scales dir by it;
  exe+0x2c6d1 `WorldVec3::TranslateToFloor(coords, dir*1.25)`.
- exe+0x2c702 `ControllerPlayer::HandleActionFromJoystick(point, rotateOnly)`; rotateOnly only while
  `SkillManager::IsRunningSkill` (exe+0x2c6df).

Nothing velocity-shaped crosses the DLL boundary. The WASD command is "walk to the point 1.25 u ahead of me".

## 2. Controller side (Game.dll)

- `HandleActionFromJoystick` (Game+0x14b070) forwards to the current state's vtable+0x1c0 =
  `ControllerPlayerState::SelectJoystickAction` (Game+0x14f0f0) -> vt+0x280 `RequestMoveAction(false,false,pt)`
  (or vt+0x288 `RequestRotateAction` when rotateOnly).
- `ControllerPlayerStateMoveTo::RequestMoveAction` (Game+0x152f40): if the new direction's dot with the stored one
  (state+0x20) exceeds `[Game+0x777128]` = 0.9999998 it only latches the point (state+0x30); else
  `DefaultRequestMoveAction`. `ControllerPlayerStateMoveTo::OnUpdate` (Game+0x152e30) applies the latched point
  when the player is within `[Game+0x777180]` = 2.5 u of the current movement target -- always, at 1.25 u
  look-ahead. Net effect: the command is re-issued every frame either way.
- `ControllerPlayerState::DefaultRequestMoveAction` (Game+0x150100). **Gate at Game+0x150172..0x1501fd: the
  request is DROPPED if the point is closer than `[Game+0x77712c]` = 1.0 u in xz, or `[Game+0x7770fc]` = 0.5 u
  while `CharacterMovementManager::IsMoving` (mgr+0x38).** Then `Player::FindPath(pt, 0, &out, 0)`
  (Game+0x1502c1):
  - result 0 -> `ControllerAI::SetState("MoveTo", out)` where **out is the point FindPath RETURNED** (snapped),
    not the point requested;
  - result 2 or 4 with the destination above the player -> a camera-projection retry (the step-up-a-ledge case);
  - else (Game+0x1504c0) `NavManager::FindStraightMovePoint(playerPos, pt, &clamped)` (a navmesh raycast), then
    FindPath(clamped) and SetState("MoveTo", clamped).
- `Player::FindPath` (Game+0x3ba340) -> `CharacterMovementManager::FindPath` (Game+0x8dfd0) with snap radius
  `[Game+0x7771bc]` = **10.0**, and -- for the main player when `movementType > 0` -- the "direction gate"
  parameter ZEROED (Game+0x3ba3b0) instead of `[Game+0x777284]` = -1.0.
- `CharacterMovementManager::FindPath` calls `NavManager::FindPath` (Game+0x8e1f8) with radius 10. Results
  (from the `mov eax,N` sites): 0 success (0x8e32b), 4 NavManager::FindPath failed (0x8e202), 1 path shorter
  than a minimum (0x8e22a), **3 direction gate rejected it (0x8e297)**, 2 detour-ratio gate (0x8e2d8). The
  direction gate (Game+0x8e234..0x8e295) compares the dot of the requested direction with the first straight-path
  leg against the parameter: -1.0 (mouse) accepts anything, so click-to-move paths round corners; 0.0 (WASD)
  refuses any path whose first leg heads more than 90 degrees from the key direction, so keyboard movement
  falls through to the raycast: go this way as far as the mesh allows, never route around.
- `ControllerAI::SetState("MoveTo")` -> `CharacterMovementManager::MoveTo` (Game+0x8e8c0) -> waypoints ->
  `MoveToNextWaypoint` (Game+0x8d7b0) -> Game+0x8dc67 **`NavManager::MoveObject(entity, worldPos)`**.
- `NavManager::MoveObject` (Engine+0x150d30) tail-jumps to Engine+0x25af50 with `entity+0x38` (the
  `vftable{for CROWD::ICrowdAgent}` subobject): stores the target at agent+0x78, sets agent+0x70 = 2. This is
  DetourCrowd's requestMoveTarget.

## 3. Consumer side (Engine.dll, DetourCrowd)

`Engine::Update+0x169` -> `NavManager::Update(dt)` (Engine+0x150d80) -> Engine+0x25b320 (Crowd::Update over
sub-crowds) -> **Engine+0x25c090** (0x1ed2 bytes, a port of `dtCrowd::update`). Per agent:

- dt_seconds = dt_ms * 0.001 (`[Engine+0x34b990]`, Engine+0x25c122).
- corners = navquery->vt[0x8](npos, target, polyPath) (findStraightPath, Engine+0x25c6fb); dvel = unit(corner0 -
  npos) * maxSpeed (agent+0x18; Engine+0x25c814..0x25c9b2); agent-vs-agent separation added, |dvel| re-clamped.
- speed = min(maxSpeed, speed + maxAccel*dt), then min(speed, |dvel|) (Engine+0x25cf5x..0x25cfc9).
- **step = dt_seconds * speed; newPos = npos + dir * step** (Engine+0x25d1a4..0x25d1f4); if dist(npos, target)
  <= step then npos = target EXACTLY (Engine+0x25d724). Arrival never overshoots.
- Safety clamp (Engine+0x25dc6c): navquery->vt[0x18](npos) -> poly ref; nonzero: only y corrected; zero (left the
  mesh): vt[0x20](npos, extent) nearest point -> npos = it, replan flag agent+0x140.
- Heading smoothed toward the desired direction at up to agent+0x40 rad/s (Engine+0x25db64..0x25dc54).
- Callback Engine+0x25dcd7: `[agent+8]->vt[0x18](dt, &agent->npos)` = **`Character::CrowdAgentMoved(int,
  CROWD::CrowdAgentData const&)`** (Game+0x66d80; `Player::vftable{for CROWD::ICrowdAgent}` at Game rva 0x7561b0:
  +0x00 Created, +0x10 Update, +0x18 Moved, +0x28 ReachedGoal, +0x30 Depenetrate, +0x38 Error). It builds a
  WorldVec3 (`SetFromWorldPosition` + `PutOnFloor`), lerps y, then `Character::RotateTowards(dt, heading, pos)`
  (Game+0x5e070) -> Game+0x5e2eb **`World::SetCoords`**: the position write.

Agent layout (from the crowd body): +0x08 ICrowdAgent*, +0x10 radius, +0x14 height, +0x18 maxSpeed, +0x1c
maxAccel, +0x40 maxTurnRate, +0x44 flags, +0x48 npos, +0x54 desired dir (unit), +0x60 heading, +0x6c speed,
+0x70 target state, +0x78 target pos, +0x88 neighbours, +0xb8 dvel, +0xd0 separation, +0xe8 corridor, +0x108
poly path, +0x140 replan. maxSpeed/maxAccel filled by `Character::CrowdAgentUpdate` (Game+0x66c00) from
`Entity::GetMotion()` and `[Character+0xd94]`.

## 4. The sliding algorithm: a nearest-point snap of the GOAL, not of the velocity

Not `moveAlongSurface`, not `dtPathCorridor::movePosition`, not a projection of velocity onto the blocking edge.
`NavManager::FindPath` (Engine+0x150190 -> Engine+0x10c2c0):

- Engine+0x10c3ef `findNearestPoly(start, extents {0.05, 3.0, 0.05})` (extents set at Engine+0x10a5b7).
- Engine+0x10c436 `findNearestPoly(destination, extents {radius, 3.0, radius})`, radius = 10 for the player.
- Engine+0x10c483 rejects only if the snap moved the destination farther than radius.
- Engine+0x10c4ab..0x10c4f7: start and destination polys coincide (the normal case for a 1.25 u step) -> the
  OUT point is the SNAPPED point (`WorldVec3::Translate` by nearest - requested, `PutOnFloor`), no path search.

`findNearestPoly` = Engine+0x260d40 (proved from `NavManager::IsPointOnPathMesh`, Engine+0x150570, which builds
extents {0.05, 5.0, 0.05}, calls it and tests the dtStatus success bit `shr eax,0x1e; test al,1`).

So the requested point `p + 1.25*dir` lands inside the wall; Detour's closest-point-on-poly returns the
perpendicular projection onto the blocking boundary edge; the perpendicular component of the request is
discarded, the tangential component survives; the character walks there; next frame the same from the new
position. That is the slide. The fallback `FindStraightMovePoint` (Engine+0x150160 -> Engine+0x10b880:
findNearestPoly(from) + `dtNavMeshQuery::raycast` Engine+0x262ee0, hit t vs FLT_MAX `[Engine+0x34bda4]`; returns
from + unit(to-from)*min(t*len, len), PutOnFloor'd; 1 ok, 3 start off-mesh) STOPS rather than slides.

## 5. Why a small gap is hard to hit

The crowd step (speed*dt, ~0.05 u at 60 fps) is fine. The granularity problem is the command layer: the
destination is always 1.25 u ahead and then snapped up to 10 u; a request shorter than 1.0 u (0.5 u while moving)
is discarded and the previous target keeps being walked to. A gap narrower than the slide's per-frame tangential
progress is passed over, and the "stop here" the blind player needs has no expression in the keyboard path.

## 6. Seams

Detecting a slide and its edge:
1. Hook exported `NavManager::FindPath` (already in `gd_names.h`; `world.cpp find_path_corridor` calls it): out
   point minus `to` argument = the snap; `normalize(out - to)` is the blocking edge's inward normal, its
   perpendicular the wall tangent. Every character paths through it: filter by entity.
2. Hook exported `Character::CrowdAgentMoved` (Game+0x66d80) for the player: the 2nd arg is `agent+0x48`
   (npos); desired dir at arg+0xc, speed at arg+0x24; a displacement shorter than speed*dt or rotated from the
   desired dir = clamped.
3. No hook: exported `Character::GetMovementTarget()` / `CharacterMovementManager::GetMovementTarget()` is the
   post-snap destination; compare with playerPos + 1.25*dir. Cheapest reliable "am I sliding".
4. `NavManager::FindStraightMovePoint(from, to, out)` (exported, named in `gd_names.h`, unused): an exact navmesh
   raycast -- a fan of these gives a free-distance profile per bearing far better than `free_distance_ex`'s
   0.5 u `IsPointOnPathMesh` walk; a gap is a local maximum. Wrappers: `Character::GetStraightMoveToPoint`,
   `Character::GetFurthestMoveToPoint(WorldVec3 const&, float)`, `Character::CanMoveTo(WorldVec3 const&, float,
   float*)` (all Game.dll exports).

Influencing movement:
1. **`ControllerPlayer::HandleActionFromJoystick`** (Game+0x14b070, exported, in `gd_names.h`): one call per
   frame, player-only, the argument IS the destination. Rewrite the point (shorten/lengthen the look-ahead,
   re-aim at a gap centre) or swallow the call to stop dead (no new command; the crowd's exact-arrival snaps
   npos to the last target). Everything downstream behaves as the game intends. Constraint: a rewritten point
   must be >= 1.0 u away in xz (0.5 u while moving) or DefaultRequestMoveAction discards it.
2. Explicit stop: `Character::StopMoving` (Game+0x5fba0) / `CharacterMovementManager::Stop` (exported).
3. Direct commands bypassing the joystick layer and its gates: `Character::MoveTo(WorldVec3 const&, float,
   AnimationSet_Type, float)`, `CharacterMovementManager::MoveTo(WorldVec3 const&)` (Game+0x8e8c0) -- full
   pathing incl. routing around, which the keyboard path refuses.
4. The one-float switch between the two movement modes: `Player::FindPath` zeroes the direction-gate argument
   for movementType > 0 (Game+0x3ba3b0); hooking it and forcing -1.0 makes WASD path round corners like the
   mouse. Real behaviour change.
5. `SetControllerDirection` / `SetControllerMovementLength` are NOT a movement seam.

## 7. Not established

- The concrete class behind the crowd's internal nav-query interface (`subcrowd+8`; slot semantics proved from
  call sites, vtable not located). Does not affect the conclusion.
- Whether `moveAlongSurface` exists anywhere in Engine.dll (nothing in the player's path has its shape).
- `CharacterMovementManager::PathResult` enumerator names (values and producers are known).
- Agent flag bits at +0x44 and `CrowdAgentDepenetrate` (gate agent-vs-agent only).
- Live confirmation of the 1.25 u look-ahead and the 1.0/0.5 u gate (constants re-read from the images 2026-09-01;
  behaviour not yet observed).
