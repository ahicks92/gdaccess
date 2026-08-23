# Rooms: where the player is, and how we know

A blind player's perception of place is what the mod announces; nothing else says "room". This document is the
model, the data, the tooling and the runtime of that feature (designed with the user 2026-08-22; the
authoring workflow and the description rules come next).

## Vocabulary
- **region** -- the game's named area ("Devil's Crossing"), identified by the world-map location record of
  its chunks (`riftgatemap1a_devilscrossing`). The HUD's area name.
- **sub-region** -- our named division of a region ("the prison", "the road north"); assigned by the
  authoring workflow so a player without the map can plan a route. Table `subregions`.
- **room** -- a piece of the walkable ground produced by the segmentation: tens to a few hundred m², a box
  or an approximation of one. Has a stable key, a class, an anchor, and (once authored) a title and a
  description.
- **chunk** -- the engine's `GAME::Region`: one `.lvl` inside `world001.map`, 128x128 units (a few are
  96 wide), the unit of streaming and of `WorldVec3` (region-relative position). 633 chunks; the overworld
  ones are laid out in one shared frame: **tile coordinates are world coordinates**, and a chunk's
  `GetOffsetFromWorld()` IntVec3 is simply its tile frame's min corner (verified live against
  `World::GetRegion(i)` for all 633 on 2026-08-22). Region-relative = world - offset.

## What the game ships (read offline by `tools/gdmap`)
- `resources/Levels.arc` -> `world001.map` (819 MB): a "MAP" v9 header with the quest list, a "Unique
  Entities" table and the chunk table (per record: IntVec3 offset, 16-byte GUID, location record, devotion
  shrine record, lvl path, body offset + size, 6 placement ints), then every chunk's "LVL" body.
- Inside a body: **Detour tile-cache layers** (`DTLR` v1; 32x32-unit tiles at 128x128 cells = 0.25
  units/cell; uncompressed 49208-byte blobs of heights/areas/cons). area 0 = unwalkable, 1/2 = the
  engine's 8-unit checkerboard (polygon splitting, NOT surface classes). Quest gates (doors, barricades)
  are runtime obstacles on top of this bake, so the bake is "everything walkable regardless of quest
  state". The bake leaves an un-eroded 4-cell band along tile seams (walkable strips inside walls; the
  game does not walk them): `seam_filter` drops band cells not backed by interior ground.
  The runtime mesh is up to ~1.5 units wider than the bake at edges (polygon simplification), and an
  upper floor (the prison's cell block at height 14 over ground at 8) shares (x, z) with the ground:
  multi-layer cells are unioned.
- **Terrain texture layers**: `records/terraintextures/*.dbr` names each followed by a 128x128 u8 opacity
  mask at 1 unit/sample (region-local; world = offset + local). Roads are painted layers (gravel,
  cobbles, flagstone, fieldstone...); which records are roads is a judgment (`ROAD_RE` in rooms.py for
  now), `surfaceType` is only footstep audio (Grass 68, Dirt 63, Stone 29, Wood 2, Snow 1, Unwalkable 1).
  Roads are an overlay for road helpers, never a segmentation input (decided).

## Segmentation (`tools/gdmap/rooms.py`, a port of wotr-access's RoomMap)
Per region, all its overworld chunks stitched (a room never needs to stop at a chunk seam):
seam filter -> islands (components not connected to the main one; under `island_min` m² dropped, larger
ones flagged) -> furniture mask (interior islands <= `furniture_max` cast no clearance shadow) -> clearance
(EDT) -> persistence watershed (basins from clearance maxima, split where they meet across a dip deeper than
`persist`) -> BFS fill of sub-`cut_floor` slivers -> small-region merge (< `min_area`) -> **walk cap**:
a room whose longest walk (double-sweep Dijkstra on the 8-neighbour cell graph) exceeds `max_walk` is
bisected with an **axis-aligned line** across its bounding box's longer axis, in the middle third, where the
cross-section is narrowest (decided: the announcements ARE the perception, so a boundary must be simple to
hold in the head -- a north-south or east-west line, never a geodesic contour) -> merge again -> stats
(anchor = the cell of maximum clearance, class from area/elongation/clearance as in wotr) -> exits
(connected runs of boundary cells between two rooms; `cut` marks cap cuts). Rooms are ordered by anchor
(z, x). Devil's Crossing: 8 chunks, 45,000 m², 202 rooms, 297 exits, 4 s.

**Cross-region exits** (`rooms.py seams [--write]`, 2026-08-23): the per-region watershed sees its grid
edge as a wall, so an opening at a REGION boundary (the Devil's Crossing -> Lower Crossing road at z=-256,
found on the user's real save) never becomes an exit. The seams pass scans every region pair in the db for
adjacent walkable cells across the seam (heights within 1 unit when known), clusters them into openings
(>= 0.75 units wide) and writes exit rows into BOTH regions with the far side's full room key as `room_b`.
The mod keeps an exit whose `room_b` is not local as a foreign exit and speaks its destination as
"<region name>, <room title>" ("Lower Crossing, stump clearing"). **Re-run `seams --write` after any
`area --write`** -- re-segmenting a region deletes all its exit rows, the cross-region ones included.

Defaults (`Params`): persist 0.7, min_area 12, cut_floor 0.45, furniture_max 12, max_walk 60,
min_split_area 40, island_min 50. Sensitivity on Devil's Crossing: persist 0.5 -> 164 rooms, 0.7 -> 144,
1.0 -> 129, 1.5 -> 114 (five-chunk cluster); the cap made 9 cuts at 60 and 1 at 80 there, 20 over the
whole region (the open eastern chunks). Per-region params live in the db (`rooms.py area X --set k=v`).

## The database (`assets/rooms.db`, `tools/gdmap/roomsdb.py`)
Written by the tools, read-only for the mod (vendored SQLite, `src/db.*`).
- `regions(key, name, location_record, chunks json, params json, algo_version, signature, stale)` --
  key = the location record basename; signature = blake2 over the chunks' bodies (a game patch that changes
  a chunk marks the region stale: `rooms.py status`).
- `grids(region_key, x0, z0, w, h, cell, labels, label_keys json)` -- the label grid, run-length encoded as
  (int16 value, uint16 run) pairs row-major (`rle_encode` / `LabelGrid::decode_rle`); label -> room key.
- `rooms(key, region_key, subregion_key, anchor_x, anchor_z, cls, area, walk, bbox json, island, title,
  body, status)` -- **key = `<region>:<round(anchor x)>:<round(anchor z)>`**: authored rows (title, body,
  sub-region) survive a re-segmentation as long as a room still contains that anchor; vanished keys become
  `orphan`. status: unseen, shot, described, verified, orphan, stale.
- `subregions(key, region_key, name, summary)`, `exits(region_key, room_a, room_b, x, z, width, cut)`,
  `shots`, `terrain_types`, `coverage` (for the authoring workflow).

Commands: `uv run tools/rooms.py regions|grid|segment|area|status|plan` (docstring has the forms);
`area devilscrossing --write --name "Devil's Crossing"` writes the region; `plan devilscrossing` renders
what the mod will use. Floor plans: `build/rooms/*.png`, north (-z) up, exits as dots (red = cap cuts).

## Runtime (`src/rooms.cpp`, `src/core/rooms_model.*`)
- Per frame from the in-game screen: chunk (`world::region_name()`) -> region (the db's `chunks`) ->
  the region's grid (loaded once) -> `label_at(x, z, ring 8 cells)` -> room. `Hysteresis`: after 1 s in a
  room (`settle`) any change is announced at once; within that first second a change must persist 400 ms
  (`dwell`) so boundary flapping stays quiet (`/room?dwell=&settle=`); the first room is immediate.
- On a confirmed change one line in the player's own voice (Zira, `voice::kGroupInfo`): the region name
  if it changed, the sub-region name if it changed, the room title -- or "room N" for untitled rooms while
  nothing is authored (`/room?untitled=0` to silence). "Devil's Crossing, room 193" at the spawn.
- **X** speaks the room's title, then its description, through the screen reader ("no description yet"
  until authored).
- **V / Shift+V** = the scanner's `ScanGroup::Exits`: `rooms.cpp` provides the current room's exits as point
  items (`world::set_exit_provider`; id = `kPointIdBase` + exit index, label = the destination's title or
  "room N", note "blocked" when the live `IsPointOnPathMesh` refuses a 5-point cross at the opening although
  the bake allows it -- a shut gate or barricade) and `world::cycle_review` does what it does for every
  group: continue from the reviewed id, nearest first, `lock_point` on the opening, the route ping on landing,
  `;` re-pings, "distant" when off screen. "room 190, blocked, 5 away, 4 o'clock, 1 of 3".
- Dev: `/room` (the lookup chain, the current room, its exits with live walkability; `?say=1` repeats,
  `?reload=1` reopens the db after a tools rewrite), `/regions?max=`, `/portals`, `/navprobe?x0=&z0=&x1=&z1=&step=`
  (the live mesh sampled; `build/rooms/compare_navprobe.py` diffs it against the bake).
  Authoring: `/teleport?x=&z=[&check=1]` (world coordinates; refuses unloaded chunks and off-navmesh landings;
  `tools/shots.py` hops 60 units at a time so chunks stream in), `/project?pts=`, `/fog?x=&z=&radius=`,
  `/pause?set=0` (a hot reload in the world can leave the game paused).

## Authoring (done for Devil's Crossing 2026-08-22)
`uv run tools/shots.py region <region> --status unseen` photographs every room (the game's own teleport, fog
reveal, outline + exits drawn from `/project`, terrain and entity facts in `meta.json`); then the Workflow
`tools/workflows/rooms_author.js` (every agent Opus): sub-regions once (`subregions: true`), one describer per
room (`rooms: [keys]`, self-checked with `author.py check`, every 10th room reviewed), then the consistency pass
(`consistency: true, subregion_keys: [...]`, optional `notes`) that reads each sub-region as a whole. Rules:
`docs/rooms-description-rules.md`. Copy `assets/rooms.db` next to the DLL (a build does it) and `/room?reload=1`.

## Open
- One room of Devil's Crossing (`-70:-183`) is unseen: its anchor is bake-only ground the live mesh refuses.
- The region display name is authored (`--name`); the game's own source is the riftgate master table's
  `tagWorldMap*` tags (`records/ui/riftgatemap/riftgate_mastertable.dbr`), not yet mapped.
- Exits across region boundaries, road helpers (skeleton graph), the procedural DLC (segment live from the
  in-memory data; the anchor-keyed design allows it), player-authored marks.
