# Lua in Grim Dawn (investigated 2026-08-22, live-probed through the mod)

## What it is
- **LuaJIT 2.0.4** in `x64\lua51.dll`, bound dynamically by Engine.dll (the `lua_*`/`luaopen_*` names in
  Engine.dll are `GetProcAddress` strings), wrapped by a binding layer called **LUAGLUE** (`__luaglue` table
  in the state: `class`, `derive`, `instance`, `dump`, `dumpex`, `dumpr`, `readonly`, getters/setters;
  `__luaglue_version`). The JIT is on (`jit.status()` true).
- One global state, owned by `GAME::LuaManager` at **`*(gEngine + 0x68)`** (read from
  `GameEngine::PostLuaInitialize`). Exported: `LuaManager::RunCode(char const*)` (a chunk; returns false on
  error), `Load(std::string const&)` (a script by resource path), `RegisterForUpdates(objectId, fn)` /
  `UnregisterForUpdates`, `Update(int)`, `GetState()`, `CollectGarbage`, `DumpStack`, `LogInfo`. Lua errors
  and `print` go to the engine log channel, not to a file.
- Dev route: **`/lua?code=...`** (or POST the chunk as the body; `scratchpad/luaprobe.py` does that) runs a
  chunk on the game thread through `RunCode`. Readback channel used for probing: `Game.AddObjective(text)`
  shows up in `/objectives` (`Game.ClearObjectives()` afterwards).

## Sandbox (probed live)
- **Present**: `_G`, `string`, `table`, `math`, `coroutine`, `jit`, `pcall/xpcall/error`, `loadstring`,
  `loadfile`, `dofile`, `load`, `getfenv/setfenv`, `getmetatable/setmetatable`, `newproxy`, `collectgarbage`,
  `print`, `dump/dumpex/dumpr` (glue helpers), the engine's math shorthands (`sqrt min max pow sin cos tan
  random`), the quest helpers from `scripts/game/*.lua`, `Client`/`Server` booleans (both true in single player).
- **Absent (nil)**: `io`, `os`, `ffi`, `debug`, `package`/`require`, `module`. So no files, no clock, no
  C calls from Lua -- EXCEPT that **`loadfile`/`dofile` open real filesystem paths** (verified: a chunk under
  the scratchpad loaded and ran), while the game's own scripts are reached through `Script.Load("scripts/...")`
  from the resource archive (`loadfile("scripts/libs/misc.lua")` fails: the VFS is not on loadfile's path).
- `Script.Load(path)` loads from `resources/Scripts.arc` (and mod directories); the entry point is
  `scripts/main.lua` -> `libs/shared.lua` (vector, userdata, table, misc) -> `game/grimdawn.lua` -> `quests.lua`,
  `dungeonchests.lua`, `events/waveevent.lua`; ~110 quest/flavour scripts live under `game/quests/`.

## The API the glue exposes (from the exe's registration tables + the live `_G` walk; the official
## reference is the Modding Guide PDF, "Tutorial 07: Lua Scripting Basics", pages 52-64)
- **Game**: GetLocalPlayer, GetNumPlayers, GetPlayer(i), GetMin/Max/AveragePlayerLevel, GetGameDifficulty
  (`Game.Difficulty.Normal/Epic/Legendary`), GetGameChallenge, GetGameTime, IsHardcore, PlayersDead,
  ScreenShake, PlayNetSound, PlayFullscreenGlow, AllowNpcTalk, UnlockNextDifficulty, UnlockTutorial,
  TeleportPlayer, KillAllMonsters(+OnNextRespawn), Enable/DisableRespawn, AddObjective/RemoveObjective/
  ClearObjectives, mutators (AddMutator, ResetMutators, NotifyNewMutators), the survival/crucible set
  (Start/End/ResetSurvivalEvent, timers, wave tiers, restarts, SaveSurvivalScore), the endless-dungeon set
  (GenerateEndlessDungeon, IsEndlessDungeonBossDead, ...), GetAscendantTokenModifier.
- **Player** (userdata; `Player.Get(id)`, `IsType(id)`): GetPlayerName, GetFaction, HasToken/AnyoneHasToken/
  ServerHasToken, GiveToken, RemoveToken, GetQuestState(questId), GetQuestTaskState(questId, taskId),
  GrantQuest(questId, taskId), CompleteQuest, DestroyItem, IsSpaceAvailable, HasMastery, HasBuff, Teleport,
  GetSurvivalScore, InEndlessDungeon/ActiveFloor/BossFloor; plus the **Character** base: GetLevel, GetCoords/
  SetCoords, AdjustMoney, AdjustTribute, HasTribute, GiveExperience, GiveLevels, GiveSkillPoints, GiveFaction,
  SetFaction, UnlockFaction, GiveItem, GiveAffixItem, TakeItem, HasItem, Attack, Move, MoveAction,
  UseSkillAction, Turn, PlayAnimation, PlaySound, Say, Kill, IsAlive, TeleportHome, ToggleSkills,
  ToggleInvincible; `Character.Create(dbr, coords)`, `CreateNearPlayer`.
- **Entity** (base of everything placed): Create(dbr, coords), Get(id), IsType, GetCoords, SetCoords,
  IsReloaded, NetworkEnable. **Object**: GetId, GetName, Destroy, Cache, Release. **Actor**: GetAttachedCoords.
- Level pieces: **Door** (Open/Close/IsLocked/SetLocked/SetAutoClose), **Lever** (+IsOpen), **Chest**
  (SetLocked), **Shrine** (IsLocked, IsRestored, SetLocked), **Riftgate** (SetLocked), **Destructible**
  (Destroy), **AreaTrigger** (Create, DestroyItems), **Proxy** (spawner: Create, Run, AllKilled, IsAmbush,
  LinkPatrolPointGroup), **Npc** (SetAvailableForConversation), **Item** (Create, HasAffix).
- **UI.Notify(tag)** (the HUD banner, localized tag), **Shared.Localize(tag)**, `Shared.setUserdata/
  getUserdata/persistUserdata/restoreUserdata` (per-object script state, saved with the game), **Time.Now()**
  (ms), **Script.Load / Script.Event(name) / Script.RegisterForUpdate(objectId, "fnName", periodMs) /
  UnregisterForUpdate** (periodic callbacks need a ScriptEntity object id), **MpScript.QuestEvent /
  QuestGlobalEvent / LuaGlobalEvent** (network broadcast of quest events), `Debug.AddStatisticText`.
- Math types: **Vec** (Length, LengthXZ, Unit, ...), **Coords/WorldCoords** (origin, xAxis, yAxis, zAxis via
  getters), `WorldVec`; `CrossProduct`, `DotProduct`. Note the game's scripts use a preprocessed dialect:
  `/* */` comments, `!=`, `&&`, `||` (the guide, p. 55).
- Quest ids / task ids are the 32-bit hashes the Quest Editor shows (e.g. `kQuest_WakingToMisery = 0x2B43AF80`).

## How capable, for us
- It is the **quest/level scripting layer**: tokens, quest state, spawning, doors, teleports, banners, XP and
  items. It is not a general automation layer: no access to the UI, inventory grid, skills, hot slots, camera,
  input, or entity queries beyond `Entity.Get(id)`; no file/OS/FFI access (except `loadfile` of real paths).
- Everything it can do, the exported C++ API can do too (and more); the Lua route is only simpler for
  quest-state manipulation (`GrantQuest`, tokens) and for testing (`GiveExperience`, `GiveItem`, `Teleport`).
- Hooks worth knowing: the game calls Lua functions by name (`QuestEventDispatch` -> `clientQuestTable[event]`);
  `GameEngine::CallLuaQuestCommandEvent(name)` is the exported entry for quest command events. A periodic
  Lua callback needs a ScriptEntity id (`RegisterForUpdate` refuses others: "Error registering object %u for
  updates, function '%s' does not exist!").
- Risk: `RunCode` runs in the live state; an error is reported (false) and logged, a crash inside a glue call
  would take the game down like any other engine call. Quest-state writes (`GrantQuest`, tokens) are saved.
