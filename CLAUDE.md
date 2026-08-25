# GD Access — screen-reader accessibility mod for Grim Dawn (C++)

Injected DLL that hooks the game's own engine exports. Design lineage: `../wotr-access` (C#/Unity sibling
project by the same author) — see `docs/design-notes-from-wotr.md` for the decisions we carry over.

## Game facts
- Grim Dawn v1.3.0.8 (x64), Steam build. Install: `C:\Program Files (x86)\Steam\steamapps\common\Grim Dawn`.
  The 64-bit game is `x64\Grim Dawn.exe`; **its working directory must be the install root** (where
  `database.arz` and the `.arc` archives live) or it starts with no data and crashes in a string compare.
- Closed-source C++ (Titan Quest engine lineage), split into DLLs that **export named, MSVC-decorated C++
  symbols**: `Engine.dll` (6283 exports), `Game.dll` (25100 -- note pefile silently caps at 8192 unless
  `max_symbol_exports` is raised; `tools/gen_exports.py` does), `DirectInput.dll` (34, Crate's own wrapper
  around dinput8), `Widget.dll` (tool UI, irrelevant). The exe itself is SteamStub-packed on disk (static
  analysis of it is useless; it is plain in memory) and holds the in-game UI screens (MenuManager, UI*Window).
- Settings: `%USERPROFILE%\Documents\My Games\Grim Dawn\Settings\options.txt`. `screenMode`: 0 fullscreen,
  1 windowed (title bar), 2 borderless. `inactiveUpdateRate = 0` means the engine stops ticking when the
  window is unfocused (our per-frame hook stops too). We run windowed 1600x900.
- Rendering text: bitmap fonts via `GraphicsCanvas::RenderText2d*` (Engine.dll, exported) — the (x,y)
  overloads are called by the Rect overloads, so only the (x,y) family is captured.
- Input: the game polls `DirectInputDevice::GetNumKeyEvents()/GetKeyEvent(i)` each frame. `Display::HandleKeyEvent`
  exists but the exe's UI does not route through it. Returning 0 from `GetNumKeyEvents` mutes game keys.
  Key codes are the game's `InputDevice::Button` enum: DIK scancodes for plain keys, then a contiguous
  block for extended keys -- Home 0x78, Up 0x79, PageUp 0x7a, Left 0x7b, Right 0x7c, End 0x7d, Down 0x7e,
  PageDown 0x7f, Insert 0x80, Delete 0x81; Ctrl 0x1d, Right Ctrl 0x6b, Alt 0x38, Right Alt 0x76, Shift
  0x2a/0x36; mouse buttons from 0x91 (wheel up). Full table from the game itself: `tools/exports/keynames.txt`
  (dev route `/keynames`).
- Navmesh is Recast/Detour (`GAME::NavManager`, `CROWD::` = DetourCrowd); `NavManager::FindPath` is exported.
- The exe's main loop (exe+0xee4d..0xef91, read 2026-08-21) per iteration: `display->Update(dt)` (vtable+0x28 on the
  object at mainobj+0x250 -- the engine's `Display` in the menus, the exe's own subclass in the world, whose
  override never reaches the exported `Display::Update`), the input device poll, `SoundManager::Update`,
  `PresentSurface`, `Steamworks::Update`, `Engine::Update(0,0,0,0)`. **Our per-frame tick is the `Engine::Update`
  hook** (runs after the poll and the render in both states); `Display::Update` is only counted. A second path
  (loading screens) calls neither.
- In the world the HUD's numbers ("250/250"), "Active Quests" and the game's message boxes ("Okay") ARE captured
  by the RenderText2d hooks; the intro cutscene subtitles and the minimap's area name are drawn another way.
- Reaching in-game objects (plan): `GameEngine` is not an exported singleton -- capture `this` by hooking a
  per-frame member (`GameEngine::Update(int)`, `ControllerPlayer::Update(int)`), `Singleton<NavManager>::Get`
  is exported, `GameEngine::GetMainPlayer()` / `PlayerManagerClient::GetMainPlayer()` give the `Player`;
  positions are `Entity::GetCoords()` / `Character::GetFootCoords(bool)`, `WorldVec3` = region-relative Vec3 +
  `Region*` (`GetWorldPosition`, `SetFromWorldPosition`, `PutOnFloor`); probes: `NavManager::IsPointOnPathMesh`,
  `IsBlocked(Vec3,float)`, `FindStraightMovePoint`, `FindClosestPointOnPathMesh`, `World::GetEntitiesInSphere`,
  `World::GetIntersection(WorldRay)`; `Character::GetCurrentAttackTarget`, `GetMovementTarget`.
- Measured 2026-08-21 (src/world.cpp): `WorldCoords` = `Region*` (+0) + origin `Vec3` (+8) + pad (+0x14) + 3x3 axes
  (+0x18, size 0x40; read from the ctor 2026-08-22);
  `Coords` = 3x3 axes then origin (offset 36); `WorldVec3` = `Region*` + `Vec3` (region-relative; region 0A001's
  origin coincides with world). Player "Bob" spawns at world (59.2, 7.8, 97.2) in `Levels/Region0A001.lvl`;
  default camera yaw 0.8727 rad. **WASD is the game's own feature**: `movementType = 1` in options.txt (the
  launcher does not set it; 0 = click-to-move). Screen-up in world xz = (-sin yaw, -cos yaw), screen-right =
  (cos yaw, -sin yaw). `NavManager::IsPointOnPathMesh` on `WorldVec3::PutOnFloor`'d points gives the wall
  probes (free distance 3..15 units at the spawn). Own audio: miniaudio mixer in src/audio.cpp (the dev
  launcher's process mute silences it too).
- Entities around the player: `Region::GetEntitiesInSphere(mem::vector<Entity*>&, const Sphere&, bool, EntityListType)`
  on the player's `Region*` (NOT `Level::` -- a Region holds its Level at +0x68 and its World at +0x30; calling
  the Level variant with a Region crashed). Read with `tools/dll_dis.py Engine.dll <name>`: `mem::vector` is
  std-like `{begin, end, cap}` and is appended to; `Sphere` = `{Vec3 centre, float radius}` in the calling
  region's coordinates. Keep the radius modest (25 worked, 400 crashed the game inside the query).
  `Object::GetObjectName()` returns `const char*` (the .dbr record path), `GetObjectId()` the id the
  controller APIs take. Dev routes run under an SEH guard (`route faulted: ...`); hangs are not caught.
- **Targeting = the virtual cursor** (verified 2026-08-21): the exe re-resolves its combat enemy from the cursor
  every frame (`SetCombatEnemy(0)` at exe+0x218eb then `SetCombatEnemy(id)` at exe+0x21b9c), so
  `world::lock_target(id)` just keeps the cursor override on the entity's projected position
  (`WorldCamera::Project(WorldVec3 + 1.0 y, Viewport(0,0,w,h))` -> Vec2 via hidden pointer) each frame, and the
  game hovers, names ("Training Dummy" + level in `box_font` HUD text), targets and attacks it natively. Off-screen
  entities cannot be targeted this way (the projection leaves the window) -- the player must face them first.
  Dynamic class names: `Object::GetRTTIClassInfo` dispatched through the vtable slot found in `Object::vftable`
  (the export is the base implementation); `RTTI_ClassInfo` = vptr + `const char* name` ("Player", "Npc",
  "Monster", "PlayerSpawnPoint"). `/entities`, `/project?id=`, `/lock?id=|off=1`, `/target` are the dev routes.
- Hot reload crashes (the in-world `bad_function_call` abort of 2026-08-21 and a main-menu crash inside our
  tick on 2026-08-22) were Detours transactions updating only the calling thread: `gdaccess_unload` runs on a
  remote thread, so the game thread could sit in a trampoline being freed (and the new DLL maps at the old
  base, which is why the stale frames symbolized against the new PDB). Fixed: `hooks.cpp ThreadUpdater`
  suspends/updates every thread of the process in every transaction, and the unload sleeps 250 ms after
  detaching so in-flight hook bodies finish before FreeLibrary. Verified: 3 reloads at the main menu and 2 in
  the world. `tools/windows.py` reads the text of a modal dialog the unfocused game put up.
- Combat feedback (static RE 2026-08-22, `docs/combat-feedback.md`): floating numbers/Miss/Dodge/Block are
  `EventManager::Send(ev, 0x1b)` from `CombatManager::TakeAttack` (text at ev+0x40, u16); all damage runs
  through exported `CombatManager::ApplyDamage(float, PlayStatsDamageType const&, CombatAttributeType,
  vector<attackerId>)` on the victim; no player combat log exists (`gLogCombat` is a dev printf switch);
  popups = `ControllerPlayer::SetUserText(tag)`, banners = `GameEngine::AddUINotification`.
- Positional voices (2026-08-22, verified through the loop): `src/tts_onecore.cpp` (own OneCore backend via
  C++/WinRT, the only TU including `winrt/*`; worker-thread MTA only; api-ms-win-core-winrt-* delay-loaded so a
  missing runtime loses combat speech, not the mod) -> `src/voice.cpp` (Mark = at the enemy, Zira = the
  player; worker queue, per-text PCM cache, `audio::play_pcm` with groups/replace; own ring at `/voice`,
  `/voices` status; falls back to `speech::speak` tagged "(prism)") -> `src/combat.cpp` (the `EventManager::Send`
  0x1b hook, `core/combat_text` parse, `core/combat_coalesce` 150 ms same-place merge, `core/threshold_watcher`
  10 % health steps). H = vitals, menus/UI and every other KEY stay on `speech::speak` (prism/NVDA): the voices are for what happens in the world, keys are screen-reader readouts unless noted. Synthesis is 4-15 ms per line.
  Dev: `/voice?say=&voice=mark|zira&pan=&replace=1`, knobs `vol= coalesce= window= cap= max= near= far= floor=`,
  `/combat[?raw=N]`. Voice level: lines are peak-normalized (0.8), bypass the 0.6 master, and use their own
  rolloff `world::voice_gain` (1.0 out to 9 units = moderateRange, linear to 0.4 at 32 = bossRange); the review
  pings keep the sonar curve. The game's range table is `records/game/gameengine.dbr` (`tools/arz.py` reads
  database.arz): meleeRange 1.25, meleeTargetDistance 2.4, shortRange 4.75, moderateRange 9, longRange 15,
  maximumRange 18, bossRange 32, camera 20..48; skills pick one by `distanceProfile`.
  Known: the first `/keydown` hold right after a hot reload did not register once (second try worked).
- Tooltips/descriptions are exported as data: `Item::GetUIDisplayText`, `Monster::CreateUISummaryText`,
  `Conversation::GetText`, `Quest2::GetText`, `LocalizationManager::GetText(tag)`.
- Movement/targeting substrate (all `GAME::ControllerPlayer`, exported): `SetControllerDirection(Vec3)` +
  `SetControllerMovementLength(float)` + `HandleActionFromJoystick(WorldVec3, bool)` is the gamepad/WASD
  movement path; `SetCombatEnemy(id)`/`GetCombatEnemy`/`FaceTarget(id)`/`SetTargetPoint(WorldVec3)` is
  targeting state; `SendSkillAction`, `InstantSkillAction`, `InteractAction`, `NpcAction`, `ItemAction`,
  `UseItem` take explicit world points / entity ids. `options.txt` `targetLock` is the game's own
  target-lock option. To be verified in-game before the movement/targeting design.

## The NVDA-killing bug and its fix (solved 2026-08-21)
`DirectInput.dll` contains a verbatim copy of Microsoft's old "Disabling Shortcut Keys in Games" recipe: on
every focus gain it installs a `WH_KEYBOARD_LL` hook (`DirectInputDevice::LowLevelKeyboardProc`, whose body is a
pure `CallNextHookEx` pass-through) and calls `SystemParametersInfo(SPI_SETSTICKYKEYS/TOGGLEKEYS/FILTERKEYS)`; on
focus loss it unhooks. The hook churn during the busy focus transition got NVDA's low-level hook timed out
and silently removed by Windows (symptoms: NVDA keys dead after alt-tab; CapsLock/OCR blocked; restarting
NVDA fixed it until the next focus change). Fix in `src/hooks.cpp`: in-process detours on
`SetWindowsHookExA/W` refuse `WH_KEYBOARD_LL` with a fake handle, `UnhookWindowsHookEx` accepts it, the three
SPI set calls are swallowed, and any hook already installed at injection time is removed. Verified: NVDA
survives focus changes, OCR works, mod speech works.

## The dev loop (working as of 2026-08-21)
Everything runs with the game **visible but never focused** and muted, so iterating never touches the
developer's screen reader. Client: `uv run tools/gd.py <cmd>` (add `--with pillow` for `shot`).
- `gd.py launch [--speak] [--nobuild]` — build, start the game unfocused with the DLL injected before init, wait
  for `/health` (45 s; a crashed instance is killed, not left in its crash-reporter dialog).
- `gd.py health | text | speech [--since N] [--wait S] | log [--since N] | buttons`
- `gd.py say "…" | mute on|off | gamekeys on|off | key enter | key a --ch a --shift | keys "text"`
- `gd.py click X Y [--button right] | mouse TYPE X Y | cursor X Y | cursor --clear | kill`
- Hot reload: `powershell -File tools/inject.ps1` (ejects via the DLL's exported `gdaccess_unload`, rebuilds,
  re-injects). Never tear down from DllMain (joining a thread there deadlocks under the loader lock).
- How it stays unfocused: `gdinject --launch` uses `STARTF_USESHOWWINDOW` + `SW_SHOWNOACTIVATE`, and with
  `GDACCESS_NOFOCUS=1` the DLL defangs the game's own `SetForegroundWindow`/activating `ShowWindow`/
  `SetWindowPos` and fakes `GetForegroundWindow`/`GetActiveWindow`/`GetFocus`. Do NOT un-minimize or restore the
  window from outside — the game activates itself on restore. `inactiveUpdateRate` must be nonzero
  (launcher patches 0 -> 30) or the engine stops ticking while unfocused.
- Input reaches the game through its own device poll (`DirectInputDevice::GetNumKeyEvents/GetKeyEvent`,
  `GetNumMouseEvents/GetMouseEvent`, hooked): synthetic events are appended to the real ones, one "frame
  group" per `Display::Update`. The cursor is overridden in both `GetCursorPosition` (screen variant
  converted with `ClientToScreen`) and raw `user32!GetCursorPos`. Measured layouts are in `src/hooks.h`.
  **MouseEvent types: 1 = left down, 9 = left up, 2 = right down, 10 = right up, 3..8/11..16 = buttons 3..8,
  17/18 wheel, 0 = per-frame idle with position.** Verified: a click on the main menu's "Create" opens the
  Create Character dialog.
- The engine dispatches events to `Engine::AddWidget` registrants (vector at Engine+0x3e8, vtable +0x10 key /
  +0x18 mouse). The exe's real mouse handler is `exe+0xbef10`; several slots are import thunks or
  `return false` stubs — **never detour those** (it crashed the game; `GDACCESS_HOOK_WIDGETS=1` re-enables the
  experimental handler hooks). The exe is SteamStub-packed on disk: `tools/dump_exe.py` dumps the unpacked
  image from the live process and `tools/exe_dis.py <rva>` disassembles it with Engine/Game export names.
- **A crashed game looks alive**: its crash handler keeps the process in a dialog, `tasklist` still lists it and
  `/health` answers "game thread did not run the job". `uv run tools/gd.py status` decides running / CRASHED
  (dialog or crash child process) / hung; run it whenever a dev call times out, before assuming a hang.
  `gd.py kill` kills only pids whose image is exactly `Grim Dawn.exe`; `tools/stacks.py` refuses any pid that
  is not the game (a stale pid can be reused by another process, and the walker suspends its threads).
- Crash triage: `uv run --with pefile tools/stacks.py` (stacks + faulting registers) while the crashed
  process is still alive. Read the cause before relaunching. `tools/dll_dis.py <dll> <name>` disassembles an
  Engine.dll/Game.dll export from disk (they are not packed) with calls annotated -- read the callee before
  guessing a struct layout (that is how Sphere = {Vec3, float}, mem::vector = {begin, end, cap} and
  "Region::GetEntitiesInSphere, not Level::" were established). `Object::GetObjectName` returns `char const*`.
  In dev routes that walk game objects, read each one under a `__try` guard (src/world.cpp `read_entity`).

## Status (end of 2026-08-21 session) and next steps
- Working: dev loop (launch unfocused/muted, hot reload, /text /speech /gui /action /click /key /gamekeys),
  engine-free core (`src/core`: MessageBuilder, strings, input manager, screen framework, ported nav graph +
  announcer + typeahead + navigator; 76 doctest cases), app layer wiring, `unsupported` fallback, and the
  screens `main_menu`, `create_character` (Name edit mode: Enter edits, letters go to the game's field with
  per-character echo, Enter/Escape/Tab/arrows leave; Next disabled until a name exists; state survives the
  Difficulty dialog's Back, resets after Cancel -- both measured), `difficulty_select` (two variants: Create/Back
  after Create Character, Accept/Cancel from the main menu's difficulty button; locked tiles read from the
  greyed label colour; Space reads the description block). Whole flow verified through the loop: main menu
  -> Create -> name -> Next -> Normal -> Create lands back on the main menu with the character selected and
  "Start" enabled (the game does NOT auto-start).
- Modifier state comes from the game's per-event flags (ButtonEvent +17/+18/+19 = shift/alt/ctrl), never from
  our own down-tracking: alt-tabbing into the game delivers an Alt key-down whose release goes to the task
  switcher, which left Alt "held" and silently killed every plain binding (fixed 2026-08-21; tracked keys are
  also reset on any foreground change). `/keydown?name=alt` then `/key?name=down` is the regression test.
- Keyboard ownership: `Screen::owns_keyboard()` / `passes_key()` decide per frame who sees key events; the
  key poll hook filters per event (`set_game_key_filter`). Foreground faking is caller-filtered
  (`caller_is_game`): game modules see the window as active, dinput8.dll sees the truth (verified by the user:
  background keys no longer reach the game; Ctrl+letter chords arrive with ctrl/shift flags set).
- Known: the name field's text is NOT captured by the RenderText2d hooks (the edit widget draws another way),
  so the name is our state only. `description()` in difficulty_select.cpp and the name-field click offset
  are measured at 1600x900. The main menu's two icon buttons bottom-left (exit at 48,871, options at 95,871)
  are not modelled yet (no text label).
- In the world (verified 2026-08-21, character "Bob", Normal): the tick runs (`Engine::Update`), the
  `unsupported` fallback speaks and passes the keyboard to the game, HUD text is captured. The game's
  "Welcome to Grim Dawn v1.3" message box (title lines + "Okay") is a good first generic in-game screen.
- In-game layer (2026-08-21, all verified through the loop): WASD is the game's own `movementType = 1`
  (`HandleActionFromJoystick` from exe+0x2c708 while moving); `src/world.cpp` reaches the player, region,
  life, camera yaw, navmesh probes and nearby entities with class names; `src/screens/in_game.cpp` runs four
  ear-fixed wall tones (miniaudio) and Ctrl+Shift+P "where am I"; `world::lock_target(id)` parks the virtual
  cursor on an entity and the game hovers/targets/attacks it natively (a click at the projection produced
  `HandleActionFromMouse(false, true, ..., id 15984)` on the training dummy). `screens/message_box.cpp` models
  the game's Okay / Yes-No boxes (untested: the welcome box was dismissed for good).
- Screens added 2026-08-21 evening, all verified through the loop: `pause_menu` (Escape in the world: Return to
  Game / Options Menu / Exit to Main Menu / Quit to Desktop; Escape = Return), `loading` (the tick DOES run
  through a load; speaks "loading" + the tip line), `message_box` (first live use: the exit confirm), `tip`
  (tutorial tips as a mod-owned overlay: the `LocalizationManager::GetText` hook records `tagQuickTip*`
  fetches, the screen recognises the popup's title line top-left, reads the lines as items, Close/Escape
  right-clicks the popup -- UNTESTED live, no tip has fired since). The `unsupported` fallback now speaks
  only after 30 frames current (transition frames were noise). Round trip world -> pause -> Exit -> Yes ->
  main menu -> Start -> loading -> world works entirely through our screens.
- Labels: `world::label_of(id)` / `/entities` give the game's own names ("Hangman Jarvis", "Chester",
  "Training Dummy", the player's name) via `Monster::GetGameDescription(false,false)`,
  `Npc::GetRolloverDescription`, `Player::GetRolloverDescription`, `Item::GetGameDescription` (u16 by value,
  hidden pointer, SEH-guarded). Wall tones: the wotr WAV loops (assets/audio/walltones, two banks), per-frame
  probes, range 10, loop gain 1.0 with a 250 ms watchdog; obstacle = walkable navmesh 2-4 units beyond the stop. Loudness (measured 2026-08-22): the WAVs are RMS-matched but
  pitched apart (south 248 Hz vs north 942 in bank 1), so per-file dB trims in `in_game.cpp` bring all eight to
  bank 1 north's -23.4 dB(A) (`audio::set_loop_gain`); `/walltones?trim=off|default|<1|2>,<n|e|s|w>,<dB>`
  adjusts live and the status line prints the table.
- Controls (2026-08-21, docs/controls.md has the full default map, read from screenshots of Options ->
  Controls): the game binds single buttons only, so "lifting to chords" is done on our side -- in the world
  the `in_game` screen owns the keyboard, passes the frequent keys straight through (WASD, 1-0, Space, E, R, U,
  Escape, Alt, F2-F7) and app.cpp's `game.*` actions map Ctrl+<default key> to an injected plain key
  (verified: plain C swallowed, Ctrl+C opens the character window). The character window's text is NOT
  captured by the text hooks (another widget path, like the bindings list and the name field).
- Tip overlay verified live (the "Character Window" tip): reads, lists lines, right-click closes -- but a
  modal game menu on top eats the click; lines are drawn twice (deduped in tip.cpp).
- Review cursor (2026-08-21, wotr's scanner keys, verified through the loop): `.`/`n`/`b`/`m` (+Shift back)
  cycle enemies / people / bystanders / objects nearest-first, speak "label, N away, H o'clock, i of n", and
  lock the virtual cursor on the landing; `i`/Enter click it; `k` = where am I. Identity = object id,
  re-found each step (wotr's continue-from rule). Enemies = Monsters that `FactionManager::IsFoe(playerId,
  id, false)` (via `GameEngine::GetFactionManager`) calls foes -- guards are Monsters too. Objects = labelled
  non-character entities only (engine helpers ScriptEntity/PatrolPoint have no label); nothing qualifies at
  the spawn yet -- chests/doors/items need a class survey. docs/controls.md has the player-facing table.
- Structured menus (2026-08-22, verified through the loop): `src/exe_ui.cpp` reads the exe's widget objects
  (RVAs + offsets from static RE with `tools/exe_dis.py`, which now resolves imports/xrefs/strings offline;
  `docs/exe-ui-layout.md` is the reference). `main_menu` (12 widget-backed items incl. the unlabeled
  Options/Exit icons), `create_character` (name read from the edit box, Male/Female/Hardcore from the toggles'
  pressed byte, Next's enabled byte), `difficulty_select` (tiles pressed/enabled, description from its text
  widgets), `message_box` (DialogManager in the world, the menu popup window at the main menu) and
  `pause_menu` (the exit window's four TextButtons pressed through its registry) no longer touch screen
  space. Round trip main menu -> Create -> Difficulty -> Start -> world -> pause -> Exit -> Yes -> main menu
  runs on those paths. `app.cpp` skips key dispatch on the frame a screen becomes current (structured
  detection is immediate; the Escape that opens the pause menu would otherwise close it). Dev: `/ui`,
  `/ui/activate?ptr=`, `/ingame`, `/dialog[?answer=]`. Synthetic `/key` events are served to the game AND
  the mod, so dev-driven Escape closes a menu dialog natively before our Back runs (not a bug for real keys).
  The exe layout check runs on first use (injection happens before SteamStub unpacks the code).
- UI pass completed 2026-08-22 (all verified through the loop): `delete_character` (prompt, the DELETE edit box,
  Accept/Cancel), `options` (7 tabs named from their rollover tags via `hooks::localize`; pages of check boxes
  with tooltip descriptions, sliders (Left/Right 5 %, large 20 %), drop-downs (Left/Right), the key-binding
  table read-only; Apply/Default/Close), `tip` (read from the exe's tip manager, Close = the right-click's
  state write), `conversation` (the conversation window: speaker, speech, rows; choosing = a click at the
  row's own rect so the step's quest actions run), `loading` (app state 10), `in_game` (InGameUI present).
  `screens/edit_field.h` (EditSession) is the shared typing-into-a-game-edit-box behaviour. `textcap` is now
  dev-only (`/text`) except the loading tip line. The navigator rerenders before rebaselining its live watch
  after activate/adjust (values were being spoken twice). Key-binding REBINDING, the Options discard prompt on
  Close (not seen yet), the Multiplayer/Network screens and the in-world windows (character, inventory,
  skills, quests, journal -- framework B, offsets in docs/exe-ui-layout.md's InGameUI map) are not modelled.
- In-world input, decided 2026-08-22 (explicitly deviating from wotr: the player is always embodied, perception
  and interaction are what the camera shows, the camera is the player's): **J = left mouse button, I = right,
  Enter = left, U = the game's own Interact (nearest usable thing within 10 units, no aiming); hold = hold.**
  The button goes down at the virtual cursor (the locked review target while the camera shows it, else the real
  cursor) and the game decides (attack / talk / open / pick up / skill; click-to-move is off with
  `movementType = 1`). Hold semantics (static RE, `re_mouse_hold.md`): the exe re-issues the command every
  10-50 ms while a button is held and tears it down on the first event with both buttons up, so
  `hooks::set_mouse_hold` injects the transition once and then patches every REAL polled event to report the
  button held (+0x10/+0x11), active (+0x18) and at the override position. Verified: holding J on the locked
  dummy walked the character 22 units to it and attacked continuously (100 `HandleActionFromMouse` calls in 3 s).
  NPC talk / pickup / use are one-shot in the game (they clear the command themselves). A locked thing the
  camera does not show: "too far away" on the key, nothing sent; the review readout says "distant" for it and
  the cursor override is only parked while it is on screen. **Camera locked** (`world::pin_camera`, per frame
  from the in-game screen; the user found zoom does not change what they hear): `GameCamera::SetZoom` at the
  far end of its range (+0x590..+0x594, current fraction +0x584) and `SetCameraYaw(0)` = north up, re-applied
  when the game drifts them; the zoom preset keys were removed. Far zoom brought the 25-unit-away dummy on screen. The Interact key (`tagUse`, action 0x36) and Pickup
  (0x37) are proximity searches, not cursor actions (`re_interact_key.md`); `tools/arc_unpack.py` reads the
  game's localized strings offline. Review groups now use the game's own predicates (2026-08-22): people =
  `Npc::HasConversation()`, bystanders = Npcs without one, objects = `FixedActor`/`Item` whose virtual
  `IsOfInterest()` is true (the Interact key's filter; slot found in each class's exported vftable, is-a via
  `RTTI_ClassInfo+0x10` parent walk) -- Hangman Jarvis is a person again; near the spawn only the riftgate
  (47 units) and a lore note qualify as objects, the locked gate and the checkpoint do not. Dev:
  `/scan?group=0..3&max=`, `/classinfo`, `/entities` marks `[FixedActor|Item interest|no-interest]`, `/key`
  knows period/comma/semicolon/... names. Open: whether wall tones / pings should follow zoom (by ear), key rebinding.
- In-world windows, first pass 2026-08-22 (survey + exe readouts in `docs/ingame-ui-survey.md`; player keys in
  `docs/controls.md`): the model layer `src/gameapi*.cpp` reads quests, factions, hot slots, lore, bags,
  equipment, skills, masteries and the character sheet through Game.dll exports (`gGameEngine`/`gEngine` ARE
  exported data; `LocalizationManager::Instance()+LocalizeWithoutParams` are public; `GameTextLine` = 0x40
  stride with the u16 string at +8; id -> object via a cached `ObjectManager::GetObjectList` sweep with a
  pre-sized buffer; virtual text builders dispatched through the object's vtable, with COMDAT-folded base
  bodies detected as "ambiguous" slots). Screens (`src/screens/window_base.h`: active = the window's IsVisible,
  Escape = Show(false), tab row + Ctrl+Tab, one column): `inventory` (InGameUI+0xbbf0; Equipment / bags /
  Stats with attribute "+"), `skills` (class selection via `SkillsWindow::SetPane` exe+0x27c580, tree per
  mastery, learn/refund = the window's own IncrementSkillLevel+SubtractSkillPoint sequence, Ctrl+1..0/J/I
  assignment), `codex` (quest tree + lore), `factions`, `vendor`, `stash`, `quest_reward`, `shrine`; in-world
  keys Q objectives, Y quickbar switch, Ctrl+1..0 read slot, G pickup (`InGameUI::HandleKeyAction(0x37)` by RVA, signature-checked).
  VERIFIED through the loop: inventory (equipment, bag read/unequip/re-equip via SmartAutoInsert, sheet,
  attribute point), skills (choose Soldier -> spend on mastery -> learn Cadence -> slot 1 and left mouse),
  codex, factions, Q/Y/G, `/cheat?xp=` (`GameEngine::CharacterExperienceOutbound`) to reach level 2. NOT yet
  seen live: vendor, stash, shrine, quest reward (no NPC of those kinds at the spawn). Lessons: a signature
  longer than the 16-byte check buffer killed the game (exe_ui now clamps); `push rdi` carries a 0x40 REX
  prefix in this exe; `EquipmentCtrl::RemoveItem` only detaches (always AddItem to the bag first); the bag
  map's pair sits at +0x1c/+0x20 (4-byte value alignment), the market map's at +0x20/+0x28. Dev routes:
  `/quests /objectives /factions /hotbar /lore /inv /skills /sheet /obj /loc2 /cheat /ingame?action=`.
- Quickbar + skill targeting (2026-08-23, `docs/skills-targeting.md`, verified live on the test character):
  bare Y is now the game's own Quickbar Switch (passed straight through; `screens::quickbar_tick` watches
  `exe_ui::quickbar_page()` and announces the new bar "quickbar N" -- verified). Reading moved to Ctrl+1..0 =
  read slot 1..10 of the displayed bar, Ctrl+- / Ctrl+= = the left / right mouse skill; each says the skill
  and how it aims. **Aim = `world::skill_aim`**: `SkillActivated::GetTargetType()` returns `*(int*)(this+0x5c0)`
  (never overridden, called directly after an is-a `SkillActivated` guard). The RUNTIME enum is NOT the DBR
  targetingMode order -- read off the game: **1 = self/buff** (Overguard, potions, Field Command), **2 =
  offensive** (Weapon Attack, Cadence, Blitz, Forcewave, War Cry), **3 = ground point** (unconfirmed;
  Evade/Move To are actually 0), **0 = passive/none**. Value 2 is split by the concrete RTTI class name
  (Radius -> "around you" for War Cry, else "at a target"). Verified via `/hotbar` (prints `aim=`) and
  `/action read.leftMouse` -> "left mouse Weapon Attack, at a target". **Input:** `Ctrl+<digit>` is a read
  chord but a digit is also a passthrough key, so `app.cpp`'s game-key filter swallows 0x02..0x0b while Ctrl is
  held (else a real Ctrl+1 reads AND activates slot 1). Synthetic `/key` bypasses that filter and always
  reaches the game, so in-world Ctrl-chords can't be verified through `/key` -- use `/action`. Verified with a
  levelled test char (cheat XP -> learn Cadence/War Cry/Overguard/Forcewave): reads on the displayed page give
  "1 Cadence, at a target" / "2 War Cry, around you" / "3 Overguard, self" and are page-relative (Y switches
  page, reads follow). **Mouse assignability**: the assign API (SetPrimarySlot) accepts ANY learned skill on
  the mouse incl. a self-buff (Overguard -> "left mouse Overguard, self"); the earlier non-stick was UNLEARNED
  skills, not a self-buff rule. Open: confirm real-key Ctrl-chord dispatch on the user's own keyboard (synth
  can't).
- Recover a basic attack (2026-08-23, verified): the inventory Equipment tab is now an `AssignSource` -- with a
  weapon hand slot focused (EquipmentCtrlLocation 9 Right Hand / 10 Left Hand) the assign keys (Ctrl+J / Ctrl+I
  -> left / right mouse, Ctrl+1..0 -> a slot) put the character's **default basic attack** there. The id comes
  from `gameapi::default_skill_id(0)` = `SkillManager::GetDefaultSkillId(DefaultSkill)`, the game's OWN accessor
  (Game.dll export, virtual `[SkillManager+0xe0]`): it SEARCHES the live skill list, so the default-attack id is
  per-character AND weapon-dependent -- NEVER hardcode it (`PlayerHotSlotCtrl::SetToDefaults` calls
  `GetDefaultSkillId(0)` for the left mouse the same way; there is no integer sentinel). Verified: broke the left
  mouse (Overguard), focused Right Hand, assign restored "Weapon Attack" on both buttons; a non-weapon slot says
  "nothing to assign". Deliberate small-scope stand-in for a full hotbar manager. Full reset available via
  `PlayerHotSlotCtrl::SetToDefaults` if wanted later.
- Equip-by-list, hotbar manager, two weapon sets (2026-08-23, `docs/skills-targeting.md`/`controls.md`, all
  verified live on the test char). **Reusable picker** `src/screens/list_picker.{h,cpp}`: a layered overlay
  (layer 30, `open_picker(title, items, on_pick)`); closing re-exposes the launching screen with focus on the
  slot it came from (the existing layered-screen focus restoration -- no `push_child` needed). **Equip picker**:
  Enter on an equipment slot lists every bag item across all bags that fits (`gameapi::can_equip` ->
  `EquipmentCtrl::CanItemBePlaced`), first entry "empty" = unequip; Backspace still unequips directly. Weapons
  go to the ACTIVE weapon set (equip() uses the current EquipmentCtrl). **Hotbar manager** (Ctrl+`,
  `src/screens/hotbar_manager.{h,cpp}`, layer 1 so game windows cover+close it; closes on on_unfocus unless our
  picker is up): lists the current set's two bars (indices 0-9, 14-23), activate a slot -> skill picker (clear
  [id 0 -> `assign_skill_to_slot(idx,0)`] + `gameapi::assignable_skills()` filtered to `level>0` and
  `world::skill_aim != None`, so learned activatable skills only, passives/masteries excluded, item-granted
  included). **Weapon swap = F** (`gameapi::swap_weapon_set` -> `ControllerCharacter::SetAlternateEquipment`;
  the game's "Switch Weapons" is unbound); a weapon set is ONLY the two hands (+0xe0/+0x138), everything else
  shared. The swap propagates to the equipment view a frame later, so the announce ("weapon set N, <right>,
  <left>") is deferred 3 ticks (`weapon_swap_tick` from the in-world tick). Verified: armed set A=Gladius,
  B=Splintered Club independently, F swaps and each keeps its own; equip picker filters correctly; manager
  assigns learned skills + focus returns to the slot. New gameapi: `can_equip`, `swap_weapon_set`,
  `item_skill_ids`, `assignable_skills` (+ exports GetItemSkillList / Controller Get/SetAlternateEquipment).
- Skills: spirit-guide reclaim, requirement-gated learning, modifier targets, stat tooltips (2026-08-24,
  `docs/skills-targeting.md`, all verified live on the test char). **Learning respects requirements**:
  `gameapi::can_learn_skill` replicates the game's SkillReasons gate (exe+0x2492b0) -- points>0, level<max,
  `Skill::GetMasteryLevel >= GetMasteryLevelRequirement`, and a modifier's base skill learned -- and `learn_skill`
  refuses with the spoken reason ("needs mastery N", "requires Cadence", "no points"). **Modifiers name their
  base**: `Skill::GetModifiedSkillId` reads 0 for tree modifiers, so `skills()` reverses each base's
  `Skill::GetModifiers()` into `SkillInfo::modified_skill_id` -> "Discord, modifies Cadence". **Refund only at a
  spirit guide**: talking to an `NpcSkillReallocator` calls `GameEngine::DisplaySkillReallocationWindow`, setting
  the reclaim flag at **skills window +0x1f4c** (the click handler exe+0x248380 reads it as `[controller+0x1e1c]`,
  controller = window+0x130); `exe_ui::skills_reclaim_mode()` reads it. In reclaim mode `screens/skills.cpp` shows
  a hint row + each spent skill's "N iron bits to reclaim" (`SkillManager::GetCurrentSkillReclamationCost`) and
  Backspace reclaims one point; outside a guide Backspace does nothing (fixing the old refund-anywhere bug that
  silently charged bits). `can_reclaim_skill` gives the reason: the mastery bar reclaims down to 1 like any skill
  (base `DecrementSkillLevel`) but its last point is blocked (`tagDecreaseMasteryError`), and a reclaim you can't
  afford (`reclaim_cost() > money()`) says "not enough iron bits". **Stats
  tab**: every row now carries the game's own tooltip on Space (`tagCharAttributeDescription0X`,
  `tagCharStats{OA,DA,DPS}Description`, `tagStatsResistance0XDesc`); attributes remain non-refundable (no reclaim
  path wired). Dev: `/reclaim` opens reclaim mode without a guide; `/cheat?bits=N` tops up iron bits. New exports:
  `Skill::GetMasteryLevel`, `Skill::GetModifiers`, `SkillManager::GetCurrentSkillReclamationCost`,
  `GameEngine::DisplaySkillReallocationWindow`, `Character::AddMoney`.
- Lua (2026-08-22, `docs/lua.md`): LuaJIT 2.0.4 (`x64\lua51.dll`) behind the LUAGLUE binding layer; one
  state owned by `LuaManager` at `*(gEngine+0x68)`; `LuaManager::RunCode` is exported (dev route `/lua?code=`,
  readback via `Game.AddObjective` -> `/objectives`). Sandboxed (no io/os/ffi/debug/require) but `loadfile`
  reaches real paths. It is the quest/level scripting API (tokens, quest state, spawns, doors, teleports,
  banners, XP/items) -- nothing UI-side; everything it does our export calls can do too.
- Rooms (2026-08-22, `docs/rooms.md`, verified through the loop): the game's baked navmesh (Detour tile-cache
  layers inside `world001.map`, 0.25 units/cell, world coordinates; quest gates are runtime obstacles on
  top) is segmented OFFLINE per region (wotr's watershed + an axis-aligned walk-time cap; roads are an
  overlay only) and shipped in `assets/rooms.db` (vendored SQLite, `src/db.*`, read-only). `src/rooms.cpp`
  looks the player up per frame (chunk -> region -> label grid, 8-cell ring, 400 ms dwell) and announces
  place changes in Zira ("Devil's Crossing, room 193"); X = description, V/Shift+V = exits (parks the review
  cursor on the opening via `world::lock_point`). Vocabulary: region = the game's named area, sub-region =
  ours, room = a watershed piece, chunk = the engine's `Region`. Dev: `/room`, `/regions`, `/portals`,
  `/navprobe`. Lessons: `Region::IsUnderground` calls `LoadLevel` (never sweep it); a dev route that outlives
  the 8 s job timeout must own its state (`run_on_game_thread` and `serve()` fixed 2026-08-22). **`World::GetRegionContainingXZ(from, x, z)` takes x,z RELATIVE TO `from`** (world.cpp
  `region_containing_xz` converts; it only looked right in chunk 0A001 whose offset is zero). **Never move the player with
  `Entity::SetCoords`**: it is a raw 0x40-byte field write + `OnMoved()`; the level bookkeeping is in
  `World::SetCoords` and the player's per-frame update (`Engine::UpdateForcedEntitiesInPlayerLoadSphere` ->
  `Entity::Update` -> `ControllerPlayer::Update`) only reaches entities registered in the iterated region, so a
  raw SetCoords stalled the controller (WASD dead, `in_world()` false -> "unsupported screen") and, once the
  stale level was torn down with the controller object, crashed the exe's mouse handler in `SetMoveCommand`.
  `/teleport` uses the game's own `Character::TeleportToLocation(WorldCoords const&)` (Game.dll, exported:
  `ControllerCharacter::Teleport` -> `World::SetCoords` + `NavManager::ResetObject`; axes taken from the
  character), tries rising PutOnFloor start heights and refuses landings the navmesh rejects (verified: 60
  controller ticks/s after same-chunk, cross-chunk and hop-tour teleports). The Lua `Game.TeleportPlayer` is
  the riftgate fade activity (integer coords, async), not a dev teleport. The game pauses single player when it
  believes the window lost focus (a hot reload in the world can leave it paused: `/pause[?set=0|1]`,
  `GAME::Pause/UnpauseGameTime`; `/player` shows `paused=`); `in_world()` no longer needs a live controller. **Devil's Crossing is
  authored** (2026-08-22): 15 sub-regions, 201 of 202 rooms titled + described by `tools/workflows/rooms_author.js`
  (Opus agents over `tools/shots.py` screenshots + `tools/author.py facts`; `docs/rooms-description-rules.md`; a
  mechanical `author.py check`, a sampled reviewer, then a per-sub-region consistency pass -- the per-room
  writers never see their neighbours, so confusable titles and fixtures named from the next room are fixed there;
  ~13M Opus tokens for the region). `devilscrossing:-70:-183` is orphan (its anchor is bake-only; the live
  mesh refuses it). **Hargate's Isle** (chunk 0W021, boat-only via the Row Boat DungeonEntrance) was restored by
  the by-record chunk fix and authored as its own sub-region (14 rooms; 216 total). The dev character dies while posing for screenshots (level 2 among monsters): the shots run
  toggles Lua `ToggleInvincible`, and a shot whose outline is <30 % visible is the tell of a respawn.
  **Lower Crossing + Burial Cave authored** (2026-08-23, covers the first quest): 212 rooms / 12 sub-regions
  (Burial Hill, The Hidden Path and the mires; the sub-region agent reads the game's area names off the
  shots' minimap corner) and the cave as its own region key `burialcave` (`rooms.py area --only --key`;
  25 rooms / 5 sub-regions), all verified; reviewer findings fed the consistency pass as notes. Pipeline
  hardening from those runs (shots.py): approach-then-wait teleports (chunks stream around the PLAYER),
  grid-sampled stepping stones instead of room anchors (the east cliffs are a walkable island 170
  anchor-units off the mainland), `/teleport` calls `Region::BackgroundLoadLevel` for unloaded chunks
  (off-map dungeons like the cave never stream by proximity; no-route targets get force-load + direct
  jump), in-process `gd.capture()`, black-frame retry, open-window guard, `ToggleInvincible(true)` at
  region start (a setter despite the name), flushed per-room timing (~3 s/room). Describers return
  `shots_suspect` and flagged rooms are re-shot + re-described. **Height handling** (2026-08-23, db schema
  v2): stacked walkable layers are tiny in practice (DC 72 m² of ramparts/bridges, the cave one 20 m²
  overpass; the prison's stacked interior is not in the walkable bake at all), so the base plane keeps the
  LOWEST layer + a per-cell floor y (`grids.heights`, RLE int16 decimeters) and the upper layers are sparse
  overlay cells (`grids.overlays`) labeled offline by height-continuity BFS (`gdmap.rooms.resolve_overlays`:
  an overpass joins the room it continues; an enclosed upper floor stays -1 = announce nothing, never the
  room below). Runtime `LabelGrid::label_at(x, z, y)` picks base vs overlay by which floor y is nearer;
  layers closer than 0.9 units are one floor. Verified live in the cave: (1234.9, -469.9) at y 7.7 says
  "stacked coffin floor", the base at -0.5 is "vine curtain gallery"; all 453 authored rooms re-attached
  by anchor on the schema regen (kept=all, orphaned=0). Next: road helpers, the next regions, region names
  from `tagWorldMap*`.
- Runtime exits + a corrected segmenter + a full retag (2026-08-24, `docs/rooms.md`; verified live). Root
  cause of the exit bugs (the user's report: a false cobbled-house-yard<->wrecked-house-gap door, the
  islanded riftgate): the segmentation ran on the baked Detour **tile-cache**, which is flat (we took the
  lowest layer), per-tile seam-eroded, and blind to off-mesh links -- so exits were false (2-D adjacency
  across a wall/height), missing (seam erosion islanded rooms; stairs/quest doors are off-mesh, absent from
  the bake), or duplicated. The map holds ONLY the tile-cache (RLTD blobs, no assembled navmesh); the game
  builds the real 3-D navmesh from it at load. Two changes:
  1. **Exits are computed LIVE at runtime, not stored** (`src/rooms.cpp exit_items`, `world::find_path` =
     `Player::FindPath`, dev route `/findpath`; `core/rooms_model LabelGrid::neighbors_within`). On V / room
     change: every OTHER room whose cells fall within `kExitRadius`=28u (a radius) and that the game's own
     pathfinder can reach is an exit, bearing to its nearest cell. Correct for quest state / doors /
     destructibles (`NavBlocker` = `dynamicblocker_invisible`; `FixedItemDoor`; `Destructible` = breakables --
     all detectable via the sphere query + RTTI name, gate `find_path` exactly as they gate the player) and
     height (find_path drops unreachable stacked floors). **Line-of-sight is NOT required** -- the bearing may
     point through a wall at a room reached by going round (decided with the user; same as the old adjacency
     exits). But the **route must be DIRECT** (2026-08-24, on the user's report that "in A, can directly reach
     B" is the rule, not "B is near and reachable"): each reachable candidate's actual navmesh corridor
     (`world::find_path_corridor` = `NavManager::FindPath`, whose 7th arg is a `mem::vector<WorldVec3>` straight
     path -- all four scalar out-params null-checked, Engine+0x10c2c0; dev `/findpath?...&corridor=1`) is
     classified cell-by-cell against the label grid (`core/rooms_model LabelGrid::path_is_direct`, engine-free +
     unit-tested) and DROPPED if it lingers > ~2u inside any room that is neither the current room nor the
     candidate (a third room = that room's exit, not ours). Off-grid/unlabelled samples don't count as a detour.
     **Strictly additive**: dropped only on a positive detour finding; if the corridor can't be computed the
     candidate is kept (reachability already passed), so a hiccup never loses a real exit. Cross-region
     (foreign) exits still come from the stored `seams` rows (the far room is in another region's grid). The
     offline `exits_of`/`fill_label_seams`/`exits` passes are now vestigial for intra (the mod ignores stored
     intra rows); `seams --write` is still required after any `area --write`.
  2. **Seam-stitched segmenter** (`gdmap.rooms.bridge_walk_seams`, run in `tools/rooms.py build_area`):
     stitches walkable cells across internal tile seams so segmentation matches runtime connectivity, healing
     the riftgate + ~10 island rooms per region. Validated cell-by-cell vs the live navmesh (every bridged
     courtyard cell was on the runtime path mesh; `max_gap`=6, height <=1.5u).
  **Full retag from scratch** on the new segmenter (~30M Opus tokens, ~1.5 h): resegment (`area --write`) ->
  delete dead rooms (anchors moved, keys not in the grid's label_keys) -> re-shoot -> `rooms_author.js`
  (subregions + describe + consistency) per region. DC 195/197 titled (2 untitled = genuinely-unreachable
  off-mesh cliffs), LC 198/198, Burial Cave 22/22; 417 rooms, then `seams --write`. Not fixed (deferred,
  perceived-place segmentation quality, not a bug): boundaries at watershed narrowings through open ground can
  feel arbitrary (cobbled<->wrecked is a real narrowing). Note: the prison courtyard has stacked floors
  (~y3.6 under ~y7); dev `/teleport` floors from the char's incoming y and can land on the lower one (looks
  exitless) -- a tool artifact, the rooms are keyed to the y7 floor the player walks. The mod loads `rooms.db`
  from next to the DLL (`build/ninja/assets/`, copied at build time); a hot copy + `/room?reload=1` tests a db
  edit without a rebuild.
- Riftgate travel screen (2026-08-22, verified live through the loop incl. a real trip Lower Crossing -> Devil's
  Crossing): `screens/riftgate.cpp` over the world map in riftgate mode (`exe_ui::riftgate_*`, layout in
  docs/exe-ui-layout.md "Riftgate travel"). Review groups: N = people + non-loot objects of interest, M = loot.
  Sonar field (`src/sonar.cpp` + `src/core/sonar_field.{h,cpp}`, 2026-08-24, replaces wotr's left-to-right
  sweep -- which read as noise with GD's fixed viewpoint and fast enemies): enemies / loot / dungeon entrances
  each REPEAT their own cue, the period shrinking as the thing nears (logarithmic in distance, so nearing
  matters most up close: 0.14s@2u..0.80s@25u, the far end 2x the old 0.40s sweep cadence) and their left/right position offsetting the phase (50 % from the
  left = half a period late) so co-distant things stagger instead of merging; rear high shelf on every spatial
  cue; `/sonar` (knobs `pnear pfar dnear dfar radius vol ref floor force`). **One distance curve for every
  positioned cue** (`world::ear_frame`: ref/(ref+dist), ref 10 units, floor 0.2; `/sonar?ref=&floor=` tunes it
  for the review pings too) with only a channel volume per system (sonar `vol=`, 1.0) -- so distance is doubly
  encoded, closer = faster AND louder. Labels: every Actor through the virtual GetGameDescription.
  Corpses are not enemies (`Character::IsAlive`). Main-menu character selection is a middle Tab stop of the
  main menu (the exe's CharacterPicker, docs/exe-ui-layout.md). **The virtual cursor parks at the entity's
  bounding-box CENTRE** (`Entity::GetRegionBoundingBox` = {centre, half-extents}, region frame): the fixed +1.0
  lift put the cursor above ground items, the click hit the ground and fired the default attack (2026-08-22;
  verified: a lore note and the training dummy both resolve by id in `HandleActionFromMouse`). Ground EQUIPMENT still
  resolves nothing (sighted players click its floating label): J on a reviewed Item issues
  `ControllerPlayer::ItemAction` after `SetCommandRepeated(false)` (docs/re_pickup.md; verified walking 16 units
  to a note). `/jkey?down=1|0` presses J without the game seeing a J key (synthetic /key goes to both).
- Release-prep pass (2026-08-25, all verified live except where noted): (1) **q objectives fixed**
  (`screens/codex.cpp speak_objectives`): the old walk read every tracked quest's every task and surfaced
  their "return to X" turn-ins (the user's "arbitrary / return to xyz" bug). Now prefers
  `GameEngine::GetObjectives` (the game's HUD list, localized) and, when it is empty -- which it is across much
  of the campaign; the old code comment was right, the disasm claim that the HUD renders from it does not hold
  live -- falls back to each tracked incomplete quest's CURRENT step (first not-complete task with an
  unsatisfied objective). Verified: a char with "Something For Nothing" (kills done) reads "Return to Harmond",
  not the whole tree. (2) **Map picker merged to one flat nearest-first list** (`screens/map_markers.cpp`): the
  quest/non-quest tab split was vacuous (`world::map_markers` hardcodes `quest=false`). Verified live (1 of 23).
  (3) **Enter no longer leaks a world click** (`screens/in_game.cpp`): a close-on-pick screen (map, pause
  "Return to Game", conversation close) re-exposed `InGameScreen` with Enter still physically held, and its raw
  poll turned that into a left-click. Fix: `on_focus` latches a left/right mouse key already held on entry and
  swallows it until released once (built; needs a real keyboard to feel). (4) **Component/augment attach**
  (`gameapi_items.cpp is_component/compatible_items/attach_component`, `screens/inventory.cpp`): activating a
  component (records/items/materia = `ItemRelic`; none ship outside that folder) in a bag opens a picker of
  every item it fits via the game's own `Player::GetCompatibleItems` (bags + equipped + stash); picking one
  calls `Character::UseItemOn(player, comp, target, ItemSource, 0,0,false)` -- a pure inventory op, NO
  blacksmith (the NPC path is removal only). Verified live: Chilled Steel 3->2, target's tooltip shows it
  attached. Arg order confirmed. Dev: `/inv?compat=<id>`, `/inv?attach=<id>&target=<id>`. (5) **Evade
  understood, left as-is** (docs/evade.md): evade aims by the WASD movement vector while moving, else a
  standing-still fallback that reads the cursor -- and the mod already parks the virtual cursor on the locked
  review target, so evade lands on a good scheme with NO mod: WASD held -> dodge that direction, standing ->
  dodge toward the locked target (confirmed by the user in play). A diagnostic hook on the exported
  `ControllerPlayer::EvadeAction` forwarder was tried and removed -- it never fired (the exe calls the state's
  `RequestEvadeAction`/`DefaultRequestEvadeAction` directly; that is the seam if we ever force the direction).
- Settings for a shipping user (audited 2026-08-25): only ONE mandatory options.txt change, `movementType = 1`
  (Controls -> Keyboard); nothing in the repo sets it (NOT auto-forcing it: the engine rewrites options.txt on
  exit and the in-memory Options setter is unconfirmed). Plus keep default keybindings and `displayDamage` on.
  inactiveUpdateRate / windowed / 1600x900 / targetLock are dev-loop artifacts, irrelevant to a focused player.
  Game-free CI is feasible (nothing links a game file; `gd_names.h` + `rooms.db` are committed; only prism must
  be fetched). Runnable-by-others gaps: a non-CLI injector/launcher, and gating the dev HTTP server out of release.
- Next (needs the user's hands): player-facing targeting keys
  (nearest enemy / cycle / announce name, distance, direction -- the hover name arrives as `box_font` HUD text),
  an attack key that clicks the locked target, wall-tone tuning by ear, hover sounds, the main menu icon buttons.

## Build / run
- Toolchain: VS 2022 Community (MSVC 14.44), Ninja (from VS), CMake. `tools\vsdev.cmd <cmd>` runs a command
  inside `vcvars64`. `tools\build.cmd` configures (first time) and builds into `build\ninja\`.
- `powershell -File tools/inject.ps1 -Launch` — build, then start the game with the DLL injected before it
  initializes (sets `SteamAppId` so the Steam stub does not relaunch; follows a relaunch if it happens).
- `powershell -File tools/inject.ps1` — hot reload into the running game: ejects the old DLL (so the linker
  can overwrite it), builds, re-injects. `-Eject` unloads. `-NoBuild` skips the build.
- Log: `%LOCALAPPDATA%\gdaccess\gdaccess.log` (truncated on each load). Speech lines are logged as `[speak]`.
- Dev hotkeys (polled with GetAsyncKeyState, only while the game is really foreground): F10 alive line,
  F11 toggle announcing newly appearing text, F12 read all on-screen text.
- Speech: prism (prebuilt SDK in `third_party/prism-bin`, delay-loaded from next to gdaccess.dll).
- Hooking: Microsoft Detours, vendored source in `third_party/Detours`, built as a static lib.

## Tools (Python: `uv run tools/<script>.py` -- the repo root `pyproject.toml` declares lz4/pefile/capstone/numpy/scipy/pillow, no `--with` needed)
- `tools/rooms.py` + `tools/gdmap/` (arc, map header, level bodies: navmesh tile layers + terrain layers,
  segmentation, renderer, `roomsdb.py`) -- the rooms pipeline, `docs/rooms.md`. `rooms.py area
  devilscrossing --write` regenerates `assets/rooms.db`; floor plans in `build/rooms/`.
- `tools/gen_exports.py` — dumps `.def` files and undecorated export listings from the installed DLLs into
  `tools/exports/` (regenerate after a game patch).
- `tools/gen_names.py` — resolves the exports we hook by regex over the undecorated listing and writes
  `src/gd_names.h`; fails loudly if a pattern does not match exactly one export.
- `tools/arz.py <record-path-regex> [field-regex]` — reads `database.arz` offline (records + their fields).
- `tools/stacks.py [pid|exe] [n]` — native stack dump of all threads via dbghelp; `tools/pe_survey.py`,
  `tools/dinput_hook_scan.py` — static analysis helpers; `tools/hookmon.py` — LL keyboard hook monitor.
- Reference implementations in `reference/`: `iagd` (injected Detours hook DLL for this game, MIT) and
  `GDCommunityLauncher`.

## Design rules (agreed 2026-08-21)
- **Menus one by one, no crawlers, no screen space.** Each game screen is a dedicated `Screen` subclass
  declaring its graph over OUR model of that screen (the controls it has, in player-sensible order). Identity,
  labels, state and activation come from the game's own objects through `src/exe_ui.h` (the exe's two private
  widget frameworks, reached by base-relative layout; `docs/exe-ui-layout.md`) or exported calls
  (`DialogManager` for message boxes): a button is pressed through its listeners / its window's registry,
  never by a click at a drawn label or a measured pixel. Text capture (`textcap`) is a dev discovery tool
  (`/text`) and the fallback's name source only. Unknown screens get the honest "unsupported screen"
  fallback; a game build whose code bytes fail `exe_ui::available()` gets "game version not supported" once.
  No screen clicks at drawn text any more; the two remaining synthesized clicks land on a widget's OWN rectangle
  (an edit box taking focus, a conversation row) because the game's handler for them has side effects we
  must not bypass.
- **No player-facing "focus mode".** Who gets the keyboard is decided every frame by the screen stack:
  a modelled screen declares `owns_keyboard()` (default true; the game then sees no key events and we drive
  it by clicks/calls), the `unsupported` fallback and any screen where the game itself must handle keys
  (in-game movement, typing into a game text field) return false and every key passes through. The player
  never toggles anything; `/gamekeys` is a dev override that holds until the next screen change.
- **We own the UI state.** Which screen is open, what is focused, what the choices are: ours. We drive the
  game to match and read it back only at defined checkpoints. The game's drawn UI is not a state store;
  nothing user-facing derives from screen-space round trips.
- **MessageBuilder everywhere.** Anything spoken with more than one part is composed through
  `gd::core::MessageBuilder` (fragments space-join, list items comma-join, single use). Never hand-build
  ", " in a speech path; never add a second joining helper. All mod-authored wording lives in
  `src/core/strings.h`; composed shapes are `push_*` helpers there. Game text passes through verbatim.
- **English only** (decided). No localization layer.
- **Speech never interrupts by default**; interrupt only on focus moves and synchronous state feedback.
- **Never hold a `GraphNode*` across frames.** The graph is rebuilt immediate-mode; a stored pointer dangles
  after the next render (the navigator's `last_spoken_node_` crashed the game in the announcer's path walk
  on a Tab landing, 2026-08-21). Remember a `ControlId` and resolve it in the current render when needed
  (`GraphNavigator::last_spoken_node()`).
- `src/core` is engine-free (no Windows, no game types) and unit-tested with doctest
  (`cmake --build build/ninja --target gdcore_tests && build/ninja/gdcore_tests.exe`). Re-run the CMake
  configure after adding files (globs).
- Playability milestone: character creation -> main game with WASD movement, wall tones, and a real
  understanding of how the game targets.

## Conventions
- Hook by exported name, never by signature scan. Member functions: `this` first; class-by-value returns
  (`std::basic_string`) use a hidden return pointer as the 2nd argument. The game uses
  `basic_string<unsigned short>` (not char16_t) — see `src/msvc_string.h`.
- All engine calls on the game thread (inside `Display::Update` or a hook). Speech may be called from anywhere.
- Localize every string the mod speaks (rule inherited from wotr-access; not yet wired up).
- The user is blind: no focus stealing, no synthesized input, no launching the game from Claude without asking.
