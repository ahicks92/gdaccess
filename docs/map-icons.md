# The aerial map's icons: what a sighted player gets as "your objective is that way" (2026-09-11)

Survey done for sighted parity on quest direction, with the game live under the player (read-only routes) plus
static RE of Game.dll/Engine.dll and the exe. Result: the game has no compass, no arrow and no per-objective
coordinate. Direction comes from four sources, in order of how directive they are.

## 1. Points of interest (class `AreaOfInterest`, nugget type 6)

`records/ui/mapaerial/poi/*.dbr` (template `areaofinterest.tpl`), 100 records, placed in the levels (124
references in Levels.arc, none spawned by scripts). Fields: `AreaDescription` = a localized tag
(`tagPOI001A` "Entrance", `tagPOICaveBurialHillA` "Burial Hill Entrance", `tagPOIQuest_Homestead` "Secure a way
to Homestead"), `markerRange` 50, and for 53 of them `questFile1..N` + `taskUID1..N` (10 also `questOnly`).
`AreaOfInterest::AppendDetailMapData` (Game.dll+0x26060) evaluates the record's condition collection (the
bound task's state) and a byte at +0x728 before writing its nugget, so a quest-bound POI is on the map only
while that task is active. That is the sighted player's objective marker. The first quest's cave entrance
(`poi_caveburialhillentrance`, bound to `mq_wakingtomisery` task -1834957056) is one.

Not a `StaticMarker` (its ctor calls an Engine base, not StaticMarker's); `Player::GetMarkerUIDs` /
`AddMarkerUID` (via `StaticMarker::BindToCharacter`) is a separate "markers the character walked through" list,
empty on a level-32 character, not a direction source. No `StaticMarker` records ship.

## 2. The quest log prose

Task descriptions carry the designers' directions in words ("Far to the north, in the Asterkarn Mountains").
`/quests` prints them; it is the only cue for anything outside the loaded, in-view area. NB the compass those
words use is the default camera's screen-up, 50 degrees from the mod's clock (docs/compass.md).

## 3. Everything else on the map

Nugget types, read off each class's `AppendDetailMapData` (the first dword store into the local nugget) and
the game's rollover words (`records/ui/mapaerial/iconbutton_*rollover.dbr` -> `tagMapSymbol*`):

| type | writer | game's word |
|---|---|---|
| 0 | the hero (GetDetailMapData itself) | Player |
| 1 | party members (GetDetailMapData) | Group |
| 2 | Npc | NPC |
| 3 | StaticTeleporter / DynamicTeleporter | Riftgate |
| 4 | StaticRespawner | Healer |
| 5 | FixedItemShrine | Shrine |
| 6 | AreaOfInterest | POI |
| 7 | NpcMerchant | Vendor |
| 8 / 17 | Monster (hostile per FactionPack; 17 when MonsterClassification == 3) | (none; mod: hero monster / boss) |
| 10 | NpcSkillReallocator | Spirit Guide |
| 11 | NpcEnchanter | Inventor |
| 12 | the grave (GetDetailMapData, `GetMainPlayersGraveData`) | (mod: your grave) |
| 13 | NpcCaravan | Smuggler |
| 14 | Actor with record `mapNuggetType = Custom` | (mod: obstacle) |
| 15 | NpcCrafter | Smith |
| 16 | StaticShrine | Shrine |
| 18 | NpcTransmuter | (mod: illusionist) |
| 19 | Item, EndlessBuffShrine, MonsterShrine, NpcCrafter's first branch | unseen |

`mapNuggetType` is a record field on every actor: across the database only `Custom` (53 records, all
barricades / rubble walls / the Burrwitch bridge debris, symbols `ui/mapaerial/symbols/mapsymbol_dynamicobstacle0*.tex`)
and `Boss` (7 monsters) are set. `Actor::AppendDetailMapData` (Engine.dll+0x2a730) reads the type at actor+0x4b4
(-1 none, 14 custom -> bitmap at +0x4b8, else the enum is the nugget type). NPC quest availability is NOT in the
map data; the "!" is drawn in the world.

## The nugget (MinimapGameNugget, 0xA0, read live)

+0x08 type; +0x10 `basic_string<u16>` name (POIs: the localized AreaDescription; the hero: the character's
name; empty on class-driven icons); +0x50 the custom symbol `GraphicsTexture*` (a `Resource`, so
`Resource::GetFileName` names it); +0x58 `Region*`; +0x60 region-relative Vec3; +0x70 a facing; +0x8c 0.75.
Live example: two type-14 nuggets on `wallfieldstonedry_barrierstraight01` pieces (`mapNuggetCustom =
mapsymbol_dynamicobstacle02`) at 137 / 146 units.

## Reach: how far a sighted player sees

- The camera: distance 20..48, default 36 (`gameengine.dbr`). The mod pins 48.
- The aerial map (M) is an orthographic camera looking down at the explored terrain. Its zoom is the view
  height in units / 3: the wheel handler (exe+0x174c20) clamps 40..135, default 65, saved as `mapZoom`
  (options.txt). So the visible slab is 120 x ~190 units zoomed in, 195 x ~315 default, 405 x ~650 zoomed out
  (pane 965 x 600, `aerialmapconfiguration.dbr`). The map-update method exe+0x174e80 (its `this` = MiniMap+0xba0;
  nugget vector at +0x210 = MiniMap+0xdb0, zoom current/target at +0xacc/+0xad0 = MiniMap+0x166c/+0x1670,
  verified live) gathers the icons with `GameEngine::GetDetailMapData(vector&, WorldFrustum const&)` over that
  camera's frustum (`World::GetEntitiesInFrustum` then every entity's vtable slot 0x58 = AppendDetailMapData).
  The frustum, not the connected-region set, is the limit. A pan offset and a Centre button let the view be
  dragged over streamed, explored ground.
- The HUD compass-map: zoom fixed at 20 or 30 by mode -> 60 / 90 units tall.

## What the mod does with it (2026-09-11)

The Ctrl+M list (`screens/map_markers.cpp`, `world::map_markers`) names each icon as the game does: the nugget's
own name first, else the entity under the icon (a merchant's or NPC's name), else the type's rollover word;
type 14 reads "obstacle" from the symbol path. `MapMarker::quest` = type 6. While the screen is open the zoom is
pushed to the wheel's maximum (`exe_ui::aerial_zoom_set(135)`, both fields so the next update gathers at the
new reach) and the player's value restored on leaving. Decided against (with the player): gathering the icons
ourselves beyond the sighted reach, and flagging quest-bound POIs from an offline table -- parity first.
Dev: `/mapmarkers` (typed, named), `/mapnuggets` (raw).
