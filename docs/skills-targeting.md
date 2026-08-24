# Skills, the quickbar, and how the game targets (RE 2026-08-23)

How a blind player is told what a skill will do, and the hot-slot / mouse-slot machinery behind the quickbar
keys. Player-facing keys are in `docs/controls.md`; the model layer is `src/gameapi*.cpp`; the aim resolver is
`world::skill_aim` in `src/world.cpp`.

## How the game decides player-position vs mouse

Every player skill instance is a `GAME::SkillActivated` subclass. It carries a target-type field at `+0x5c0`,
returned by the virtual `SkillActivated::GetTargetType()` (Game.dll export; the game itself calls it through
`SkillManager::GetTargetType`, dispatching the skill object's vtable). The body is trivial:

    SkillActivated::GetTargetType()  ->  return *(int*)(this + 0x5c0);

and it is **never overridden** (only `SkillActivated` defines it), so once an object is confirmed to be a
`SkillActivated` the export can be called directly on it. The value is the `SkillTargetType` enum, loaded from
the DBR `targetingMode` field, whose template default order fixes the numbering:

- **0 Default** — no explicit aim; the game resolves it from the skill's kind.
- **1 Point** — a ground spot: the mouse cursor's world position.
- **2 Object** — a world object (rare).
- **3 Target** — a specific entity under the cursor.

`Default` is overloaded: a self-buff and a basic weapon attack both leave `targetingMode` unset. So Default is
disambiguated by the skill's concrete RTTI class name (each concrete `Skill_*` class has its own
`GetStaticClassInfo`, so `GetRTTIClassInfo` returns e.g. `Skill_AttackRadius`, `Skill_WeaponPool_ChargedFinale`,
`Skill_BuffSelfToggled`):

- name contains `Radius` (`Skill_AttackRadius`, `Skill_BuffRadius*`, `Skill_OnHitAttackRadius`) → **around you**
  (centred on the player: War Cry, etc.).
- name contains `BuffSelf` / `Passive` / `Toggled` / `Shapeshift` → **self** (cast on you, no aim).
- otherwise (`SkillActivatedWeapon`, `WeaponPool`, `Attack*`, `Spell`, projectiles) → **at your current
  target** (the enemy under the cursor / auto-face).

Passives and modifiers are `Skill` but **not** `SkillActivated` (no `+0x5c0`), so `skill_aim` returns `None`
for them — they are never on the bar anyway.

The four player-facing buckets (`world::SkillAim`, spoken by `screens::speak_slot` / `speak_mouse`):

- **self** — Overguard, Field Command, the stances, Cadence's secondary buff.
- **around you** — War Cry (`Skill_AttackRadius`, instant, `skillTargetRadius`).
- **at a spot** — targetingMode `Point`: Shattering Smash (`Skill_AttackWave`), most cursor-placed AoE/spells.
- **at a target** — Cadence, Blade Arc, Blitz, and any targetingMode `Target`.

The game does the actual targeting natively (the number keys pass straight through); the mod only *says* which
bucket a slot is, so the player knows before firing. Verify with `/hotbar` (each slot prints `aim=`).

## Hot slots (the quickbar model)

- `Player::GetPlayerHotSlotCtrl` → `HotSlotCtrl`; `GetHotSlotOption(index)` → a `HotSlotOption`. Subclasses
  (`HotSlotOption::GetType`): Skill, Potion, PotionSkill, Scroll, Evade. Only `HotSlotOptionSkill` carries a
  skill id (`GetSkillId`); potions/scrolls/evade have none, so their aim is `-`.
- 47 slots per weapon config. The four displayed bars start at index **0 / 14 / 26 / 36** (10 slots each; the
  gap holds the config's own mouse/extra entries). `quickbar_slot_index(bar, k)` maps them; the HUD's current
  bar is `InGameUI+0x72f0` (`exe_ui::quickbar_page()`, 0..3), cycled by the game's own **Y = Quickbar Switch**.
- The two **mouse** slots are separate: `HotSlotCtrl::GetPrimarySlot` (left) / `GetSecondarySlot` (right).
- `HotSlotOptionSkill::kAlternateEquipmentFlag/Mask` — a slotted skill can be tied to a weapon set.
- Assigning a skill: build a `HotSlotOptionSkill` with its ctor in our memory, `SetPlayer`, then `SetHotSlot`
  (bar) / `SetPrimarySlot` / `SetSecondarySlot` (mouse); the game deep-copies our option. `SetPrimarySkillId`
  did **not** move the slot when tried (2026-08-22) — use the slot setters.

## When a skill can / cannot be on the mouse

Believed (needs one live confirmation — try dropping a self-buff on a mouse button): the mouse buttons accept
only skills that need aiming — attacks and `Point` / `Target` skills — and reject pure self-buffs, toggles and
passives, gated by `HotSlotOption::Validate` / `ValidationResult` on the same target-type. i.e. a slot whose
`skill_aim` is `self` or `around you` is a keyboard-bar skill, not a mouse skill; `at a spot` / `at a target`
are mouse-eligible. To be checked against the game's own drop behaviour before relying on it.
