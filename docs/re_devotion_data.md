# Devotion at the game-data level (offline RE 2026-08-27, `database.arz` + `Text_EN.arc` + `templates.arc`)

Everything here was read offline with `uv run tools/arz.py`, `uv run tools/arc_unpack.py` and a
throw-away `templates.arc` reader. **Nothing was verified in a running game.** Where the data does not
answer a question (the runtime `AffinityType` enum order, the devotion-point cap, the
celestial-power-to-skill assignment) that is said explicitly.

**Scope caveat:** this install is the **base game only** -- there is no `gdx1/`, `gdx2/` or `gdx3/`
directory under the Grim Dawn install, so no expansion `.arz` is layered on top. Every count below
(87 constellation records, 50 celestial powers, 30 shrines) is the base-game database. With
Ashes of Malmouth / Forgotten Gods installed the expansions add records through their own
`gdx*/database/*.arz`, which `tools/arz.py` does not read today.

---

## 1. `records/ui/skills/devotion/devotion_mastertable.dbr`

Template `database/templates/ingameui/devotionpane.tpl`. It is the devotion window's whole layout
record. Fields, grouped:

### The constellation list
- `devotionConstellation1` .. `devotionConstellation87`, each
  `records/ui/skills/devotion/constellations/constellationNN.dbr`. **87 records exist and all 87 are
  referenced.** One of them (`constellation87.dbr`, FileDescription `Crossroads - Bitmap`) has NO
  `devotionButtonN` at all -- it is the artwork for the Crossroads cluster. So **86 pickable
  constellations**, of which 5 are the one-star Crossroads.

### Affinity panel (the sidebar)
- `affinity01Bitmap` .. `affinity05Bitmap` = `records/ui/skills/devotion/affinity_0Nbitmap.dbr`
- `affinity01Number` .. `affinity05Number` = `records/ui/skills/devotion/affinity_0Nnumber.dbr`
- `affinity01Rollover` .. `affinity05Rollover` = `records/ui/skills/devotion/affinity_0Nrollover.dbr`
- `affinityTitle = records/ui/skills/devotion/devotion_affinitytitle.dbr`
- Per-affinity tint colours, named by affinity, NOT numbered:
  `ascendantRed/Green/Blue/Alpha = 0.8 / 0.5 / 1.0 / 1.0`,
  `chaosRed/Green/Blue/Alpha = 1.0 / 0.35 / 0.5 / 1.0`,
  `eldritchRed/Green/Blue/Alpha = 0.65 / 1.0 / 0.2 / 1.0`,
  `orderRed/Green/Blue/Alpha = 1.0 / 0.7 / 0.4 / 1.0`,
  `primordialRed/Green/Blue/Alpha = 0.6 / 0.6 / 0.92 / 1.0`.

### Search box (the devotion window has a text filter)
- `searchBox = records/ui/skills/devotion/devotion_searchbox.dbr`
- `searchBoxBackground = records/ui/skills/devotion/devotion_searchbox_background.dbr`
- `clearSearchButton = records/ui/skills/devotion/devotion_buttonclearsearch.dbr`
- Engine side: `GAME::GameEngine::GenerateUIDevotionSearchText(Skill const*, mem::vector<GameTextLine>&, GameTextClass)`
  (exported) is what the filter matches against.

### Point counters and costs
- `pointsRemaining = records/ui/skills/devotion/devotion_styleskillpointsremaining.dbr`
- `pointsTotal = records/ui/skills/devotion/devotion_skillpointstotal.dbr`
- `pointsRemainingColorPulse`, `goldCostBoxImage`, `goldCostNumber`, `goldCostText`,
  `totalGoldBoxImage/Number/Text`, `totalAetherBoxImage/Number/Text` -- i.e. the reclaim price is shown
  in **iron bits AND aether crystals**, matching `devotionReclamationAetherCost` in section 5.

### Star / link / constellation rendering
- `connectionActiveTexture` / `connectionInactiveTexture` / `connectionLockedTexture`,
  `connectionWidth = 7.0` -- the drawn edges between stars (three states: taken, available, locked).
- `activeBorderBitmap`, `availableBorderBitmap`, `skillBorderBitmap`, `disabledBitmap`,
  `emptySkillBitmap = ui/skills/devotion/devotionicon_empty.tex`, `effectBgBitmap`, `startBgBitmap`
- `starColorPulse`, `starColorPulseBitmap`, `starPulseDelayMin = 2000`, `starPulseDelayMax = 7000`
- `constellationColorPulse`, `constellationFlashPulse`, `constellationRollover`,
  `constellationCompleteSound = records/sounds/skillsounds/devotion/spak_devotion_constellationcomplete.dbr`
- `devotionButtonSound = records/sounds/skillsounds/devotion/spak_devotion_selection.dbr`
- `bgTile = ui/skills/devotion/devotion_section1.tex`, `nebulaSections` (5 dbrs), `edgeFade`
- Canvas size: `sizeX = 6144`, `sizeY = 5120`, `positionX = 0`, `positionY = 0`,
  `windowScreenAlignmentX/Y = Center`

### Chrome
- `closeButton`, `helpButton`, `undoButton` (= `devotionbuttons_undopointallocation.dbr`),
  `infoBox`, `infoTabOpen`, `infoTabClose`, `sideBar`, `skillSelect`,
  `skillsTab1Button` / `skillsTab2Button` / `skillsTab3Button`, `levelTextString`,
  `zoomInButton`, `zoomOutButton`, `zoomText`, `buttonColorPulse`.

`records/ui/hud/hud_mastertable.dbr` and `records/ui/hud/hud_orbmastertable.dbr` both have
`hudDevotionWindow = records/ui/skills/devotion/devotion_mastertable.dbr`.

### Which affinity number is which

The rollover records give the answer directly:
- `affinity_01rollover.dbr`: `Line1Tag = tagDevotionAffinity01`, `Line2Tag = tagDevotionAffinity01Info`
- ... same shape for 02..05.

And from `Text_EN.arc` (`tags_skills.txt`):
- `tagDevotionAffinity01 = Ascendant`
- `tagDevotionAffinity02 = Chaos`
- `tagDevotionAffinity03 = Eldritch`
- `tagDevotionAffinity04 = Order`
- `tagDevotionAffinity05 = Primordial`

So the **UI slot order is 1 Ascendant, 2 Chaos, 3 Eldritch, 4 Order, 5 Primordial** (alphabetical).
`records/game/gameengine.dbr` carries the matching icon list `affinity1Icon .. affinity5Icon =
ui/skills/devotion/devotion_affinity01.tex .. 05.tex`, and the engine reads it through the exported
`GAME::GameEngine::GetAffinityBitmap(enum GAME::AffinityType) const` -- which is strong (not proof)
evidence that the runtime `AffinityType` enum has that same order, 1-based or 0-based.

**Counter-evidence, do not skip it:** the DBR template `ingameui/devotionconstellation.tpl` declares
`affinityGivenName1` / `affinityRequiredName1` etc. as a combo box whose
`defaultValue = "Primordial;Chaos;Order;Eldritch;Ascendant;"` -- a *different* order. That string is
only the editor's drop-down; the stored value in every constellation record is the **plain string**
(`"Eldritch"`, `"Ascendant"`, ...), never an index, so the data itself never commits to an enum
ordering. **Verify `AffinityType` at runtime** before indexing anything: read
`GAME::Character::GetAffinity(AffinityType)` for a live character with a known single Crossroads
picked, or read the five affinity numbers off the window.

---

## 2. Constellation records

`records/ui/skills/devotion/constellations/constellationNN.dbr`, template
`database/templates/ingameui/devotionconstellation.tpl`. All 87 records carry the same field
set (missing fields = value 0 / absent):

- `FileDescription` -- the **internal** name. It is often stale: `constellation09` says "Eye of Dreeg"
  but the tag says "Eye of the Guardian"; `constellation46` "Samael's Witchblade" -> "Solael's
  Witchblade"; `constellation50` "Caller of The Frost" -> "Amatok the Spirit of Winter";
  `constellation51` "Pestilence" -> "Affliction"; `constellation84` "Candle" -> "Scholar's Light";
  `constellation29` "Turtle" -> "Tortoise". **Always use the tag, never FileDescription.**
- `constellationDisplayTag` -- the name tag, e.g. `tagDevotion_A01`. Tag families: `tagDevotion_A01..A38`
  (tier 1), `tagDevotion_B01..B31` (tier 2), `tagDevotion_C01..C13` (tier 3). All five Crossroads
  records share `tagDevotion_A15`.
- `constellationInfoTag` -- the flavour paragraph, e.g. `tagDevotion_A01Desc`.
- `constellationBackground` -- `constellationNN_background.dbr` (a `bitmapsingle.tpl` with
  `bitmapName` + `bitmapPositionX/Y`). Present on 82 of 87.
- `affinityGivenName1..3` (string) + `affinityGiven1..3` (int) -- the **completion bonus**: the affinity
  granted when every star of the constellation is taken. Slot 3 exists in the template but is 0 in all
  87 records; **at most two affinities are ever given**. There is no other "completion bonus" field --
  affinity IS the completion bonus.
- `affinityRequiredName1..3` (string) + `affinityRequired1..3` (int) -- the affinity gate on the whole
  constellation. Up to three are used (6 records use slot 3, e.g. `constellation57` Hydra requires
  Eldritch 5, Ascendant 3, Chaos 3).
- `devotionButton1..devotionButton10` (the template allows 10; **max used is 8**) -- each points at a
  `records/ui/skills/devotion/tierN_XXy.dbr` **UI button** record, not the skill.
- `devotionLinks1..devotionLinks10` -- declared `class = "array", type = "int"` in the template, but
  **every one of the 352 present values in the shipped data is a single int**, and `devotionLinks1` is
  never present. So in practice: `devotionLinksK = J` means *star K's parent is star J*, star 1 is the
  root, and the shape is a **tree** (not a general graph). Example, `constellation02.dbr` Akeron's
  Scorpion: `devotionLinks2=1, devotionLinks3=2, devotionLinks4=3, devotionLinks5=3` -- star 5 branches
  off star 3, not star 4. A parser should still read it as an array in case an expansion uses more.
- Render colours per state: `constellationActiveRed/Green/Blue/Alpha` (0.9 / 0.8 / 1.0 / 0.8),
  `constellationAvailableRed/Green/Blue/Alpha` (1 / 1 / 1 / 0.45),
  `constellationUnavailableRed/Green/Blue/Alpha` (0.7 / 0.9 / 1.0 / 0.15) -- identical in all 87 records.

**There are no positions in the constellation record.** Positions live on the UI button records
(section 3) and on `constellationBackground`.

### Counts (all 87 records)
- Stars per constellation: 0 stars x1 (the Crossroads bitmap), 1 star x5 (the Crossroads), 3 stars x8,
  4 stars x11, 5 stars x26, 6 stars x18, 7 stars x17, 8 stars x1 (`constellation67`, Abomination).
  **Total 438 stars**, which is exactly the entry count of `_devotiontree.dbr` (section 3).
- Tier by star-record prefix: `tier1_*` = constellations 1-35 and 80-86 (42 records, includes the
  Crossroads); `tier2_*` = 36-65 and 79 (31); `tier3_*` = 66-78 (13).

The full per-constellation listing is in the appendix at the end of this file.

---

## 3. Star records and celestial powers

A star is **two records with the same basename in two folders**:

- the **UI button**: `records/ui/skills/devotion/tier1_01a.dbr`, template
  `database/templates/ingameui/skillbutton.tpl`. Fields: `skillName` (the pointer to the skill record),
  `bitmapPositionX` / `bitmapPositionY` (the star's position on the 6144x5120 devotion canvas; observed
  range X -2336..1855, Y -2010..2156), `isCircular = True`, `bitmapNameUp/Down/InFocus/Disabled`,
  `soundNameDown`, and on celestial-power stars also `skillOffsetX` / `skillOffsetY`.
- the **skill**: `records/skills/devotion/tier1_01a.dbr` (a plain star) or
  `records/skills/devotion/tier1_01e_skill.dbr` (a celestial power -- note the `_skill` suffix).

### The master list: `records/skills/devotion/_devotiontree.dbr`
Template `database/templates/devotionskilltree.tpl`. This is the devotion equivalent of a mastery's
skill tree, and it is what the PC record points at:
`records/creatures/pc/malepc01.dbr` and `records/creatures/pc/femalepc01.dbr` both have
`devotionTree = records/skills/devotion/_devotiontree.dbr`.

It holds **438 triples**: `skillName1..438`, `skillType1..438`, `skillLevel1..438`.
- `skillLevelN` is **0 for all 438** entries.
- `skillTypeN` is `"Passive"` for **388** entries and `"Effect"` for **50**.
  **`skillType = "Effect"` is the data's definition of a celestial power.** Everything else is a plain
  stat star.

### A plain star (passive)
`records/skills/devotion/tier1_01a.dbr`, template `database/templates/skill_passive.tpl`,
`Class = Skill_Passive`. Non-default fields, in full:

```
Class = Skill_Passive
FileDescription = Bat
characterBaseAttackSpeedTag = CharacterAttackSpeedAverage
offensiveLifeModifier = 15.0
offensiveSlowBleedingModifier = 15.0
skillDisplayName = tagDevotion_A01          (the CONSTELLATION's tag, not a per-star name)
skillDownBitmapName = ui/skills/icons/skillicon_devotionstar01_down.tex
skillUpBitmapName = ui/skills/icons/skillicon_devotionstar01_up.tex
skillMaxLevel = 1
```

Points to note:
- `skillMaxLevel = 1` on **440 of the 441** devotion skill records that declare it -- **a star costs
  exactly one devotion point and has one level**. (The one exception, `tier2_15g_skill.dbr` = Blizzard,
  has `skillMaxLevel = 60`, which looks like a data slip; its `skillExperienceLevels` array is still 15
  long. The other `skillMaxLevel = 60` in the folder is a pet innate,
  `records/skills/devotion/pets/petskill_devotionpetinnate_tier2.dbr`.)
- Every stat field on a passive star is a **scalar**, not a per-level array -- a passive star has no levels.
- `skillDisplayName` on plain stars is the *constellation* tag, so plain stars have no individual name.
  This matters for a screen reader: a star's identity is "constellation, star N, its bonuses", and its
  bonuses have to come from the game's own tooltip builder
  (`GAME::GameEngine::GenerateUIDevotionText`, exported), not from parsing DBR stat fields.
- **No affinity fields on stars.** All affinity data is on the constellation record.
- **No position fields on the skill record** -- position is on the UI button.
- There is **no `devotionMaxLevel` field**. The engine's `GAME::Skill::GetDevotionMaxLevel()` is
  backed by `skillExperienceLevels`' length (below), not by a named DBR field.

### A celestial power
`records/skills/devotion/tier1_01e_skill.dbr` (Bat -> Twin Fangs), template
`database/templates/skill_attackprojectileburst.tpl`, `Class = Skill_AttackProjectileBurst`. The
distinguishing fields:

```
skillDisplayName      = tagDevotionEffectA01        -> "Twin Fangs"
skillBaseDescription  = tagDevotionEffectA01Desc
skillCooldownTime     = 0.6
skillMaxLevel         = 1
skillUltimateLevel    = 1
templateAutoCast      = records/controllers/itemskills/cast_@enemyonattack_20%.dbr
skillExperienceLevels = [0, 30000, 63816, 104308, 155181, 221016, 307300, 420448, 567822, 757751,
                         999546, 1303516, 1680979, 2144278, 2706792, 3382947, 4188225, 5139176,
                         6253427, 7549690]
offensiveLifeMin      = [28, 32, 36, ... 108]       (20 entries -- one per power level)
offensiveLifeMax      = [46, 53, 60, ... 186]
offensivePierceMin    = [40, 45, ... 140]
offensiveLifeLeechMin = [20, 21, ... 40]
weaponDamagePct       = [10, 10, 11, ... 20]
projectileLaunchNumber = 2, projectileLaunchRotation = 10.0, projectilePiercingChance = 100.0
distanceProfile = Maximum, targetingMode = Target
```

- **`skillExperienceLevels` is the per-level experience requirement table**, and its LENGTH is the
  power's level cap. Observed lengths: **20 for tier-1 powers (14 records), 15 for tier-2 (22 records),
  10 for tier-3 (16 records)** -- 52 records in total (see the buff-carrier note below). The first
  entry is always 0 (level 1). Examples: tier 2 `records/skills/devotion/tier2_08g_skill.dbr` (Trample) =
  `[0, 75000, 164154, 276228, 422138, 615122, 870884, 1207721, 1646637, 2211449, 2928881, 3828651,
  4943558, 6309558, 7965836]`; tier 3 `records/skills/devotion/tier3_13g_skill.dbr` (Living Shadow) =
  `[0, 160000, 371336, 672776, 1116836, 1771443, 2721398, 4069700, 5938761, 8471532]`.
  Engine side this is `GAME::Skill::GetDevotionExperience()`, `GetDevotionLevel()`,
  `GetDevotionMaxLevel()` (exported), and the level-up feedback is
  `devotionSkillLevelUpFx` / `devotionSkillLevelUpSound` on the PC record.
- **`templateAutoCast` is the proc definition.** It points at a
  `database/templates/skillautocastcontroller.tpl` record under `records/controllers/itemskills/`
  whose fields are the whole trigger:

```
cast_@enemyonattack_20%.dbr     : triggerType = AttackEnemy,     targetType = Enemy, chanceToRun = 20,  autoTargetRadius = 22.0
cast_@selfonattackcrit_100%.dbr : triggerType = AttackEnemyCrit, targetType = Self,  chanceToRun = 100, autoTargetRadius = 22.0
cast_@selfat50%health_100%.dbr  : triggerType = LowHealth,       targetType = Self,  chanceToRun = 100, triggerParam = 50.0
```

  Trigger kinds seen across the 26 distinct controllers devotion uses: attack (`AttackEnemy`),
  attack-crit (`AttackEnemyCrit`), any hit, melee hit, block, low health; `targetType` is `Enemy` or
  `Self`; `chanceToRun` 15/20/25/30/33/35/50/100. The controller filename encodes the same:
  `cast_@<target><trigger>_<chance>%`. The 26 in use, with the count of devotion skills each serves:
  `cast_@enemyonattack_15%` (7), `cast_@enemyonattack_20%` (4), `cast_@selfonattack_25%` (4),
  `cast_@selfonattackcrit_100%` (4), `cast_@enemyonattack_25%` (3), `cast_@selfonattack_20%` (3),
  `cast_@selfonanyhit_30%` (3), `cast_@enemyonattackcrit_100%` (2), `cast_@selfonattack_33%` (2),
  `cast_@enemyonanyhit_33%` (2), `cast_@enemyonattack_100%` (2), `cast_@enemyonattack_30%` (2), and
  one each of `cast_@selfonblock_50%`, `cast_@enemyonattack_35%`, `cast_@selfat45%health_100%`,
  `cast_@selfat50%health_100%`, `cast_@enemyonblock_50%`, `cast_@selfonmeleehit_20%`,
  `cast_@selfonanyhit_15%`, `cast_@selfonanyhit_20%`, `cast_@selfonattack_100%`,
  `cast_@selfat40%health_100%`, `cast_@selfonattack_15%`, `cast_@selfonblock_33%`,
  `cast_@enemyonanyhit_50%`, `cast_@selfonanyhit_25%`.
- **Buff-carrier pattern (8 of the 50 powers).** For these, the record the tree names is a thin shell
  and the name / description / experience table / autocast live in a `_skill_buff` companion reached by
  `buffSkillName`:

```
records/skills/devotion/tier1_08e_skill.dbr : Class = Skill_AttackBuff
    buffSkillName = records/skills/devotion/tier1_08e_skill_buff.dbr   (the ONLY other field)
records/skills/devotion/tier1_08e_skill_buff.dbr : Class = SkillBuff_Debuf, debufSkill = True
    skillDisplayName = tagDevotionEffectA08  ("Assassin's Mark")
    skillExperienceLevels = [0, 30000, ... 7549690]   (20 entries)
    templateAutoCast = records/controllers/itemskills/cast_@enemyonattackcrit_100%.dbr
    defensivePhysical = [-4 .. -27], defensivePierce = [-8 .. -31],
    skillActiveDuration = [3, 3, 4, ... 15]
```

  The 8: `records/skills/devotion/tier1_08e_skill.dbr`, `tier2_03f_skill.dbr`, `tier2_04g_skill.dbr`,
  `tier2_05f_skill.dbr`, `tier2_11e_skill.dbr`, `tier3_05g_skill.dbr`, `tier3_08g_skill.dbr`,
  `tier3_09f_skill.dbr`. **Any reader must follow `buffSkillName` when `skillDisplayName` is absent**,
  or 8 of the 50 powers come out nameless.
- Power classes seen (the `Class` of the effect record or its buff): `Skill_AttackProjectileBurst`,
  `Skill_AttackProjectile`, `Skill_AttackProjectileRing`, `Skill_AttackProjectileDrop`,
  `Skill_AttackProjectileAreaEffect`, `Skill_AttackProjectileOrbiting`, `Skill_AttackRadius`,
  `Skill_AttackWave`, `Skill_AttackSpell`, `Skill_AttackWeapon`, `Skill_AttackWeaponBlink`,
  `Skill_AttackWeaponCharge`, `Skill_AttackBuff`, `Skill_AttackBuffRadius`, `Skill_BuffSelfDuration`,
  `Skill_BuffSelfShield`, `Skill_BuffRadius`, `Skill_BuffRadiusToggled`,
  `Skill_BuffAttackRadiusLightning`, `Skill_BuffAttackRadiusDrop`, `Skill_SpawnPet`,
  `Skill_TargetedSpawnPet`, `Skill_DispelMagic`, `Skill_RefreshCooldown`, `SkillBuff_Debuf`,
  `SkillBuff_Contageous`, `SkillBuff_Passive`.
- Weapon requirement lines exist as tags (`tagDevotion_RequiresSword`, `..._RequiresShield`,
  `..._Requires2h`, etc. -- full list in section 6), used by powers whose record sets the weapon
  booleans (`Sword`, `Sword2h`, `Axe`, `Axe2h`, `Mace`, `Mace2h`, `Shield`, `Spear`, `Staff`,
  `Ranged1h`, `Ranged2h`, `Offhand`, `Magical`) that also appear on plain passives as `False`.

### Where the power sits in the constellation
- 48 constellations have exactly one celestial power, 1 has two (`constellation67`, Abomination:
  star 5 = Abominable Might, star 8 = Tainted Eruption), 38 have none. **50 powers total**, matching
  the 50 `Effect` entries in the tree.
- The power is the **last** star in 45 of the 49 constellations that have one. The exceptions, all of
  which put a power at **star 3**: `constellation36` Rhowan's Crown (5 stars),
  `constellation51` Affliction (7 stars), `constellation52` Crab (5 stars), plus `constellation67`
  Abomination's first power at star 5 of 8. **Do not assume "last star = power"; read `skillType`.**

### "Assigned to skill" -- NOT in the data
A celestial power is bound to one of the character's own skills at runtime (`tagQuickTip61`:
"Assign your Celestial Power to a skill by pressing the icon in the Devotion Window and selecting a
skill from the list"). **There is no DBR field for it.** A whole-database scan for any field whose
name contains "assign" or "celestial" returns **zero fields**. It is character save state; the engine
surface is `GAME::Skill` / `GAME::SkillManager` (see section 7), and the tooltip
`tagReclaimDevotion = Remove Attached Devotion Effect` is the UI action that clears it.

---

## 4. Dependency semantics from the data

What the data actually says, per star and per constellation:

1. **A constellation is gated by affinity, not by other constellations.**
   `affinityRequiredName1..3` + `affinityRequired1..3` on the constellation record is the only gate.
   Tier-1 constellations require **1** of a single affinity (e.g. `constellation02` Akeron's Scorpion:
   `affinityRequiredName1 = Eldritch`, `affinityRequired1 = 1`). Tier-2 require 4..10 across one to
   three affinities. Tier-3 require the big numbers, e.g. `constellation69` Oleron
   `Ascendant 20, Order 7`; `constellation71` Spear of the Heavens `Primordial 20, Chaos 7`;
   `constellation66` Aeon's Hourglass `Chaos 8, Primordial 18`.
2. **Within a constellation the order is the `devotionLinks` tree**: star 1 first, star K only after
   its parent `devotionLinksK`. Nothing in the star record repeats the requirement -- it is purely the
   constellation's link table. (CORRECTION after the Game.dll/exe RE, `docs/re_devotion_gamedll.md`:
   `GAME::Skill::GetDevotionParent()` is NOT this link parent -- it is the object id of the skill a celestial
   power is bound to. The link tree lives only in the exe's Star objects (`star+0x118`) and in this data;
   `Skill::GetConstellationDependencies()` is the reclaim-mode list of constellations that would lose their
   affinity requirement, not the constellation gate.)
3. **Crossroads = tier-0, one star, one point, no requirement, gives 1 affinity.** Five separate
   records, all sharing display tag `tagDevotion_A15` ("Crossroads"):
   - `records/ui/skills/devotion/constellations/constellation15.dbr` -> star `tier1_15a`, gives **Primordial +1** (star grants `characterDefensiveAbility = 18.0`)
   - `records/ui/skills/devotion/constellations/constellation80.dbr` -> star `tier1_15b`, gives **Chaos +1**
   - `records/ui/skills/devotion/constellations/constellation81.dbr` -> star `tier1_15c`, gives **Order +1**
   - `records/ui/skills/devotion/constellations/constellation82.dbr` -> star `tier1_15d`, gives **Eldritch +1**
   - `records/ui/skills/devotion/constellations/constellation83.dbr` -> star `tier1_15e`, gives **Ascendant +1**

   Each has NO `affinityRequired*` and NO `devotionLinks*`.
   `records/ui/skills/devotion/constellations/constellation87.dbr` is the artwork only (0 stars).
4. **The "self-locked" rule is NOT a data field -- it is a computed property.** The engine exports
   `public: bool GAME::Skill::GetConstellationSelfLocked(void) const` and
   `public: void GAME::Skill::SetConstellationSelfLocked(bool)`, so it is set at runtime, and the UI
   string for it is `tagRemoveBase = Devotion Point Cannot be Removed`. The data condition that makes
   it possible is visible though: **every non-Crossroads tier-1 and tier-2 constellation requires an
   affinity that it also gives** -- exactly 68 of the 86 pickable constellations do: all 37 tier-1
   non-Crossroads (1-14, 16-35, 84-86) and all 31 tier-2 (36-65, 79). The 5 Crossroads require
   nothing and the 13 tier-3 give nothing at all, so neither can self-lock.
   Examples: Bat gives `Chaos +2, Eldritch +3` and requires `Eldritch 1`; Rhowan's Crown gives
   `Eldritch +1, Ascendant +1` and requires `Eldritch 6, Ascendant 4`. So a constellation whose own
   completion bonus is what keeps it (or a downstream one) above its requirement cannot be refunded --
   that is the flag, computed from the live affinity totals, not stored per-record.
5. **A constellation's completion bonus is exactly its `affinityGiven*`.** There is no other bonus
   field. Tier-3 constellations give **nothing** (`affinityGiven1..3` all 0 in all 13) -- they are pure
   sinks. Tier-1s give 3..6 total affinity, tier-2s give 1..5.

---

## 5. Devotion shrines, points, and the reset item

### Shrines
`records/interactive/devotionshrine*.dbr`, all `Class = StaticShrine`, template
`database/templates/staticshrine.tpl`. **31 records: 30 real shrines with `devotionPoints = 1`, plus
`records/interactive/devotionshrine_mogdrogen.dbr` ("Quest Shrine - Rover Legacy") with
`devotionPoints = 0`.** Naming runs A01-A08, B01-B07, C01-C08, D01-D05, S01-S02 and each has a
`journalTag = tagDevotionShrineXNN` giving its place name (section 6).

Common fields:
- `devotionPoints = 1` -- the reward. Engine: `GAME::StaticShrine::GetDevotionPoints() const` (exported).
- `usableRange = 4.0`, `uiCloseDistance = 7.0`, `bindRadius = 80.0`, `actorRadius = 1.25`
- Per difficulty (`normal` / `epic` / `legendary`) triples:
  `<diff>LootTable` (a `records/interactive/devotionloot/devotion_container_*.dbr`),
  `<diff>XPReward` (50 / 5000 / 15000 on A01), `<diff>Disabled` (the shrine is not available on that
  difficulty), `<diff>Locked` (present but gated -- `lockedTextTag` says why).
- Meshes tell the two kinds apart:
  - **Ruined** (`mesh = level art/interactive/shrine_devotion_ruined01a.msh`, 18 records): restored by
    **offerings**, `<diff>Offering1`, `<diff>Offering2`, ... e.g.
    `records/interactive/devotionshrineb03.dbr` (Undercity)
    `normalOffering1 = records/items/materia/compa_chilledsteel.dbr`,
    `epicOffering1 = records/items/materia/compa_coldstone.dbr`,
    `legendaryOffering1 = records/items/materia/compa_coldstone.dbr`;
    `records/interactive/devotionshrines01.dbr` (Secret Ritual) uses slot 2 only,
    `normalOffering2 = records/items/crafting/materials/craft_skeletonkey.dbr`.
    `records/interactive/devotionshrinea01.dbr` (Burial Hill) takes
    `records/items/materia/compa_aethercrystal.dbr` on normal and
    `records/items/crafting/materials/craft_aethershard.dbr` on epic/legendary.
  - **Corrupted** (`mesh = level art/interactive/shrine_devotion_corrupted01a.msh`, 12 records):
    restored by **killing a spawn**, `<diff>MonsterSpawn = records/proxies/devotion/devotionproxy_*.dbr`,
    plus `proxyActivateSound` and
    `dormantLoopingEffect = records/fx/ambient/fx_shrine_devotion_corrupted01.dbr`.
  - The Mogdrogen quest shrine uses `mesh = level art/interactive/shrine_mogdrogen_ruined01a.msh` and
    takes quest items (`epicOffering1 = records/items/questitems/quest_mogdrogenshrine_01.dbr`).
- `lockedTextTag` -- `tagShrineBossLocked = " {^R} ~ Sealed by a Powerful Presence"` on the
  boss-gated ones, `tagShrineMogdrogenLocked = " {^R} ~ Sealed by Chthonic Energies"`.
- Animations/sounds: `dormantAnimation`, `dormantToRestoredAnimation`,
  `dormantToRestoredSound = records/sounds/skillsounds/devotion/spak_devotionshrine_restored.dbr`,
  `restoredAnimation`, `restoredLoopingEffect = records/fx/ambient/fx_shrine_devotion_restored01.dbr`.

### Total devotion points -- the cap is `records/creatures/pc/playerlevels.dbr` `maxDevotionPoints = 50`
(CORRECTION: the first scan missed it because it is not on a devotion record.) Read back live through
`GAME::Character::GetMaxDevotionPoints()` (`Character+0x1778`).

What the data implies: a shrine can be restored **once per difficulty**, and each restore gives its
`devotionPoints`. Summing the 30 point-bearing shrines over the difficulties where `<diff>Disabled` is
not true gives **28 on Normal + 18 on Elite + 15 on Ultimate = 61**. That is more than the 55 usually
quoted for the whole game, so either the cap is enforced in code, or some of those shrines are simply
not placed in the world on that difficulty (level placement is not in `database.arz`). **Treat 61 as
an upper bound on base-game shrine restorations, not as the point cap; read
`GetMaxDevotionPoints()` at runtime.** Supporting evidence for the per-difficulty model:
achievement `ach038Desc = "Restore 50 Devotion Shrines."` with only 30 shrine records in the game.

Also on the engine side, `GAME::ScriptableAction_GiveDevotion` (with `GetAmount()`) exists -- quests
and Lua can hand out devotion points directly, which the database does not record.
The `bonusDevotionPoints` field appears on 8 loot-chest / food-crate records and is **0 in every one**.

### Reclaim cost (per point) -- on the PC record
`records/creatures/pc/malepc01.dbr` and `records/creatures/pc/femalepc01.dbr`:

```
devotionReclamationAetherCost   = 1
devotionReclamationPointTiers   = [0, 20, 40, 60, 80, 100, 120, 140, 160, 180,
                                   200, 220, 240, 260, 280, 300, 320, 340, 360, 380]
devotionReclamationPointCosts   = [25, 50, 100, 150, 200, 300, 400, 500, 750, 1000,
                                   1250, 1500, 2000, 2500, 3000, 4000, 5000, 7500, 10000, 15000]
```

Same shape as the skill-point pair `reclamationPointTiers` / `reclamationPointCosts` on the same record
(byte-identical values). Reading: a tier index is chosen from the tiers array (threshold) and the
costs array gives the iron-bit price, plus **1 Aether Crystal per point**. The UI string confirms both
currencies:
`tagReclaimDevotionPoint = "Click to Reclaim Devotion Point (Cost {^s}{%t0}{^-} Iron Bits, {^g}{%t1}{^-} Aether Crystal)"`.
Engine: `GAME::SkillManager::GetCurrentDevotionReclamationCost()`,
`GetDevotionReclamationAetherCost()`, `UseDevotionReclamationPoints(int)`,
`GAME::ControllerCharacter::SendReclaimDevotionPointCmd(int, int)` (all exported).
**Note:** the exact meaning of "tier index" (points reclaimed so far vs points spent) was NOT
established from data -- the arrays run to 380, far past any plausible devotion total, which is why
they look generic/shared with the skill-point path. Verify against
`GetCurrentDevotionReclamationCost()` live.

### The full reset item ("Tonic of Clarity")
- `records/items/misc/potions/potion_devotionreset.dbr` -- `Class = ItemDevotionReset`, template
  `database/templates/itemdevotionreset.tpl`, `itemClassification = Epic`, `itemCost = 12000`,
  `description = tagConsumable_DevotionReset` ("Tonic of Clarity"),
  `itemText = tagConsumable_DevotionReset_Desc`, `bitmap = items/misc/potion_devotionreset.tex`,
  `mesh = items/misc/potion_devotionreset.msh`.
- Its blueprint: `records/items/crafting/blueprints/other/craft_devotionreset.dbr` --
  `Class = ItemArtifactFormula`, `artifactName = records/items/misc/potions/potion_devotionreset.dbr`,
  `artifactCreationCost = 150000` (iron bits),
  `reagentBaseBaseName = records/items/crafting/materials/craft_aethershard.dbr`,
  `reagentBaseQuantity = 8`, `artifactCreateQuantity = 1`, `itemClassification = Legendary`.
- There is **no separate "tonic of clarity" record** -- `tagConsumable_DevotionReset` IS that name.
  (`records/skills/itemskills/legendary/item_momentofclarity.dbr` is an unrelated item skill.)
- Engine: `GAME::ItemDevotionReset` (`Use(Character*)`, `AllowUse(bool&)`, `GetUIDisplayText`, its own
  `GetStaticClassInfo` for the RTTI is-a check) plus `GAME::CursorHandlerDevotionReset` (the
  "pick this up and use it" cursor) and `GAME::GameEngine::DevotionPointsInUse()`. UI strings:
  `tagDevotionReset = "{^E}Use to Reset your Devotion Points"`,
  `tagDevotionResetConfirmation = "Reset all your Devotion Points?"`,
  `tagDevotionNoPointsUsed = "{^r}[No Devotion Points to Reset]"`.

---

## 6. Localization tags for the devotion UI (from `Text_EN.arc`)

### Panel and counters (`tags_skills.txt`)
- `tagSkillDevotion = Devotion`
- `tagDevotionInfo = In its darkest hour, humanity turns to the stars. Devote yourself to the celestial symbols and deities of old. Reclaim their ancient power.`
- `DevotionPointsAvailable = Points Available: {%d0}` (and `DevotionPointsAvailableSingular`, same text)
- `NoDevotionPointsAvailable = Points Available: 0`
- `DevotionPointsTotal = Points Unlocked: {%d0} / {%d1}`

### Affinities
- `tagDevotionAffinity01 = Ascendant` / `tagDevotionAffinity01Info = The quantity of Ascendant Affinity you have harnessed.^n^nAscendant beings have risen to godlike status through immense feats or reverence from their lessers.`
- `tagDevotionAffinity02 = Chaos` / `...02Info = ...Chaotic beings crave entropy and destruction above all else.`
- `tagDevotionAffinity03 = Eldritch` / `...03Info = ...Eldritch beings represent the wild forces of magic and the metaphysical realms beyond the veil.`
- `tagDevotionAffinity04 = Order` / `...04Info = ...Beings of Order cherish harmony and balance in all things.`
- `tagDevotionAffinity05 = Primordial` / `...05Info = ...Primordial beings embody the powers of creation, life, death, and reality itself.`

### Point actions
- `tagReclaimDevotionPoint = Click to Reclaim Devotion Point (Cost {^s}{%t0}{^-} Iron Bits, {^g}{%t1}{^-} Aether Crystal)`
- `tagRemovePoint = Left click to Remove Devotion Point`
- `tagReclaimDevotion = Remove Attached Devotion Effect`
- `tagRemoveBase = Devotion Point Cannot be Removed`   (the self-locked message)
- `tagDevotionReset = {^E}Use to Reset your Devotion Points`
- `tagDevotionResetConfirmation = Reset all your Devotion Points?`
- `tagDevotionNoPointsUsed = {^r}[No Devotion Points to Reset]`
- `tagConsumable_DevotionReset = Tonic of Clarity`
- `tagConsumable_DevotionReset_Desc = "Clears the mind and primes it for a new spiritual journey."`

### Celestial-power weapon requirements
- `tagDevotion_RequiresSword = ^oRequires a sword.`
- `tagDevotion_RequiresAxe = ^oRequires an axe.`
- `tagDevotion_RequiresMace = ^oRequires a mace.`
- `tagDevotion_RequiresSpear = ^oRequires a spear.`
- `tagDevotion_RequiresAxeSpear = ^oRequires an axe or spear.`
- `tagDevotion_RequiresShield = ^oRequires a shield.`
- `tagDevotion_RequiresCaster = ^oRequires a scepter or dagger.`
- `tagDevotion_RequiresFocus = ^oRequires a caster off-hand.`
- `tagDevotion_RequiresMagic = ^oRequires a scepter, dagger or caster off-hand.`
- `tagDevotion_Requires1hRange = ^oRequires a one-handed ranged weapon.`
- `tagDevotion_Requires2hRange = ^oRequires a two-handed ranged weapon.`
- `tagDevotion_RequiresRange = ^oRequires a ranged weapon.`
- `tagDevotion_Requires2h = ^oRequires a two-handed melee or two-handed ranged weapon.`

### Shrines
- `tagShrineBossLocked = " {^R} ~ Sealed by a Powerful Presence"`
- `tagShrineMogdrogenLocked = " {^R} ~ Sealed by Chthonic Energies"`
- `tagQuestMogdrogenShrine = Shrine of Mogdrogen`
- `tagMapBloodGroveShrine = Shrine of the Forgotten God`
- `tagDevotionShrineA01..A08 / B01..B07 / C01..C08 / D01..D05 / S01..S02` -- shrine place names, e.g.
  A01 "Burial Hill", A02 "Foggy Bank", A03 "Burrwitch Outskirts", A04 "Flooded Passage",
  A05 "Burrwitch Estates", A06 "Devil's Aquifer", A07 "Warden's Lab", A08 "East Marsh",
  B01 "Arkovian Foothills", B02 "Old Arkovia", B03 "Arkovian Undercity", B04 "Cronley's Hideout",
  B05 "Barren Highlands", B06 "Rocky Coast", B07 "Steps Of Torment", C01 "Mountain Deeps",
  C02 "Forgotten Depths", C03 "Tyrant's Hold", C04 "Den of the Lost", C05 "Infested Farms",
  C06 "Darkvale Village", C07 "Bastion of Chaos".

### Tips
- `tagQuickTip61 = Celestial Powers{^-}{^n}{^n}Assign your Celestial Power to a skill by pressing the icon in the Devotion Window and selecting a skill from the list.`
- `tagTutorialTip59TextC = When a shrine is cleansed you earn a devotion point. To allocate your Devotion Points visit the Devotion Window, which can be accessed through the Skill Window.`
- `tagTutorialTip60TextF = Bright blue stars indicate where you can put a Devotion Point in next. Activating a star unlocks its bonus.`

### Names
Constellation names are `tagDevotion_A01..A38` / `B01..B31` / `C01..C13` with flavour text in
`tagDevotion_XNNDesc`; celestial power names are `tagDevotionEffectA01..` / `B01..` / `C01..C14` with
`...Desc` for the description. Every constellation and power in the appendix below is already
resolved through those tags.

---

## 7. Engine surface (exported, from `tools/exports/Game.x64.txt`)

Not part of the data question, but the data above is only useful through these. All are exports of
`Game.dll`:

```
GAME::Character::GetDevotionPoints() const                        -> unsigned int (unspent)
GAME::Character::GetTotalDevotionPoints() const                   -> unsigned int
GAME::Character::GetMaxDevotionPoints() const                     -> unsigned int  (the cap; data has none)
GAME::Character::AddDevotionPoints(unsigned int)
GAME::Character::AddTotalDevotionPoints(unsigned int)
GAME::Character::RemoveDevotionPoints(unsigned int)
GAME::Character::RemoveTotalDevotionPoints(unsigned int)
GAME::Character::SubtractDevotionPoint()
GAME::Character::GetAffinity(enum GAME::AffinityType) const       -> unsigned int
GAME::Character::AddAffinity(enum GAME::AffinityType, unsigned int)
GAME::Character::SubtractAffinity(enum GAME::AffinityType, unsigned int)
GAME::Skill::GetAffinityBonus() const        -> mem::vector<std::pair<AffinityType, unsigned int>> const&
GAME::Skill::GetAffinityDependencies() const -> mem::vector<std::pair<AffinityType, unsigned int>> const&
GAME::Skill::GetConstellationDependencies() const -> mem::vector<std::string> const&
GAME::Skill::GetConstellationSelfLocked() const -> bool
GAME::Skill::GetDevotionParent() const       -> unsigned int   (the devotionLinks parent)
GAME::Skill::GetDevotionLevel() const / GetDevotionMaxLevel() const / GetDevotionExperience() const
GAME::Skill::IncrementDevotionLevel()  (virtual)
GAME::SkillProfile::GetDevotionMaxLevel() const
GAME::SkillManager::GetNumDevotionPointsSpent() const
GAME::SkillManager::GetCurrentDevotionReclamationCost() const
GAME::SkillManager::GetDevotionReclamationAetherCost() const
GAME::SkillManager::UseDevotionReclamationPoints(int) -> bool
GAME::ControllerCharacter::SendReclaimDevotionPointCmd(int, int)
GAME::GameEngine::GenerateUIDevotionText(Skill const*, Skill const*,
        mem::vector<GameTextLine>&, SkillReasons const*, bool, bool, int, int, GameTextClass)  [static]
GAME::GameEngine::GenerateUIDevotionSearchText(Skill const*, mem::vector<GameTextLine>&, GameTextClass) [static]
GAME::GameEngine::GetAffinityBitmap(enum GAME::AffinityType) const
GAME::GameEngine::DevotionPointsInUse() -> bool
GAME::StaticShrine::GetDevotionPoints() const
GAME::ItemDevotionReset::{Use, AllowUse, GetUIDisplayText, GetStaticClassInfo, ...}
GAME::CursorHandlerDevotionReset::{ActivateWorld, Cancel, Escape, IsShrineCapable, ...}
GAME::ScriptableAction_GiveDevotion::{Execute(Entity*), GetAmount()}
GAME::PlayStats::UnlockedDevotionShrine()
```

`GenerateUIDevotionText` takes a `SkillReasons const*`, the same struct the skills window uses for
"needs mastery N" -- so the game will hand back the *reason* a star cannot be taken, exactly as
`gameapi::can_learn_skill` does for class skills (see `docs/skills-targeting.md`). Note the setters
(`SetAffinityBonus`, `SetAffinityDepedency` [sic], `SetConstellationDependencies`,
`SetConstellationSelfLocked`, `SetDevotionParent`, `SetDevotionLevel`) are also exported -- that is how
the loader pushes the constellation DBR data onto the Skill objects.

---

## What I could NOT establish from the data

- **The runtime `AffinityType` enum order.** UI slots say Ascendant, Chaos, Eldritch, Order,
  Primordial; the DBR editor template's drop-down says Primordial, Chaos, Order, Eldritch, Ascendant.
  The data only ever stores the affinity *name string*. Must be checked live.
- **The devotion point cap.** No field anywhere. Data allows 61 base-game shrine restorations across
  three difficulties; the cap lives in `GetMaxDevotionPoints()`.
- **Exactly how `devotionReclamationPointTiers` / `Costs` is indexed** (points reclaimed so far?
  points spent?). The arrays go to 380 and are byte-identical to the skill-point pair, so they look
  generic.
- **The celestial-power-to-skill assignment.** Confirmed absent from the database (zero fields
  matching "assign"/"celestial"); it is save/runtime state.
- **Expansion content.** No `gdx*` database is installed here, so nothing about Ashes of Malmouth /
  Forgotten Gods constellations, shrines or affinity totals is covered, and `tools/arz.py` reads only
  `database/database.arz`.
- **Whether `<diff>Disabled` on a shrine means "not placed" or "cannot be restored"** -- the level
  placement is not in `database.arz`.
- **`records/skills/devotion/tier2_15g_skill.dbr` (Blizzard) has `skillMaxLevel = 60`** where every
  other star has 1. Read as a data slip, not verified.

---

## Appendix: all 87 constellation records

Format: record, localized name (`display tag`), star count, completion bonus, affinity gate, the
`devotionLinks` tree as `child<-parent`, and the celestial power star(s). All records live under
`records/ui/skills/devotion/constellations/`.

- `constellation01.dbr` Bat (`tagDevotion_A01`): 5 stars; gives Chaos +2, Eldritch +3; requires Eldritch 1; links 2<-1 3<-2 4<-3 5<-4; star 5 = Twin Fangs (`records/skills/devotion/tier1_01e_skill.dbr`)
- `constellation02.dbr` Akeron's Scorpion (`tagDevotion_A02`): 5 stars; gives Eldritch +5; requires Eldritch 1; links 2<-1 3<-2 4<-3 5<-3; star 5 = Scorpion Sting (`records/skills/devotion/tier1_02e_skill.dbr`)
- `constellation03.dbr` Raven (`tagDevotion_A03`): 4 stars; gives Eldritch +5; requires Eldritch 1; links 2<-1 3<-2 4<-2; no celestial power
- `constellation04.dbr` Hammer (`tagDevotion_A04`): 3 stars; gives Ascendant +4; requires Ascendant 1; links 2<-1 3<-2; no celestial power
- `constellation05.dbr` Anvil (`tagDevotion_A05`): 5 stars; gives Ascendant +5; requires Ascendant 1; links 2<-1 3<-2 4<-3 5<-4; star 5 = Targo's Hammer (`records/skills/devotion/tier1_05e_skill.dbr`)
- `constellation06.dbr` Shepherd's Crook (`tagDevotion_A06`): 5 stars; gives Ascendant +5; requires Ascendant 1; links 2<-1 3<-2 4<-3 5<-4; star 5 = Shepherd's Call (`records/skills/devotion/tier1_06e_skill.dbr`)
- `constellation07.dbr` Sailor's Guide (`tagDevotion_A07`): 4 stars; gives Primordial +5; requires Primordial 1; links 2<-1 3<-2 4<-2; no celestial power
- `constellation08.dbr` Assassin's Blade (`tagDevotion_A08`): 5 stars; gives Ascendant +3, Order +2; requires Order 1; links 2<-1 3<-1 4<-3 5<-4; star 5 = Assassin's Mark (`records/skills/devotion/tier1_08e_skill.dbr`)
- `constellation09.dbr` Eye of the Guardian (`tagDevotion_A09`): 5 stars; gives Eldritch +3, Ascendant +3; requires Eldritch 1; links 2<-1 3<-2 4<-3 5<-4; star 5 = Guardian's Gaze (`records/skills/devotion/tier1_09e_skill.dbr`)
- `constellation10.dbr` Falcon (`tagDevotion_A10`): 5 stars; gives Eldritch +3, Ascendant +3; requires Ascendant 1; links 2<-1 3<-2 4<-3 5<-4; star 5 = Falcon Swoop (`records/skills/devotion/tier1_10e_skill.dbr`)
- `constellation11.dbr` Eel (`tagDevotion_A11`): 3 stars; gives Primordial +5; requires Primordial 1; links 2<-1 3<-2; no celestial power
- `constellation12.dbr` Owl (`tagDevotion_A12`): 4 stars; gives Ascendant +5; requires Ascendant 1; links 2<-1 3<-2 4<-2; no celestial power
- `constellation13.dbr` Viper (`tagDevotion_A13`): 4 stars; gives Primordial +3, Chaos +2; requires Chaos 1; links 2<-1 3<-2 4<-3; no celestial power
- `constellation14.dbr` Gallows (`tagDevotion_A14`): 4 stars; gives Primordial +5; requires Primordial 1; links 2<-1 3<-2 4<-3; no celestial power
- `constellation15.dbr` Crossroads (`tagDevotion_A15`): 1 stars; gives Primordial +1; requires nothing; links none; no celestial power
- `constellation16.dbr` Empty Throne (`tagDevotion_A16`): 4 stars; gives Ascendant +5; requires Ascendant 1; links 2<-1 3<-2 4<-2; no celestial power
- `constellation17.dbr` Rat (`tagDevotion_A17`): 4 stars; gives Eldritch +3, Chaos +2; requires Chaos 1; links 2<-1 3<-2 4<-3; no celestial power
- `constellation18.dbr` Tsunami (`tagDevotion_A18`): 5 stars; gives Primordial +5; requires Primordial 1; links 2<-1 3<-2 4<-3 5<-4; star 5 = Tsunami (`records/skills/devotion/tier1_18e_skill.dbr`)
- `constellation19.dbr` Imp (`tagDevotion_A19`): 5 stars; gives Primordial +3, Eldritch +3; requires Primordial 1; links 2<-1 3<-2 4<-3 5<-4; star 5 = Aetherfire (`records/skills/devotion/tier1_19e_skill.dbr`)
- `constellation20.dbr` Fiend (`tagDevotion_A20`): 5 stars; gives Eldritch +3, Chaos +2; requires Chaos 1; links 2<-1 3<-2 4<-3 5<-4; star 5 = Flame Torrent (`records/skills/devotion/tier1_20e_skill.dbr`)
- `constellation21.dbr` Bull (`tagDevotion_A21`): 5 stars; gives Order +2, Primordial +3; requires Primordial 1; links 2<-1 3<-2 4<-3 5<-3; star 5 = Bull Rush (`records/skills/devotion/tier1_21e_skill.dbr`)
- `constellation22.dbr` Wraith (`tagDevotion_A22`): 4 stars; gives Ascendant +3, Primordial +3; requires Primordial 1; links 2<-1 3<-1 4<-1; no celestial power
- `constellation23.dbr` Harpy (`tagDevotion_A23`): 4 stars; gives Ascendant +5; requires Ascendant 1; links 2<-1 3<-2 4<-2; no celestial power
- `constellation24.dbr` Fox (`tagDevotion_A24`): 4 stars; gives Eldritch +5; requires Eldritch 1; links 2<-1 3<-2 4<-3; no celestial power
- `constellation25.dbr` Ghoul (`tagDevotion_A25`): 5 stars; gives Chaos +3; requires Chaos 1; links 2<-1 3<-2 4<-2 5<-4; star 5 = Ghoulish Hunger (`records/skills/devotion/tier1_25e_skill.dbr`)
- `constellation26.dbr` Dryad (`tagDevotion_A26`): 5 stars; gives Order +3; requires Order 1; links 2<-1 3<-2 4<-3 5<-4; star 5 = Dryad's Blessing (`records/skills/devotion/tier1_26e_skill.dbr`)
- `constellation27.dbr` Hawk (`tagDevotion_A27`): 3 stars; gives Eldritch +3; requires Eldritch 1; links 2<-1 3<-2; no celestial power
- `constellation28.dbr` Wolverine (`tagDevotion_A28`): 5 stars; gives Ascendant +6; requires Ascendant 1; links 2<-1 3<-2 4<-3 5<-3; no celestial power
- `constellation29.dbr` Tortoise (`tagDevotion_A29`): 5 stars; gives Primordial +3, Order +2; requires Order 1; links 2<-1 3<-2 4<-3 5<-3; star 5 = Turtle Shell (`records/skills/devotion/tier1_29e_skill.dbr`)
- `constellation30.dbr` Panther (`tagDevotion_A30`): 4 stars; gives Primordial +3, Order +2; requires Order 1; links 2<-1 3<-2 4<-3; no celestial power
- `constellation31.dbr` Crane (`tagDevotion_A31`): 5 stars; gives Order +5; requires Order 1; links 2<-1 3<-2 4<-3 5<-4; no celestial power
- `constellation32.dbr` Vulture (`tagDevotion_A32`): 5 stars; gives Chaos +5; requires Chaos 1; links 2<-1 3<-2 4<-2 5<-2; no celestial power
- `constellation33.dbr` Hound (`tagDevotion_A33`): 3 stars; gives Primordial +4; requires Primordial 1; links 2<-1 3<-2; no celestial power
- `constellation34.dbr` Spider (`tagDevotion_A34`): 5 stars; gives Eldritch +6; requires Eldritch 1; links 2<-1 3<-1 4<-1 5<-1; no celestial power
- `constellation35.dbr` Lizard (`tagDevotion_A35`): 3 stars; gives Primordial +4; requires Primordial 1; links 2<-1 3<-2; no celestial power
- `constellation36.dbr` Rhowan's Crown (`tagDevotion_B01`): 5 stars; gives Eldritch +1, Ascendant +1; requires Eldritch 6, Ascendant 4; links 2<-1 3<-2 4<-3 5<-4; star 3 = Elemental Storm (`records/skills/devotion/tier2_01c_skill.dbr`)
- `constellation37.dbr` Scales of Ulcama (`tagDevotion_B02`): 6 stars; gives Order +2; requires Order 8; links 2<-1 3<-2 4<-3 5<-2 6<-5; star 6 = Tip the Scales (`records/skills/devotion/tier2_02f_skill.dbr`)
- `constellation38.dbr` Wendigo (`tagDevotion_B03`): 6 stars; gives Chaos +2; requires Primordial 6, Chaos 4; links 2<-1 3<-2 4<-3 5<-4 6<-5; star 6 = Wendigo's Mark (`records/skills/devotion/tier2_03f_skill.dbr`)
- `constellation39.dbr` Huntress (`tagDevotion_B04`): 7 stars; gives Eldritch +1, Ascendant +1; requires Ascendant 4, Eldritch 4, Chaos 3; links 2<-1 3<-2 4<-3 5<-3 6<-5 7<-5; star 7 = Rend (`records/skills/devotion/tier2_04g_skill.dbr`)
- `constellation40.dbr` Dire Bear (`tagDevotion_B05`): 6 stars; gives Primordial +1, Ascendant +1; requires Ascendant 5, Primordial 5; links 2<-1 3<-2 4<-3 5<-4 6<-4; star 6 = Maul (`records/skills/devotion/tier2_05f_skill.dbr`)
- `constellation41.dbr` Assassin (`tagDevotion_B06`): 7 stars; gives Ascendant +1, Order +1; requires Ascendant 6, Order 4; links 2<-1 3<-2 4<-2 5<-4 6<-4 7<-6; star 7 = Blades of Wrath (`records/skills/devotion/tier2_06g_skill.dbr`)
- `constellation42.dbr` Magi (`tagDevotion_B07`): 7 stars; gives Eldritch +3; requires Eldritch 10; links 2<-1 3<-2 4<-3 5<-3 6<-3 7<-6; star 7 = Fissure (`records/skills/devotion/tier2_07g_skill.dbr`)
- `constellation43.dbr` Autumn Boar (`tagDevotion_B08`): 7 stars; gives Ascendant +3; requires Primordial 4, Ascendant 4, Order 3; links 2<-1 3<-2 4<-3 5<-3 6<-5 7<-5; star 7 = Trample (`records/skills/devotion/tier2_08g_skill.dbr`)
- `constellation44.dbr` Widow (`tagDevotion_B09`): 6 stars; gives Primordial +3; requires Eldritch 6, Primordial 4; links 2<-1 3<-2 4<-3 5<-4 6<-5; star 6 = Arcane Bomb (`records/skills/devotion/tier2_09f_skill.dbr`)
- `constellation45.dbr` Revenant (`tagDevotion_B10`): 6 stars; gives Primordial +1, Chaos +1; requires Chaos 8; links 2<-1 3<-2 4<-3 5<-4 6<-5; star 6 = Raise the Dead (`records/skills/devotion/tier2_10f_skill.dbr`)
- `constellation46.dbr` Solael's Witchblade (`tagDevotion_B11`): 5 stars; gives Eldritch +1, Chaos +1; requires Eldritch 6, Chaos 4; links 2<-1 3<-2 4<-3 5<-4; star 5 = Eldritch Fire (`records/skills/devotion/tier2_11e_skill.dbr`)
- `constellation47.dbr` Bysmiel's Bonds (`tagDevotion_B12`): 5 stars; gives Eldritch +3; requires Eldritch 6, Chaos 4; links 2<-1 3<-2 4<-3 5<-4; star 5 = Bysmiel's Command (`records/skills/devotion/tier2_12e_skill.dbr`)
- `constellation48.dbr` Tempest (`tagDevotion_B13`): 7 stars; gives Eldritch +1, Primordial +1; requires Primordial 5, Ascendant 5; links 2<-1 3<-2 4<-3 5<-4 6<-4 7<-6; star 7 = Reckless Tempest (`records/skills/devotion/tier2_13g_skill.dbr`)
- `constellation49.dbr` Targo the Builder (`tagDevotion_B14`): 7 stars; gives Order +1; requires Primordial 6, Order 4; links 2<-1 3<-2 4<-2 5<-4 6<-4 7<-6; star 7 = Shield Wall (`records/skills/devotion/tier2_14g_skill.dbr`)
- `constellation50.dbr` Amatok the Spirit of Winter (`tagDevotion_B15`): 7 stars; gives Primordial +1, Eldritch +1; requires Primordial 6, Eldritch 4; links 2<-1 3<-2 4<-2 5<-4 6<-2 7<-6; star 7 = Blizzard (`records/skills/devotion/tier2_15g_skill.dbr`)
- `constellation51.dbr` Affliction (`tagDevotion_B16`): 7 stars; gives Eldritch +1, Ascendant +1; requires Eldritch 4, Ascendant 4, Chaos 3; links 2<-1 3<-2 4<-3 5<-4 6<-3 7<-6; star 3 = Fetid Pool (`records/skills/devotion/tier2_16c_skill.dbr`)
- `constellation52.dbr` Crab (`tagDevotion_B17`): 5 stars; gives Ascendant +3; requires Ascendant 6, Order 4; links 2<-1 3<-2 4<-3 5<-4; star 3 = Arcane Barrier (`records/skills/devotion/tier2_17c_skill.dbr`)
- `constellation53.dbr` Manticore (`tagDevotion_B18`): 6 stars; gives Ascendant +1, Eldritch +1; requires Eldritch 6, Chaos 4; links 2<-1 3<-2 4<-3 5<-3 6<-5; star 6 = Acid Spray (`records/skills/devotion/tier2_18f_skill.dbr`)
- `constellation54.dbr` Solemn Watcher (`tagDevotion_B19`): 5 stars; gives Primordial +3, Order +2; requires Primordial 10; links 2<-1 3<-2 4<-3 5<-4; no celestial power
- `constellation55.dbr` Messenger of War (`tagDevotion_B20`): 6 stars; gives Primordial +3, Chaos +2; requires Primordial 7, Ascendant 3; links 2<-1 3<-2 4<-3 5<-2 6<-5; star 6 = Messenger of War (`records/skills/devotion/tier2_20f_skill.dbr`)
- `constellation56.dbr` Kraken (`tagDevotion_B21`): 5 stars; gives Primordial +3, Chaos +2; requires Primordial 5, Eldritch 5; links 2<-1 3<-1 4<-1 5<-1; no celestial power
- `constellation57.dbr` Hydra (`tagDevotion_B22`): 6 stars; gives Eldritch +3, Chaos +2; requires Eldritch 5, Ascendant 3, Chaos 3; links 2<-1 3<-2 4<-2 5<-4 6<-2; no celestial power
- `constellation58.dbr` Blades of Nadaan (`tagDevotion_B23`): 6 stars; gives Ascendant +3, Order +2; requires Ascendant 10; links 2<-1 3<-2 4<-2 5<-2 6<-2; no celestial power
- `constellation59.dbr` Rhowan's Scepter (`tagDevotion_B24`): 6 stars; gives Ascendant +3, Order +2; requires Ascendant 6, Order 4; links 2<-1 3<-2 4<-3 5<-2 6<-5; no celestial power
- `constellation60.dbr` Berserker (`tagDevotion_B25`): 6 stars; gives Eldritch +3, Chaos +2; requires Ascendant 5, Eldritch 5; links 2<-1 3<-2 4<-1 5<-4 6<-1; no celestial power
- `constellation61.dbr` Oklaine's Lantern (`tagDevotion_B26`): 5 stars; gives Eldritch +3, Order +2; requires Eldritch 10; links 2<-1 3<-2 4<-3 5<-4; no celestial power
- `constellation62.dbr` Shieldmaiden (`tagDevotion_B27`): 6 stars; gives Primordial +3, Order +2; requires Primordial 6, Order 4; links 2<-1 3<-2 4<-3 5<-2 6<-5; no celestial power
- `constellation63.dbr` Behemoth (`tagDevotion_B28`): 6 stars; gives Eldritch +3, Chaos +2; requires Primordial 4, Eldritch 4, Chaos 3; links 2<-1 3<-2 4<-2 5<-2 6<-2; star 6 = Giant's Blood (`records/skills/devotion/tier2_28f_skill.dbr`)
- `constellation64.dbr` Chariot of the Dead (`tagDevotion_B29`): 7 stars; gives Eldritch +3, Chaos +2; requires Ascendant 5, Eldritch 5; links 2<-1 3<-2 4<-3 5<-3 6<-5 7<-6; star 7 = Wayward Soul (`records/skills/devotion/tier2_29g_skill.dbr`)
- `constellation65.dbr` Ulo the Keeper of the Waters (`tagDevotion_B30`): 5 stars; gives Primordial +3, Order +2; requires Primordial 6, Order 4; links 2<-1 3<-2 4<-2 5<-2; star 5 = Cleansing Waters (`records/skills/devotion/tier2_30e_skill.dbr`)
- `constellation66.dbr` Aeon's Hourglass (`tagDevotion_C01`): 6 stars; gives nothing; requires Chaos 8, Primordial 18; links 2<-1 3<-2 4<-3 5<-4 6<-5; star 6 = Time Dilation (`records/skills/devotion/tier3_01f_skill.dbr`)
- `constellation67.dbr` Abomination (`tagDevotion_C02`): 8 stars; gives nothing; requires Chaos 8, Eldritch 18; links 2<-1 3<-2 4<-3 5<-4 6<-3 7<-6 8<-7; star 5 = Abominable Might (`records/skills/devotion/tier3_02e_skill.dbr`); star 8 = Tainted Eruption (`records/skills/devotion/tier3_02h_skill.dbr`)
- `constellation68.dbr` Light of Empyrion (`tagDevotion_C03`): 7 stars; gives nothing; requires Order 8, Primordial 18; links 2<-1 3<-2 4<-3 5<-4 6<-5 7<-6; star 7 = Light of Empyrion (`records/skills/devotion/tier3_03g_skill.dbr`)
- `constellation69.dbr` Oleron (`tagDevotion_C04`): 7 stars; gives nothing; requires Ascendant 20, Order 7; links 2<-1 3<-2 4<-3 5<-4 6<-4 7<-4; star 7 = Blind Fury (`records/skills/devotion/tier3_04g_skill.dbr`)
- `constellation70.dbr` Obelisk of Menhir (`tagDevotion_C05`): 7 stars; gives nothing; requires Order 8, Primordial 15; links 2<-1 3<-2 4<-1 5<-4 6<-5 7<-6; star 7 = Stone Form (`records/skills/devotion/tier3_05g_skill.dbr`)
- `constellation71.dbr` Spear of the Heavens (`tagDevotion_C06`): 6 stars; gives nothing; requires Primordial 20, Chaos 7; links 2<-1 3<-2 4<-3 5<-4 6<-5; star 6 = Spear of the Heavens (`records/skills/devotion/tier3_06f_skill.dbr`)
- `constellation72.dbr` Ulzuin's Torch (`tagDevotion_C07`): 7 stars; gives nothing; requires Chaos 8, Eldritch 15; links 2<-1 3<-2 4<-3 5<-4 6<-3 7<-6; star 7 = Meteor Shower (`records/skills/devotion/tier3_07g_skill.dbr`)
- `constellation73.dbr` Dying God (`tagDevotion_C08`): 7 stars; gives nothing; requires Chaos 8, Primordial 15; links 2<-1 3<-2 4<-3 5<-4 6<-5 7<-5; star 7 = Hungering Void (`records/skills/devotion/tier3_08g_skill.dbr`)
- `constellation74.dbr` Tree of Life (`tagDevotion_C09`): 6 stars; gives nothing; requires Primordial 20, Order 7; links 2<-1 3<-2 4<-2 5<-4 6<-4; star 6 = Healing Rain (`records/skills/devotion/tier3_09f_skill.dbr`)
- `constellation75.dbr` Mogdrogen the Wolf (`tagDevotion_C10`): 6 stars; gives nothing; requires Ascendant 15, Eldritch 12; links 2<-1 3<-2 4<-3 5<-4 6<-5; star 6 = Howl of Mogdrogen (`records/skills/devotion/tier3_10f_skill.dbr`)
- `constellation76.dbr` Blind Sage (`tagDevotion_C11`): 7 stars; gives nothing; requires Ascendant 10, Eldritch 18; links 2<-1 3<-2 4<-3 5<-3 6<-3 7<-6; star 7 = Elemental Seeker (`records/skills/devotion/tier3_11g_skill.dbr`)
- `constellation77.dbr` Leviathan (`tagDevotion_C12`): 7 stars; gives nothing; requires Eldritch 13, Ascendant 13; links 2<-1 3<-2 4<-3 5<-4 6<-4 7<-4; star 7 = Whirlpool (`records/skills/devotion/tier3_12g_skill.dbr`)
- `constellation78.dbr` Unknown Soldier (`tagDevotion_C13`): 7 stars; gives nothing; requires Ascendant 15, Order 8; links 2<-1 3<-2 4<-2 5<-4 6<-5 7<-6; star 7 = Living Shadow (`records/skills/devotion/tier3_13g_skill.dbr`)
- `constellation79.dbr` Harvestman's Scythe (`tagDevotion_B31`): 6 stars; gives Primordial +3, Ascendant +3; requires Ascendant 3, Primordial 5, Order 3; links 2<-1 3<-2 4<-3 5<-4 6<-5; no celestial power
- `constellation80.dbr` Crossroads (`tagDevotion_A15`): 1 stars; gives Chaos +1; requires nothing; links none; no celestial power
- `constellation81.dbr` Crossroads (`tagDevotion_A15`): 1 stars; gives Order +1; requires nothing; links none; no celestial power
- `constellation82.dbr` Crossroads (`tagDevotion_A15`): 1 stars; gives Eldritch +1; requires nothing; links none; no celestial power
- `constellation83.dbr` Crossroads (`tagDevotion_A15`): 1 stars; gives Ascendant +1; requires nothing; links none; no celestial power
- `constellation84.dbr` Scholar's Light (`tagDevotion_A36`): 3 stars; gives Eldritch +4; requires Eldritch 1; links 2<-1 3<-2; no celestial power
- `constellation85.dbr` Lion (`tagDevotion_A37`): 3 stars; gives Order +3; requires Order 1; links 2<-1 3<-2; no celestial power
- `constellation86.dbr` Jackal (`tagDevotion_A38`): 3 stars; gives Chaos +3; requires Chaos 1; links 2<-1 3<-2; no celestial power
- `constellation87.dbr` Crossroads (`tagDevotion_A15`): 0 stars; gives nothing; requires nothing; links none; no celestial power
