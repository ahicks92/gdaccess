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
`SkillActivated` the export can be called directly on it. It returns a `SkillTargetType`.

**The runtime values are NOT the DBR `targetingMode` enum.** The DBR field's authoring order is
`Default;Point;Object;Target`, but the *runtime* `+0x5c0` is a resolved value the class sets, and it was read
straight off the game (2026-08-23, `/hotbar` over known Soldier skills):

- **1 = self / buff** — cast on you (or allies), no aiming. Overguard (`Skill_BuffSelfDuration`), the health /
  energy potions (`Skill_ChargePotion`), Field Command (`Skill_BuffRadiusToggled`).
- **2 = offensive** — the game aims it at your target / where you're facing. Basic Weapon Attack, Cadence,
  Blitz, Forcewave, **and player-centred AoE like War Cry** (`Skill_AttackRadius`).
- **3 = a ground point** — go to / place at a location. Evade, Move To are actually `0` (they came back as
  "target" only through the old miscoded fallback); `3` is inferred for cursor-placed skills, not yet seen on a
  Soldier — treat as unconfirmed.
- **0 = not applicable** — passives, and `SkillActivated` skills that expose nothing. Menhir's Will
  (`Skill_PassiveOnLifeBuffSelf`) is not a `SkillActivated` at all → `None`.

`2` is the one that needs splitting for the readout: a player-centred AoE (War Cry) is a `2` like a targeted
attack, so it is separated by the skill's concrete RTTI class name (each `Skill_*` class has its own
`GetStaticClassInfo`, so `GetRTTIClassInfo` returns e.g. `Skill_AttackRadius`, `Skill_WeaponPool_ChargedFinale`):
class name contains `Radius` → **around you**, else **at a target**.

The player-facing buckets (`world::SkillAim`, spoken by `screens::speak_slot` / `speak_mouse`):

- **self** — value 1 (Overguard, potions, stances).
- **around you** — value 2 with a `Radius` class (War Cry).
- **at a target** — value 2 otherwise (Weapon Attack, Cadence, Blitz, Forcewave).
- **at a spot** — value 3 (cursor-placed; unconfirmed on Soldier).
- (nothing spoken) — value 0 or not a `SkillActivated`.

The game does the actual targeting natively (the number keys pass straight through); the mod only *says* which
bucket a slot is, so the player knows before firing. Verify with `/hotbar` (each slot prints `aim=`).

Verified live (2026-08-23, test character, `/hotbar` with skills assigned to slots): Cadence / Weapon Attack /
Blitz / Forcewave → `target`, War Cry → `around`, Overguard / Field Command / potions → `self`, Menhir's Will
→ `-`. The read handlers themselves were confirmed with `/action read.leftMouse` → "left mouse Weapon Attack,
at a target".

**Input note:** `Ctrl+<digit>` is the read chord, but a digit is also a passthrough key (bare digits activate
the game's slots). `in_game`'s passthrough is by code alone, so `app.cpp`'s game-key filter additionally
swallows `0x02..0x0b` while Ctrl is held — otherwise a real Ctrl+1 would read *and* activate slot 1. (Synthetic
`/key` events bypass that filter entirely and always reach the game, so in-world Ctrl-chords can't be verified
through `/key`; use `/action` for the mod side.)

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

Tested live (2026-08-23): the assignment path (`SetPrimarySlot` / `SetSecondarySlot`, what
`gameapi::set_primary_skill` uses) accepts **any learned skill** on the mouse buttons — a pure self-buff
(Overguard) took the left mouse and reads "left mouse Overguard, self". So there is **no self-buff restriction
at the API level**; the belief that the mouse rejects buffs was a red herring. The only thing that fails to
stick is an **unlearned** skill (level 0), which the game drops regardless of the slot (bar or mouse).

Caveat: this is the direct API, which the game's own drag-and-drop UI may gate more tightly, and I did not
click a mouse-slotted self-buff to confirm it actually casts. But for the mod (which assigns through the API),
every learned skill is mouse-assignable, and the readout reports whatever is there with the right aim.
