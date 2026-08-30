# The loot filter at the Game.dll level (static RE 2026-08-29, offline only)

All RVAs are Game.dll v1.3.0.8 unless prefixed `exe+` (the unpacked `build/GrimDawn.unpacked.bin`).
Names in `GAME::X::Y` form are **exported** unless a line says otherwise. "Verified" = read in the
disassembly; "inferred" = deduced from surrounding code, the records or the window's captions.
Companion: the exe-side window (its check boxes) is a separate document.

## 1. Storage

```
public: bool GAME::Player::GetLootFilter(enum GAME::LootFilterOption)          0x3ba430
public: void GAME::Player::SetLootFilter(enum GAME::LootFilterOption, bool)    0x3ba460
public: void GAME::Player::SetLootFilterDefaults(void)                         0x3ba490
```

**Player+0x4c00 = the live filter, Player+0x4c20 = the factory-default snapshot** (verified). Both are
the same 0x20-byte dynamic-bitset object, constructed side by side in `Player::Player` (0x3b82a6 ->
0x3d3f70, which builds the object at `this` and the one at `this+0x20`):

```
+0x00  u32* words      <- Get/SetLootFilter index this
+0x08  u32* end        \ mem::vector<u32> {begin,end,cap}
+0x10  u32* cap        /
+0x18  size_t numBits  = 42 (0x2a)
```
The ctor calls `resize(42, /*value=*/true)` (0x250cd0, verified: it is a bit-resize with a fill flag),
so every option starts **on** before the explicit ctor writes below. 42 bits = 2 dwords: bit i lives in
`words[i>>5]` bit `i&31`.

`GetLootFilter` / `SetLootFilter` are the raw bit ops with **no range check at all** (verified: sign-extend
the enum, shift, `bt`/`bts`/`btr`, ret). An index >= 64 writes past the allocation. The mod must clamp to
0..41 itself.

`SetLootFilterDefaults` copies bits 0..41 from **+0x4c20 into +0x4c00** (verified). +0x4c20 is written
only in the ctor, and `Player::LoadNewFormatData` writes only +0x4c00, so the defaults object always holds
the factory vector, whatever the save contained.

### The factory default vector (verified, read off `Player::Player` 0x3b87dd..0x3b89b4)
The ctor sets/clears in this order (which is also the enum order, see §2): `bts` 0,1,2,3,4,5,6, then
`bts` **39**, then `bts` 7, then `btr` **38**, then `bts` 8,9,0xa..0x10; then `btr` 0x12..0x1f (18..31)
and `btr` 32,33,34,35,36,37,40,41. Bit **17 is never touched**, so it keeps the `resize(42,true)` value.

```
ON  : 0..17 (every Quality box except Always Show Double Rare, every Type box) and 39
OFF : 18..37 (Damage + Character), 38 (Always Show Double Rare), 40, 41
```
This matches the live screenshot exactly (all Quality+Type on except Always Show Double Rare; all Damage
and Character off). Bit 39 has no check box in the window (41 boxes, 42 options) -- see §2.

## 2. LootFilterOption -- index -> meaning

Derived by reading `Item::PassLootFilter` (0x3134a0) in full; every bit test in it is listed below.
The order matches the window's four columns and, within Quality/Character, the two appended options
(38, 40, 41) sit where the window draws them.

Item virtuals used (slot -> exported name, from the `ItemEquipment` vtable 0x73e778; base `Item` folds
them all to `return false`):
`+0x4e8 IsDamageTypePresent(CombatAttributeType)`, `+0x4f0 IsRetaliationTypePresent`,
`+0x4f8 IsRetaliationPresent()`, `+0x500 IsDefenseTypePresent(CombatAttributeType)`,
`+0x508 IsCharacterAttributePresent(CharAttributeType)`, `+0x510 IsSkillAttributePresent(SkillAttributeType)`,
`+0x518 HasPetBonus()`, `+0x520 HasMastery(Character&, bool)`, `+0x550 GetItemType()`,
`+0x5b0 GetItemClassification(bool)`, `+0x658 HasAscendantBonus()`; on `Weapon`: `+0x690
IsTwoHandedMeleeWeapon()`, `+0x6b0 GetWeaponType()`.

### Quality (phase A) -- "the item's rarity must match one enabled box"
| # | window caption | test | default |
|---|---|---|---|
| 0 | Common | classification == 0 | on |
| 1 | Magic | classification == 1 | on |
| 2 | Rare | classification == 2 | on |
| 3 | Monster Infrequent | **base** classification == 2 (`GetItemClassification(false)`) | on |
| 4 | Epic | classification == 3 | on |
| 5 | Legendary | classification == 4 | on |
| 6 | Sets | the item is in a set (`ItemEquipment+0x1458 != 0`, the size word of the set-name `std::string` at +0x1448, cf. `ItemEquipment::GetItemSetName`) | on |
| 7 | Always Show Uniques | classification is 3 or 4 -> **returns true immediately** (skips Type and stat filters) | on |
| 38 | Always Show Double Rare | `Item+0x880 == 2 && Item+0x884 == 2` (prefix and suffix classification both Rare, `Set/GetPrefixClassification`, `Set/GetSuffixClassification`) -> **returns true immediately** | off |
| 39 | *(no check box seen)* | `HasAscendantBonus()` | **on** |

If none of 0..6 / 39 matched, `PassLootFilter` returns false right there.

`Item+0x878` = effective classification (`GetItemClassification(true)`), raised from the base by affixes
(`Item::SetItemClassification` only ever raises it); `Item+0x87c` = the classification of the base record,
snapshotted in `Item::Load` (0x310a2c: `[+0x87c] = [+0x878]` right after the record's own value is set).
So bit 3 means "the base record itself is Rare-classified" = a monster infrequent, while bit 2 means "this
particular item ended up Rare" (verified mechanism; the MI reading is inferred from the caption).

### Type (phase B) -- "the item's kind must match one enabled box"
`r13 = Weapon::GetWeaponType()` (0 when the item is not a `Weapon`), `eax = Item::GetItemType()`.
| # | window caption | test | default |
|---|---|---|---|
| 8 | 1h Melee | weapon type in {2 Axe, 3 Sword, 4 Mace, 7 Spear} | on |
| 9 | 2h Melee | `Weapon::IsTwoHandedMeleeWeapon()` | on |
| 10 | 1h Ranged | weapon type == 13 (Ranged1h) | on |
| 11 | 2h Ranged | weapon type == 8 (Ranged2h) | on |
| 12 | Dagger/Scepter | weapon type in {5 Dagger, 6 Scepter, 9 Staff} | on |
| 13 | Caster Off-Hand | weapon type == 12 (Offhand) | on |
| 14 | Shield | weapon type == 11 (Shield) | on |
| 15 | Armor | item type in {1 Head, 3 Chest, 4 Legs, 5 Feet, 7 Hands, 14 Shoulders} (mask 0x40ba) | on |
| 16 | Accessories | item type in {2 Amulet, 6 Ring, 13 Waist, 15 Medal} (mask 0xa044) | on |
| 17 | Components | classification == 8 (Relic) -- tested **before** everything else, so a component's own rarity/type/stat boxes never apply | on |

If none matched, `PassLootFilter` returns false.

`GetWeaponType()` values were read off each class's vtable constant (verified): 2 Axe, 3 Sword, 4 Mace,
5 Dagger, 6 Scepter, 7 Spear, 8 Ranged2h, 9 Staff, 11 Shield, 12 Offhand, 13 Ranged1h, 15 Axe2h,
16 Sword2h, 17 Mace2h, 18 Spear2h, 1 = the abstract bases. `GetItemType()` (`enum GAME::Item_Type`):
1 Head, 2 Amulet, 3 Chest/Clothing, 4 Legs, 5 Feet, 6 Ring, 7 Hands/Bracelet, 8 WeaponArmor (shield/offhand
base), 9 Weapon, 11 Artifact, 12 Formula, 13 Waist, 14 Shoulders, 15 Medal, 16 Relic/Charm, 17 OneShot,
18 Sack, 19 QuestItem, 20 Note, 21 Enchantment, 22 Transmuter, 23 FactionWarrant/Booster, 24 DevotionReset,
25 DifficultyUnlock, 26 AttributeReset, 0 = the abstract bases.

### Damage + Character (phase C) -- "if any of these is on, the item must have one of them"
`CombatAttributeType` values from the `*::GetType` constants (verified): 2 Physical, 3 PierceRatio,
4 Pierce, 5 Cold, 6 Fire, 7 Poison, 8 Lightning, 9 Life, 10 Chaos, 11 Aether, 15 Bleeding,
0x3a Elemental, 0x3b CritDamageModifier, 0x3c TotalDamageModifier / AllResistance.
`CharAttributeType` (from the `CharAttributeVal_*` ctors): 4 Life, 6 LifeRegen, 13 TotalSpeed, 14 RunSpeed,
15 AttackSpeed, 16 SpellCastSpeed, 21/22 OffensiveAbility(+Modifier), 23/24 DefensiveAbility(+Modifier).
`SkillAttributeType` 1 = CooldownReduction.

| # | window caption | test | default |
|---|---|---|---|
| 18 | Physical | damage 2 or 0x3c | off |
| 19 | Pierce | damage 4, 3 or 0x3c | off |
| 20 | Fire | damage 6, 0x3a or 0x3c | off |
| 21 | Cold | damage 5, 0x3a or 0x3c | off |
| 22 | Lightning | damage 8, 0x3a or 0x3c | off |
| 23 | Acid | damage 7 (Poison) or 0x3c | off |
| 24 | Vitality | damage 9 (Life) or 0x3c | off |
| 25 | Aether | damage 0xb or 0x3c | off |
| 26 | Chaos | damage 0xa or 0x3c | off |
| 27 | Bleed | damage 0xf or 0x3c | off |
| 28 | Pet Bonuses | `HasPetBonus()` | off |
| 29 | My Masteries | `HasMastery(mainPlayer, true)` | off |
| 30 | Other Masteries | `HasMastery(mainPlayer, false)` | off |
| 31 | Speed | char attribute 0xd, 0xe, 0xf or 0x10 (total / run / attack / cast speed) | off |
| 32 | Cooldown Reduction | `IsSkillAttributePresent(1)` | off |
| 33 | Crit Damage | damage 0x3b (CritDamageModifier) | off |
| 34 | Offensive Ability | char attribute 0x15 or 0x16 | off |
| 35 | Defensive Ability | char attribute 0x17 or 0x18 | off |
| 36 | Resistances | `IsDefenseTypePresent` of any of {2,4,5,6,7,8,9,0xa,0xb,0xe,0x2a,0x2b,0x2c,0x2d,0x2e,0x2f,0x39,0x10,0xf,0x3a} (the damage resistances plus stun/sleep/trap/freeze/petrify/reflect/slow) | off |
| 37 | Retaliation | `IsRetaliationPresent()` | off |
| 40 | Health | char attribute 4 (Life) | off |
| 41 | Health Regeneration | char attribute 6 (LifeRegen) | off |

**Semantics (verified):** the three phases are ANDed; within a phase the boxes are ORed. Phase C is
skipped when **no** Damage/Character box is on -- the tail is `if (anyPhaseCBoxEnabled) result = matched;
else result = true` -- so an all-off Damage/Character column means "no stat filtering", exactly as the
coordinator guessed. Phase C also short-circuits: once one predicate matched, the remaining ones are not
evaluated.

The `HasMastery` bool = "my masteries" (true) / "other masteries" (false) is inferred from the captions
and the call order (bit 29 passes 1, bit 30 passes 0); the body is a thunk into a sub-object at
`ItemEquipment+0x1468` (vtable +0x20) and was not followed.

### Classifications outside the filter (verified in `PassLootFilter`)
`GameEngine::ResolveItemClassificationEnum` (0x2d4b00) maps the record string to
`enum GAME::ItemClassification`: 0 Common, 1 Magical, 2 Rare, 3 Epic, 4 Legendary, 5 Broken, 6 Potion,
8 Relic, 9 Quest, 10 Artifact, 11 ArtifactFormula, 13 Lore (7, 12, 14, 15 are not produced by this
function). In `PassLootFilter`:
- classification 5 (Broken) -> **never** shown, before anything else.
- 6 and 7 -> **always** shown (potions).
- 8 (Relic) -> option 17 alone decides.
- 9..15 (quest, artifact, formula, lore, ...) -> **always** shown.
- 0..4 and >= 16 -> the three phases above.

### `InGameUIActorCapture::ItemIgnore` (the argument) -- verified
```
0 = apply the filter normally
1 = ignore the filter (everything passes)
2 = everything except classification 0 (Common)
3 = only classification 9 (Quest) and 13 (Lore)
```

## 3. Persistence -- per character, in the save

`Player::SaveNewFormatData` (0x3c7915) writes `[Player+0x4c18]` (the bitset's `numBits`) and then one
obfuscated byte per bit, capped at 42; `Player::LoadNewFormatData` (0x3c53ef) reads the count back and
sets/clears each bit, skipping indices >= 42 (both verified). So **the loot filter is character data in
`player.gdc`**, not a global setting, and a save from an older build with fewer options simply leaves the
newer bits at their ctor value.

The only other readers of `Player+0x4c00` in Game.dll are `Item::PassLootFilter`, `Get/SetLootFilter`,
`SetLootFilterDefaults` and the ctor (exhaustive: a disp32 scan of the whole image). The filter is read
live on every call -- writing a bit takes effect on the next frame with no notification needed.

## 4. Everything else worth knowing

- **Who calls `PassLootFilter`.** Game.dll: only `Item::AppendDetailMapData` (0x314066, mode 0) -- a
  filtered item is not on the detail map. The exe: `exe+0x28be83` (the actor-capture pass that decides
  the floating label and calls `Item::ToggleLootBeam`) and `exe+0x21ca67`, inside the **Pickup key**
  function (`exe+0x21c6c0`). So the game's own Pickup action (our **G**, `InGameUI::HandleKeyAction(0x37)`)
  **skips items the filter hides**.
- **Nothing in the pickup command itself filters**: `ControllerPlayer::ItemAction` /
  `ControllerCharacter::PickupItem(id)` never consult it (they are not among the callers). Our **J on a
  reviewed Item** (docs/re_pickup.md) therefore picks up a filtered-out item fine, and the mod's review /
  sonar groups (`Item::IsOfInterest`) ignore the filter entirely -- we currently surface loot a sighted
  player's filter would hide. That is a product decision, not a bug.
- **Where the ItemIgnore value comes from** (exe layer, dies on an exe relink): helper `exe+0x20f70(this)`
  returns it --
  ```
  if (!gGameEngine->GetMainPlayer())          return 1;
  bool opt   = gEngine->GetOptions()->GetBool(0x11);      // the game's loot-filter option
  auto ui    = this->[0x2f0];
  int  mode  = ui->[0x72f5] ? 0 : 3;                      // 0x72f5 = "items shown" flag
  if (!opt)                                   mode = 1;
  auto cap   = this->[0x110];
  if (cap->[0x12a])                           return 2;   // "Show Items (Filter Common)"  (Z)
  if (cap->[0x129])                           mode = 1;   // "Show Items (No Filter)"      (Alt)
  return mode;
  ```
  The two modifier bytes are set by the capture's key handler `exe+0x28ac00`: action **0x23 -> +0x129**
  (Alt, no filter), **0x24 -> +0x128** (Show Item Tooltips, X), **0x25 -> +0x12a** (Z, filter common);
  each holds 1 while the key is down.
- **"Toggle Hide All Items (Loot Filter)"** (unbound by default, docs/controls.md line 45) is
  `exe+0x2114ba`: it flips the byte at `+0x72f5` of the object at `[capture+0x2f0]`, mirrors it into
  `+0x8bc1/+0x8bc2/+0x8bc4`, and announces `tagLootFilterToggleOn` / `tagLootFilterToggleOff`. With the
  byte 0 the capture passes ItemIgnore 3 -> only quest and lore items keep their labels, beams and
  pickup-key eligibility. There is **no Game.dll state** behind it; a mod key for it has to poke the exe
  byte or send the game's action.
- **Related exports**: `GameEngine::GetItemHighlightColor(ItemClassification)` (0x2d5100 -- the
  rarity colour), `Item::ToggleLootBeam`, `Item::CannotPickUpMultiple`, `Item::GetDropClassification`,
  `Item::GetPrefixClassification` / `GetSuffixClassification`, `ItemEquipment::GetItemSetName`.
  A grep of the export list for "LootFilter"/"ItemIgnore" finds nothing beyond the four functions in §1 --
  in particular there is **no exported per-option label**; captions must come from the window.
- **A mod surface** would be:
  ```
  read   : Player::GetLootFilter(player, i)          i in 0..41
  write  : Player::SetLootFilter(player, i, on)      takes effect immediately, saved with the character
  reset  : Player::SetLootFilterDefaults(player)     always the factory vector (§1)
  test   : Item::PassLootFilter(item, 0)             "would the sighted player see this item"
  ```
  with our own caption table (§2) since the game exports none.

## 5. Unverified / open
- The captions for indices 12 ("Dagger/Scepter" also matches Staff, weapon type 9) and 39 (no check box
  seen in the window; default on, `HasAscendantBonus`) -- the window agent's tag list decides.
- `HasMastery`'s bool (my / other masteries) -- inferred, see §2.
- Item types 7 (Hands and Bracelet share the value) and classifications 7, 12, 14, 15 were not resolved.
- Nothing here has been exercised in a live game.
