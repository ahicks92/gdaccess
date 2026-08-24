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

## Learning requirements, modifiers, and spirit-guide reclamation (RE + live 2026-08-24)

The skills-window click handler (exe+0x248380) branches on a reclaim-mode flag: nonzero -> a click reclaims a
point (`DecrementSkillLevel` + `SkillManager::UseReclamationPoints`), zero -> it learns (`IncrementSkillLevel` +
`SubtractSkillPoint`). The **learn branch only checks points>0 and level<max** -- the requirement gate lives in
the icon-enable logic (the SkillReasons builder exe+0x2492b0), which computes, per skill: byte0 no skill points,
byte1 `Skill::GetMasteryLevel < GetMasteryLevelRequirement` (mastery bar too low), byte2 modifier's base skill
not enabled, plus a mastery-slot rule. Driving `IncrementSkillLevel` directly (as the mod does) bypasses that
gate, which is why learning ignored requirements.

- **`gameapi::can_learn_skill(skill)`** replicates it with exports: points>0, level<max, `GetMasteryLevel >=
  GetMasteryLevelRequirement`, and (for a modifier) its base skill learned. Returns "" or the spoken reason
  ("needs mastery N", "requires <base>", "no points"). `learn_skill` refuses on a non-empty reason. The mastery
  ("class training") skill has req 0 / no base, so it is always learnable (raising the bar); choosing a *new*
  class stays a separate flow (`skills_set_pane`).
- **Modifier -> base link.** `Skill::GetModifiedSkillId` (`*(uint*)(this+0x1e0)`) is a *different*
  (transform/replace) relationship and reads **0** for tree modifiers. The tree link is the base skill's
  **`Skill::GetModifiers()`** (a `mem::vector<uint>` of its modifier ids); `skills()` reverses it into
  `SkillInfo::modified_skill_id`, so a modifier reads "modifies Cadence" and the learn gate can name the base.
- **Reclaim mode = a spirit guide.** The NPC class is `NpcSkillReallocator`; talking to one calls
  `GameEngine::DisplaySkillReallocationWindow` (forwards through `[GameEngine+0x19b0]` vtable+0x60), which opens
  the skills window with the reclaim flag set. That flag is **skills window +0x1f4c** (the handler reads it as
  `[controller+0x1e1c]`, controller = window+0x130; verified live: only that path flips window +0x1f49/+0x1f4c
  0->1 and writes a controller pointer at +0x2634). `exe_ui::skills_reclaim_mode()` reads +0x1f4c.
  - `refund_skill` (Backspace) is only wired by the screen in reclaim mode -- outside a guide it does nothing
    (fixing the old "refund anywhere, silently charging iron bits" bug). Cost is
    `SkillManager::GetCurrentSkillReclamationCost()` (`gameapi::reclaim_cost()`), the same for every skill and
    rising as you reclaim; there is **no clear-all**, it is one point at a time.
  - `gameapi::can_reclaim_skill` gives the refusal reason before trying: **the mastery bar reclaims down to 1
    like any skill** (base `Skill::DecrementSkillLevel`, the same vtable slot as a normal skill -- verified
    5->4->...->1 live), but the game blocks its **last** point (can't drop the class to 0 ->
    `tagDecreaseMasteryError`); reclaiming costs iron bits, so `reclaim_cost() > money()` -> "not enough iron
    bits" (this was the real cause of an earlier mis-read that "masteries can't be reclaimed"). The remaining
    refusal is a base skill's final point with modifiers still on it -> the game's `tagReclaimBase`.
  - Dev: `/reclaim` opens the skills window in reclaim mode without a guide (`gameapi::dev_open_skill_reclaim`);
    `/cheat?bits=N` (`Character::AddMoney`) tops up iron bits to test affordable reclaims.

Attribute points (Physique/Cunning/Spirit, the character sheet) are **never refundable** in Grim Dawn -- the
stats-tab rows wire no reclaim path and `ResetAttributePointsConfigCmd` is never called. The stats tab also now
carries the game's own tooltips (Space): `tagCharAttributeDescription0X`, `tagCharStats{OA,DA,DPS}Description`,
`tagStatsResistance0XDesc`.
