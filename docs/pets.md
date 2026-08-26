# Pets: how the game does it, what the mod has, what parity needs (2026-08-26)

Static RE: `re_pets_gamedll.md` (Game.dll surface) and `re_pets_exe.md` (the exe's pet bar, selection and
F-keys). Live verification below was done on the test character `claude` (levelled to 24 by `/cheat?xp=`,
Occultist 10, Summon Familiar + Summon Hellhound learned through `/skills?learn=`; cast from the hotbar).

## The sighted player's model

- Pets are summoned by skills (Occultist raven/hellhound, Shaman briarthorn/primal spirit, Necromancer's
  skeletons, item/devotion procs). They follow and fight on their own; the summoning skill's own tooltip says
  how many exist at once. **There is no cap on the number**: the HUD shows one portrait per entry of
  `GameEngine::GetLocalPetList()`; Raise Skeletons alone makes 3+.
- Pet portraits top-left (image + health bar, NO text the mod's hooks capture). Hover = the pet's summary.
- **Stance** per pet via the portrait's right-click menu: Set to Aggressive / Normal / Defensive
  (`tagPortraitAggressive/Normal/Defensive`). The default is Aggressive. **Stance is stored per SUMMONING
  SKILL** (`Player+0x4ca0` map keyed by skill id), so every pet of one skill shares it and a resummon keeps it.
- **Disband Pet** from the same menu (`ControllerPlayer::ReleasePet(id, false)`).
- **Commands**: (1) the "Pet Attack" default skill (`records/skills/default/defaultpetattack.dbr`, always in
  the skill list, `enabled` only while a pet exists; goes on a quickbar slot like any skill) -- pressed with the
  cursor over an enemy every controllable pet attacks it, over ground they move there. (2) A one-shot
  **selection**: F2-F6 toggle pet 1-5 (portrait order = pet-list order), F7 selects all, a left-click on a
  portrait toggles it, Ctrl(Target Pet)+click on a pet in the world toggles it. **While the selection is
  non-empty the player's next world click is eaten**: it commands the selected pets (attack the target /
  move to the point) and clears the selection. Each toggle plays the pet's acknowledge sound and puts a
  banner over it.
- The tutorial tip (`tagTutorialTip35`) is the only in-game explanation; nothing announces a pet dying.

## What the game exposes (all verified live unless marked)

- `gameapi::pet_ids()` / `pets()` -> `GameEngine::GetLocalPetList` (gGameEngine+0x36138, `mem::vector<u32>`),
  label via the usual `GetGameDescription` path ("Great Raven", "Hellhound"), life via
  `Character::GetCurrentLife/GetLifeLimit`, summoning skill via `Character::GetPetPen` +
  `PetPen::GetPetOwner(petId)` (returns the SKILL id), stance via `Player::GetPetControllerType(skillId)`.
  Spawn/death = list membership (`RegisterLocalPet` from `Monster::JoinMe`, `UnregisterLocalPet` from
  `Monster::CharacterIsDying`); polling the list per frame is enough.
- `set_pet_stance(pet, 0|1|2)` = the exe's menu sequence: `Player::SetPetControllerType(skill, t)` then
  `Monster::UseController(pet, t)` on every live pet of that skill. Verified: Hellhound 1 -> 2 -> 0 read back.
- `pet_attack(pet, target)` = `Character::RequestAttack(pet, playerId, targetId)`. Verified: the hound left
  one dummy and attacked the other, hits in the combat feed; the raven (ranged) engaged from where it stood.
- `pet_move(pet, worldPoint)` = `Character::RequestMove(pet, playerId, WorldVec3)`. Live behaviour: an
  ATTACKING pet stopped and came back to the player's side; an idle pet ignored a point 20 units away. So it is
  a **recall**, not a "go there" (or the point is being rejected -- unresolved; the exe issues moves through
  `RequestAllyMoveConfigCmd` + `Actor::Enqueue`, not read live). Ground moves are covered by Pet Attack anyway.
- `release_pet(pet)` = Disband. Verified: the raven vanished, list size 2 -> 1 the same frame.
- The game's selection list is exe-side (`X+0x858`, size at `X+0x868`, X = `[main_obj+0x90]`); F7 -> size 2,
  F2 toggles 2 -> 1 -> 2 (verified through `/key` + `/peek`). The selection-consumes-the-click branch was NOT
  reproduced through the dev loop (`/jkey`, held Enter and a `gd.py click` at the projection all left the
  selection at 2 and commanded nothing) -- unknown whether that is a dev-loop artifact; it needs the user's
  hands. The mod does not depend on it.
- "Pet Attack" through the real key path works with the review lock: lock the dummy, press the slot key ->
  the hound ran 12 units and attacked. `gameapi::activate_hotslot` alone does NOT (the exe's key path resolves
  the cursor target). `world::skill_aim` reports Pet Attack as None (it is not a `SkillActivated`), so the
  hotbar manager currently filters it out of the assignable list.
- Dev: `/pets` lists; `?stance=<pet>&type=0|1|2`, `?attack=<pet>&target=<id>`, `?move=<pet>&x&y&z`,
  `?release=<pet>`.

## Built (2026-08-26, verified through the loop; docs/controls.md has the player table)

`src/screens/pets.{h,cpp}` + `world::ScanGroup::Pets`: `]`/`[` cycle pets (Alt+] nearest; stance as the note),
Backspace = the pet overlay (rows: stance Left/Right, Enter select, Backspace disband, Space where; command rows
attack-locked-target / recall for the selection or all), F2..F6/F7 = our own selection toggles (the game's are
swallowed), Shift+Backspace = attack from the world, "<pet> summoned" / "<pet> down" in Zira from list polling
(seeded silently on entering the world; a disband from the overlay is forgotten first so it does not also say
"down"). The hotbar manager lists Pet Attack (`skill_aim` says AtTarget for `Skill_PetAttack`). Not built: a
health readout (no way to heal pets), any pet sound in the sonar.

## What parity needed (the plan that was built)

1. **Perceive**: a pets review group (a fifth scanner key, e.g. `,` is taken -- pick one) cycling
   `pets()` nearest-first: "Hellhound, 9 away, 12 o'clock, 80 percent, aggressive, 1 of 2"; landing locks the
   review cursor on the pet like any other group (so J/Ctrl+click semantics and `/inspect` work). Plus a pets
   summary key (all pets, one line each) for the "how are they doing" question.
2. **Health**: per-pet `threshold_watcher` (10 % steps, like the player's) spoken in Zira with the pet's
   name; "Hellhound died" / "Hellhound summoned" from list membership. Optional low-health warning only
   (pets are many; keep it to crossing 50/25 % if it gets noisy).
3. **Stance / disband**: a per-pet action set. Either a mod-owned "pets" overlay (layer 1 like the hotbar
   manager: one row per pet with stance as a Left/Right adjustable value and Disband on Backspace, plus
   Attack-locked-target / Recall) or keys on the reviewed pet. The overlay is the honest mirror of the
   portrait menu and scales past 5 pets; keys alone do not.
4. **Commands**: keep the game's Pet Attack skill as the "all pets" command (it already works with the lock;
   make the hotbar manager list it -- special-case the default-attack ids or treat `Skill_PetAttack` as
   AtTarget in `skill_aim`). Add "this pet attacks the locked target" = `pet_attack` from the overlay/review
   landing, and "come back" = `pet_move` to the player's position (recall). Ground "move here" for all pets
   stays on Pet Attack aimed at the follow target / an exit (the cursor lock on a point).
5. **The game's own selection keys** (F2-F7 pass through today): either announce them ("Hellhound selected",
   "all pets selected", "selection cleared") by polling `X+0x868` + the list, and warn that the next click is
   theirs -- or swallow F2-F7 and route them to the mod's own selection model. Recommendation: swallow.
   The exe path has a hidden mode (the eaten click) and a 5-pet ceiling; the mod's overlay covers it fully
   with exports only (no exe RVAs needed for pets at all).
6. **Sonar**: pets should NOT be enemy cues; a soft own-pet cue is optional (they are usually at the player's
   side). Nothing needed for the first pass.

Open: `RequestMove` to a point (see above); whether the game's selection+click works for a real click on the
user's keyboard; whether `SkillManager::GetDefaultSkillId(1)` is Pet Attack (unverified, `default_skill_id`).
