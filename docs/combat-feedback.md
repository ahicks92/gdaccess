# How Grim Dawn shows combat information (static RE, 2026-08-22)

Read from Game.dll / Engine.dll exports and disassembly (`tools/dll_dis.py`, `tools/dll_xref.py`) and the
unpacked exe (`tools/exe_dis.py`). The combat-text event layout (section 1) was CONFIRMED LIVE on
2026-08-22 by hooking `EventManager::Send` (src/combat.cpp, `/combat?raw=`): every offset matched; the
style field holds the resolved record path ("records/ui/styles/text/style_fl...") rather than the variable
name, +0x68 is a pointer and +0x70.. is more state the exe reads (unused). Implemented: src/combat.cpp +
src/voice.cpp speak the outgoing text in Mark, panned; health steps and the H key in Zira.

## Summary
- The only per-hit text the game draws is the **floating number over an enemy you (or your pet) hit**, plus
  the words Miss / Dodge / Block. Incoming damage has NO text: your health bar is the only feedback.
- Those numbers are **Engine events of type 0x1b** sent by Game.dll through the exported
  `EventManager::Send(GameEvent const*, unsigned)`; the exe's HUD listens and draws them. Hooking `Send` and
  filtering type 0x1b gives exactly what the game would have shown, already formatted.
- **Damage taken / dealt as data**: `CombatManager::ApplyDamage(float amount, PlayStatsDamageType const&,
  CombatAttributeType, mem::vector<unsigned> const& attackerIds)` (exported, `this` = the victim's
  CombatManager, `GetCharacter()` = victim) runs for every character, every hit, every tick of a DoT.
- **There is no player-facing combat log.** There is a developer one: the exported global
  `GAME::gLogCombat` (`?gLogCombat@GAME@@3IA`, unsigned) is read in 84 places and written nowhere; nonzero
  makes the combat code call `Engine::Log(LogPriority, channel, fmt, ...)` (Engine vtable slot 2, exported)
  with designer lines ("Damage (%f) Result (%f)", "^yDodge Chance (%f) caused a miss", "attackerName = %s",
  "regionHit = %s", "criticalStrike = %f", per-skill ModifyDamage math). Verbose, .dbr names, not for players.
- Other channels: `GameEngine::AddUINotification` / `GetNumNotifications` / `TakeTopNotification` (the HUD
  notification strip: level up, "Enemy Hero Killed", quest updates) and `ControllerPlayer::SetUserText(tag,
  time)` / `GetMailboxUserText` / `PopUserText` (the red popup line: "That skill is not ready",
  "Energy Too Low"; option `errorMessages`). Tallies: `GameEngine::GetTotalDamageDone(attackerId)`,
  `GetDamageByAttacker(victim, attacker)`, `GetPlayerDamagePercent`; per-player `PlayStats`
  (`GetLastMonsterHit`/`GetLastMonsterHitBy` (name, amount), `GetHitsReceived`, crits, kills by class, deaths).

## 1. Floating combat text (what the sighted player sees)
Emitted only by `CombatManager::TakeAttack(ParametersCombat&, SkillManager&, CharacterBio&)` (Game.dll
+0x10a650), the hit resolver, in these cases (each builds a `GameEvent` on the stack and calls
`Singleton<EventManager>::Get()` then `EventManager::Send(&ev, 0x1b)`):
- attacker missed (PTH roll) -> text `tagMiss` ("Miss"), style from game-balance var `missStyle`
  (`petMissStyle` when the attacker is not the main player);
- dodged (`Dodge Chance`) -> `tagDodge`; blocked -> `tagBlock` (`petBlockStyle`);
- a hit -> `Localize("HitFormat", dmg)` = `{%.0f0}` with `hitStyle`/`petHitStyle`; a crit ->
  `Localize("CriticalHitFormat", dmg)` or, with option `critMultipliers` (`Options::GetBool(3)`),
  `Localize("CriticalHitFormatMult", dmg, mult)` = `{%.0f0} (x{%.2f1})`, style `GameEngine::GetCritTextStyle(tier-1)`.
- Option `displayDamage` (Options -> "Display damage numbers when you hit enemies") gates the whole thing.
- The size of a number is `Player::RegisterCombatTextHit(dmg)` / `RegisterCombatTextCrit(dmg)` (virtual,
  Character vtable +0x870 area): they keep a running average of YOUR hit sizes in Player+0x4ce8/+0x4cec
  (sum, count) and return a clamped scale, so bigger-than-usual hits draw bigger. Not emitters.

The event (stack record in TakeAttack; `rbp = entry_rsp - 0x4d8`, event at `rsp + 0x50`):
- +0x00 u32 type = 0x1b; +0x04 u32 (an entity/attacker id; 0 on the miss path)
- +0x08 WorldVec3 (Region* + Vec3): the victim's `HeadEffect` attachment (`Entity` vtable +0x178 with the
  `Name` "HeadEffect"), i.e. above the head
- +0x20 std::string (char) style variable name ("missStyle", "hitStyle", ...)
- +0x40 std::basic_string<unsigned short> the text as drawn ("Miss", "123", "456 (x1.50)")
- +0x60 u32 0x46 (normal) / 0x85 (crit) -- a text class; +0x64 float scale (1.0 for words)
- the exe's handler also reads +0x70 (double), +0x78 (int), +0x7c (byte) -- likely the raw damage, crit
  tier and a flag; unconfirmed.

exe side: the HUD object constructed at exe+0x1fb20 registers a handler with type 0x1b (handler vtable
exe+0x309828, slot 0 = exe+0x2e270 -> exe+0xad80 appends a 160-byte entry to the vector at hud+0x08/+0x10
after projecting the position through `GameEngine::GetCamera()`; `Engine::GetUtilityFontStyle` picks the
font). exe+0xb410 (called from exe+0x2d106 each frame) ages entries (+0x50/+0x54 ms timers, 1000 ms life),
re-projects them over the entity (+0x40 entity id, +0x68 position) and draws. Pure renderer; nothing to
read there that the event did not carry.

## 2. Damage as data (the hook points)
Pipeline for every hit, on the victim: `Character::ApplyDamage` -> `CombatManager::ApplyDamage` ->
`Character::SubtractLife` -> `GameEngine::RegisterDamage(attackerId, victimId, amount)` (-> per-victim map
at GameEngine+0x1b30 keyed by the master attacker (`GetMasterAttacker`: pets roll up to their owner), only
for Player-class attackers) -> on death `SkillManager::OnEnemyDeath`, `Skill::ActivateOnEnemyDeathSecondarySkills`.
- `CombatManager::ApplyDamage(float, PlayStatsDamageType const&, CombatAttributeType, vector<unsigned> const&)`:
  amount is the post-mitigation float; `CombatAttributeType` is the damage kind (Physical, Fire, Cold,
  Lightning, Poison, Aether, Chaos, Vitality, Bleeding, Pierce ... -- the enum has ~0x38 values; the display
  tag per value is `DamageAttribute*::GetTag(CombatAttributeType)`, a `char const*` localization tag);
  the vector is the attacker ids (one per hit, more for multi-source). Returns false when nothing applied.
  Duration damage ticks come through the same call (`DurationDamageManager`). Hook this and split by
  victim == player / victim's attacker == player for "taken" vs "dealt".
- `CombatManager::TakeAttack` is the level above (one call per attack, before mitigation): knows miss /
  dodge / block / crit and the `ParametersCombat` (attacker id at +?; combat type strings "Ranged Attack",
  "Retaliation Attack" are logged from it). Hook if miss/dodge/block need speaking; otherwise ApplyDamage suffices.
- Life totals: `Character::GetLife()`/`GetLifeMax()` already used by `world.cpp`; `Character::GetLifeStateAsText`.
- Kills: `GameEngine::OnCreatureDeath(GameEvent_CreatureDeath const&)`, `PlayStats::IncrementKills(...)`,
  `PlayStats::SetLastMonsterHitName/SetLastMonsterHitByName(name, amount)` (PlayStats+... per player; the
  stats page's "Last hit" lines).

## 3. Notifications and popups (exported, pollable)
- `GameEngine::AddUINotification(UINotification::Type, u16string const&, bool)` (and a vector-of-lines
  overload). `UINotification` = `{ int type; mem::vector<u16string> lines; }` (from `TakeTopNotification`,
  returned by value). Type 0 is the general kind (the exe's 13 call sites all pass 0); the exe polls
  `GetNumNotifications()` every frame at exe+0x1fef30 and shows the lines for ~3 s (+0x1e8 timer, 0xbb8 ms).
  Game.dll itself only raises one from `StaticShrine::RequestToUse`. We can poll the same API -- but the exe
  consumes the queue (`TakeTopNotification` pops), so hook `AddUINotification` instead of polling.
- `ControllerPlayer::SetUserText(std::string const& tag, int ms)` / `GetMailboxUserText()` (the tag, e.g.
  "tagSkillNotReady") / `GetMailboxUserTextTime()` / `PopUserText()`: the HUD (exe+0x2dbd0) reads the main
  player's controller mailbox each frame, localizes the tag and shows it. Hook `SetUserText` (or read the
  mailbox before the exe pops it) to speak "That skill is not ready" etc. Option `errorMessages`.
- Hero/boss kill banners are `tagHeroKilled`/`tagBossKilled` notifications; level-up is
  `GameEngine::HandleExperienceNotification`.

## 4. The developer combat log (`gLogCombat`)
Exported data, never written by the game. Setting it to 1 from the DLL turns on `Engine::Log(prio, channel,
fmt, ...)` calls from: `CombatManager::ApplyDamage`, `TakeAttack`, `DesignerCalculateProbabilityToHit`,
`DesignerCalculateMeleeBlockChance` / `ProjectileBlockChance` / `ShieldBlockDamageReduction`,
`Skill::CollectCombatParameters`, every `Skill*::ModifyDamage`. Format strings live around Game.dll+0x63c228.
Usable as a debugging feed through a hook on `Engine::Log` (exported; varargs, so the hook must `vsnprintf`
itself), not as the player's combat log: it prints attribute math and record paths.

## 5. Options involved (options.txt, Engine.dll `Options` ctor order)
`displayDamage` (numbers on hit), `critMultipliers` (index 3 in `Options::GetBool`: "(xN)" on crits),
`critFeedback` (camera shake), `errorMessages` (the popup line), `monsterBarsUndamaged`, `targetLock`.

## Design consequences for the mod
- "Damage taken" must come from `ApplyDamage` (victim == player, or a pet); the game has no text for it.
- "Damage dealt" can come from either `ApplyDamage` (attacker ids contain the player) or the 0x1b event
  (only what the game draws: the player's and pets' hits on enemies, pre-formatted). The event is simpler
  and matches sighted feedback exactly; `ApplyDamage` adds the damage type and works with `displayDamage` off.
- Miss/Dodge/Block only exist as 0x1b events (or in `TakeAttack`).
- A mod-side combat log (ring buffer of spoken/unspoken lines, reviewable with keys) has to be ours; the
  game keeps only totals.
