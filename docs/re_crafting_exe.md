# The Crafting (Blacksmith / "Forgemaster") window: the exe side (Grim Dawn v1.3.0.8 x64, exe timestamp 0x6a85fbec, image 0x482000)

Static RE on `build/GrimDawn.unpacked.bin` with `tools/exe_dis.py` / `tools/dll_dis.py` / `tools/arz.py` /
`tools/arc_unpack.py`, 2026-08-29. **Nothing here has been exercised live by this pass**; a set of live
screenshots and three live anchors (window offset, vtable, visible byte) were supplied and every one of them
agrees with the layout derived below. All addresses are RVAs (`exe+...`, `Game.dll+...`); "verified" = read in
disassembly, everything else is marked **inferred**. Companion to `docs/exe-ui-layout.md` (frameworks A/B),
`docs/ingame-ui-survey.md` (whose "Stations ... crafting `+0x3aa80`" entry this supersedes),
`docs/re_lootfilter_exe.md` and `docs/re_devotion_exe.md` (the format this follows).

Anchors: crafting window = `InGameUI + 0x3aa80`, ctor exe+0x269e80, primary vtable exe+0x31cf38, size **0x51a0**,
visible byte `+0x68`. `InGameUI = [[main_obj+0x90]+0x2f0]`, `main_obj = [exe+0x3ceef8]`.

**Headline**: the window is a thin frame (title, subtitle, five tab buttons, a close button) around one big
by-value sub-object, the **crafting panel** at `window+0x1e40` (size 0x3360 -- it runs to the end of the
window), which owns the search box, the recipe list, the eight item slots, the cost line and Combine.
Everything the panel shows is recomputed **every frame in `Update`** from exported `ItemArtifactFormula` /
`Character` calls; the panel caches almost nothing. The selected recipe is one dword, `panel+0x60` = the
`ItemArtifactFormula`'s **object id**, and it is re-read from the list box every frame. Crafting itself is one
call, `ControllerCharacter::SendCreateArtifactCmd(CreateArtifactConfigInfo const&)`, with a ~0x330-byte struct
the window assembles entirely out of exported formula getters.

---

## 1. The window class

- `CraftingWindow` -- `InGameUI+0x3aa80`, size **0x51a0**, ctor exe+0x269e80, primary vtable exe+0x31cf38.
  Secondary vtables written by the ctor: `+0x08` -> exe+0x31d038 (slot0 exe+0x26ae40),
  `+0x90` -> exe+0x31d030 (slot0 **exe+0x26af70 = OnControlEvent**).
  Deleting destructor exe+0x26a4b0 (`mov edx, 0x51a0` gives the size); the base dtor exe+0x26a4f0 reverts the
  vptr to the framework-B base class exe+0x31cb90.
- The window's record is **`records/ui/inventor/inventor_mastertable.dbr`** -- the SAME record the **enchanter**
  window (`InGameUI+0x30dd8`, ctor exe+0x26bd00) uses. It has no `hud*Window` field of its own in
  `hud_mastertable.dbr`; the crafting window is built from the `crafting*` / `enchanter*` fields of the
  inventor mastertable (verified: `SetRecord` fetches `enchanterBaseBitmap`, `craftingTab1Button`, ... by
  name). Window default extent 346 x 715, aligned Left / Center -- which is why it sits at the left of the
  screen.

### 1.1 Vtable slots used (exe+0x31cf38, 31 slots 0x00..0xf0), verified

| slot | RVA | what |
|---|---|---|
| `+0x10` | exe+0x26a4b0 | deleting destructor (size 0x51a0) |
| `+0x18` | exe+0x26b080 | **SetRecord(std::string const&)** -- builds every control (section 1.2) |
| `+0x20` | exe+0x26a700 | Render |
| `+0x30` | exe+0x26bbe0 | -> panel `vt[0x90]` |
| `+0x38` | exe+0x26aa40 | HandleMouseEvent |
| `+0x48` | exe+0x26ad50 | Update (forwards to the panel) |
| `+0x58` | exe+0x32340 | HandleKeyEvent (the shared `return false` stub) |
| `+0x68` | exe+0x26af20 | **Escape** (section 5.3) |
| `+0xb0` | exe+0x26ba30 | **Show(bool)** (section 5.1) |
| `+0xb8` | exe+0x10d5f0 | IsVisible (the generic one: returns `+0x68`) |
| `+0xe8` | exe+0x26ba00 | **SetPlayerId(uint)** = `mov [rcx+0x98], edx` then panel `vt[0x80](id)` |
| `+0xf0` | exe+0x26bc00 | **SetCrafter(uint npcId)** -- stores `+0x9c` after an `NpcCrafter` RTTI check |

Note the deviation from the framework-B convention: `+0xf0` here is **not** OnControlEvent (as on most
windows) but `SetCrafter`. `OnControlEvent` is only reachable through the listener sub-object at `window+0x90`
(vtable exe+0x31d030, slot 0 = exe+0x26af70).

### 1.2 Window member offsets (verified from the ctor exe+0x269e80 and SetRecord exe+0x26b080)

Framework-B base: `+0x28` base-control visible byte, `+0x30` host pointer, `+0x40..+0x4c` float rect,
`+0x50/+0x54` the record's `windowDefaultX/Y`, **`+0x68` = the window's own visible byte**, `+0x69`
mouse-inside latch, **`+0x90` = the listener sub-object**.

| offset | class / record | what |
|---|---|---|
| `+0x98` | dword | **the player object id** (written by `InGameUI` through `vt[0xe8]`; forwarded to the panel) |
| **`+0x9c`** | dword | **the crafter NPC's object id** (written by `vt[0xf0]`, RTTI-checked against `NpcCrafter::classInfo`) |
| `+0xa0` | Image (vt exe+0x313860, 0x60) | `enchanterBaseBitmap` -- window background |
| `+0x100` | Image | `enchanterArtifactTabBitmap` -- the panel background |
| `+0x160` | Image | `enchanterHeadingBar` (field absent from the record -> empty) |
| `+0x1c0` | text element (ctor exe+0x2595c0, vt exe+0x31c4d0, 0xf8) | `enchanterNameText` = **the NPC's name** ("Angrim"); u16 caption at element `+0x40` -> `window+0x200` |
| `+0x2b8` | text element | `crafterHeadingText` = **the subtitle** ("Forgemaster"); u16 at `window+0x2f8` |
| `+0x3b0` | rollover helper (ctor exe+0x2310f0, 0xf0) | `enchanterHeadingRollover` |
| **`+0x4a0`** | registry, vtable **exe+0x312c68** | **the tab registry** -- a RADIO registry (`PressChild` = exe+0x12b330, section 6.1) |
| **`+0x4e0`** | Button (ctor exe+0x10aee0, 0x338) | `craftingTabArtifactButton` -> category **6** |
| **`+0x818`** | Button | `craftingTab1Button` -> category **2** |
| **`+0xb50`** | Button | `craftingTab2Button` -> category **3** |
| **`+0xe88`** | Button | `craftingTab3Button` -> category **1** |
| **`+0x11c0`** | Button | `craftingTab4Button` -> category **4** |
| `+0x14f8` | rollover (0xf0) | `craftingTabArtifactButtonRollover` |
| `+0x15e8 / +0x16d8 / +0x17c8 / +0x18b8` | rollovers | `craftingTab1..4ButtonRollover` |
| **`+0x19a8`** | int | **the current category** (6 on construction) |
| `+0x19b0/+0x19b8/+0x19c0` | `std::vector<int>` | the five categories **in on-screen tab order: 6, 2, 3, 1, 4** |
| `+0x19c8` | registry, vtable exe+0x312cf0 | registry A (full 0/1/2 click); holds only the close button |
| `+0x1a10` | Button | `enchanterCloseButton` |
| `+0x1d48` | text element (vt exe+0x31c2b0) | `enchanterWindowTitle` (field absent -> empty) |
| **`+0x1e40`** | **CraftingPanel**, 0x3360 | `enchanterArtifactTab` = `records/ui/inventor/craftingpanel/crafting_table.dbr` -- everything else |

The tab Buttons carry the usual Button state bytes at `+0x281` disabled / `+0x282` pressed, so
`window+0x762 / +0xa9a / +0xdd2 / +0x110a / +0x1442` are the five tabs' pressed bytes.

### 1.3 The five tabs

Order on screen = the ctor's vector order = the record's bitmap positions. Captions are icon-only; the names
below are the rollover records' `Line1Tag` / `Line2Tag` (`tools/arz.py`, `tools/arc_unpack.py`), verified:

| # | control | category | rollover record | title tag | English | description tag |
|---|---|---|---|---|---|---|
| 1 | `+0x4e0` | **6** | `crafting_buttonitempanelartifactrollover` | `tagCraftTabArtifactA` | **Relics** | `tagCraftTabArtifactB` = "Talismans, relics and artifacts of great power." |
| 2 | `+0x818` | **2** | `crafting_buttonitempanel01rollover` | `tagCraftTabMeleeA` | **Melee Weapons** | `tagCraftTabMeleeB` |
| 3 | `+0xb50` | **3** | `crafting_buttonitempanel02rollover` | `tagCraftTabRangedA` | **Ranged Weapons** | `tagCraftTabRangedB` |
| 4 | `+0xe88` | **1** | `crafting_buttonitempanel03rollover` | `tagCraftTabArmorA` | **Armor** | `tagCraftTabArmorB` |
| 5 | `+0x11c0` | **4** | `crafting_buttonitempanel04rollover` | `tagCraftTabMiscA` | **Accessories and Consumables** | `tagCraftTabMiscB` |

That is exactly the live order (relics / melee / ranged / armor / accessories+components).

---

## 2. The crafting panel (`window+0x1e40`)

`CraftingPanel` -- size **0x3360**, ctor exe+0x196220, primary vtable exe+0x316140 (19 slots, 0x00..0x90),
secondary vtable `+0x08` -> exe+0x3161d8, **listener sub-object at `+0x30`** (vtable exe+0x316138, slot 0 =
**exe+0x19c3d0 = OnControlEvent**), a second stub listener at `+0x38` (exe+0x317748). Deleting destructor
exe+0x196870 (`mov edx, 0x3360`).

It is **not** a framework-B window: its vtable has no `+0xb0 Show` / `+0xb8 IsVisible`. Its own slots:

| slot | RVA | what |
|---|---|---|
| `+0x10` | exe+0x196870 | deleting destructor |
| `+0x18` | exe+0x19c430 | SetRecord |
| `+0x20` | exe+0x1971f0 | Render |
| `+0x38` | exe+0x197500 | HandleMouseEvent (section 3.4) |
| `+0x48` | exe+0x19b680 | **Update** (section 4) |
| `+0x58` | exe+0x32340 | HandleKeyEvent stub |
| `+0x68` | exe+0x19c200 | Escape |
| `+0x80` | exe+0x19d850 | **SetPlayerId(uint)** (`mov [rcx+0x54], edx`) |
| `+0x88` | exe+0x19e120 | **Show(bool)** (writes `+0x50`) |
| `+0x90` | exe+0x19ecf0 | (reached from window `vt[0x30]`) |

### 2.1 Panel member offsets (verified from the ctor exe+0x196220 and SetRecord exe+0x19c430)

All offsets are **panel-relative**; add 0x1e40 for a window offset, 0x3c8c0 for an `InGameUI` offset.

| offset | class / record | what |
|---|---|---|
| `+0x30` | listener sub-object | `vt[0]` = OnControlEvent exe+0x19c3d0 |
| `+0x40` | ptr | host / parent |
| **`+0x50`** | byte | **the panel's visible byte** (written by `Show`) |
| **`+0x54`** | dword | **the player object id** |
| **`+0x58`** | dword | **the crafter NPC's object id** |
| `+0x5c` | dword | the formula id the reagent panel was last filled for |
| **`+0x60`** | dword | **THE SELECTED RECIPE** = the `ItemArtifactFormula`'s object id (0 = none) |
| `+0x68` | edit box (ctor exe+0x192dd0) | `searchBox`; its u16 text is the field at `+0xd8` |
| `+0x238` | Button (ctor exe+0x10aee0) | `clearSearchButton` |
| `+0x570` | slot control (ctor exe+0x1ad7b0, vt exe+0x3162a8) | **no record is ever assigned to it** -- hit-tested but invisible (**inferred**: the legacy `artifactFormulaBox` blueprint slot; that record exists in `crafting_table.dbr` and is referenced nowhere in the exe) |
| **`+0x790`** | item slot (ctor exe+0x110740, **0x2f0** bytes) | `reagentBaseBox` = the LEFT big slot (primary reagent) |
| **`+0xa80` / `+0xd70` / `+0x1060` / `+0x1350` / `+0x1640` / `+0x1930`** | item slots | `reagent1Box .. reagent6Box` = the 2x3 grid |
| **`+0x1c20`** | slot (ctor exe+0x1ad7b0, vt exe+0x3161e8) | `artifactBox` = the RIGHT big slot (the result) |
| `+0x1e60` | registry, vtable exe+0x312cf0 | registry A; holds `createArtifactButton` and `clearSearchButton`, listener = `panel+0x30` |
| **`+0x1ea8`** | TextButton (ctor exe+0x126fe0, 0x3b0) | **`createArtifactButton` = Combine** (`textTag = tagCraftingCombine`); disabled byte **`panel+0x2129`**, pressed `+0x212a`, caption u16 `+0x2200` |
| `+0x2258` | Image | `crafterIcon` (from `NpcCrafter::GetCrafterBitmapName`) |
| **`+0x22b8`** | text element (0xf8) | `reagentBaseQuantity` -- the "0/2" under the base slot |
| **`+0x23b0` / `+0x24a8` / `+0x25a0` / `+0x2698` / `+0x2790` / `+0x2888`** | text elements | `reagent1..6Quantity` |
| **`+0x2980`** | text element | `costNumber` (the affordable style) |
| **`+0x2a78`** | text element | `tooExpensiveCostNumber` (the red style) |
| `+0x2b70` | text element (vt exe+0x31c2b0) | `costText` = the literal "Cost:" label (`tagCraftingCost`) |
| `+0x2c68` | text element (vt exe+0x316368) | `displayHelpText` (`tagCraftingDisplayBoxHelp`) |
| `+0x2d80` | text element (vt exe+0x31c2b0) | no record assigned |
| **`+0x2e78`** | text element (vt exe+0x31c4d0) | **`artifactNameText`** = the selected recipe's coloured name |
| `+0x2f70` | byte | cleared by `Show(false)` |
| **`+0x2f71`** | byte | "a formula is selected" (`selectedFormula != null`) |
| **`+0x2f72`** | byte | **"cannot afford"** (`Character::GetCurrentMoney() < cost`) |
| **`+0x2f73`** | byte | "rebuild the recipe tree" |
| **`+0x2f74`** | byte | "refresh the row labels" (inventory or money changed) |
| `+0x2f75` | byte | busy / a create is in flight (blocks Combine) |
| `+0x2f76` | byte | "the search text changed" |
| `+0x2f77` | byte | "the search box is non-empty" |
| `+0x2f78` | `basic_string<unsigned short>` | the lower-cased search query |
| `+0x2f98` | ptr | the `createdArtifactSound` SoundPak object (`DestroyObjectEx`d in the dtor) |
| **`+0x2fa0`** | **ListBox** (ctor exe+0x1f94e0) | `craftingListBox` -- the recipe tree (section 3.3). Rows vector at listbox `+0xd0/+0xd8` = **`panel+0x3070/+0x3078`**; the scroll bar at listbox `+0x30` = `panel+0x2fd0` |
| `+0x30f0` | `std::string` | `craftingListBoxItem` record path |
| `+0x3110` | `std::string` | `craftingListBoxHeaderFont` |
| **`+0x3130`** | int | the current **category** (mirror of `window+0x19a8`) |
| **`+0x3134`** | int | the formula count the tree was built from (**-1 = force a rebuild**) |
| `+0x3168` | container | `craftingDefaultRecipes` (the record's 87-entry list of always-known blueprints) |
| `+0x3178` | container | the crafter's own recipes (`NpcCrafter::GetRecipes`) |
| **`+0x3188`** | `std::map<..., vector<GroupDesc, 0x58>>` | **the group table** built once by exe+0x198500 (section 3.1) |
| **`+0x3198`** | `std::map<int category, vector<Entry, 0x50>>` | **the formulas bucketed by category** (section 3.2) |
| `+0x31b0` | rollover | `crafterRollover` (filled from `NpcCrafter::GetEnhancementTags`) |
| `+0x31e8` | rollover | the Combine button's rollover, `tagCraftingButtonInfo` |
| `+0x3220` | rollover | the search box's rollover, `tagCraftingSearchBoxInfo` |
| `+0x3238` | `std::string` | `defaultTextStyle` |
| `+0x3258` | ptr | the control the mouse is currently over (fed to the shared tooltip helper exe+0x2371e0) |
| `+0x3260` | item-preview / rollover holder (ctor exe+0x1eda10) | given the RESULT item's id each frame; **inferred**: this is what makes the result slot's hover tooltip |
| `+0x3340/+0x3348` | | a cached copy of `Character::GetInventoryItems()` |
| `+0x3358` | int | a cached copy of `Character::GetCurrentMoney()` |
| **`+0x335c`** | byte | `NpcCrafter::RestrictsRecipes()` |
| **`+0x335d`** | byte | `NpcCrafter::LoadAwakenedRecipes()` |

### 2.2 The item-slot control (0x2f0 bytes, ctor exe+0x110740) -- verified in use

The seven reagent slots are all this class; the result slot (`+0x1c20`) and `+0x570` are the exe+0x1ad7b0
class, which uses the same offsets for everything the panel touches.

| offset (slot-relative) | what |
|---|---|
| `+0x5c` | hovered byte (cleared at the top of the panel's HandleMouseEvent) |
| **`+0x61`** | **"insufficient" flag** -- 1 when you have fewer than the recipe needs (the red quantity) |
| `+0x220 / +0x221` | two bytes set alongside the name |
| **`+0x228`** | `basic_string<unsigned short>` = the reagent's **display name** |
| **`+0x2e8`** | int = **the required quantity** |
| vt `+0x48` | per-frame update |
| vt `+0xa0` | **"this slot is satisfied"** -- the predicate the Combine gate ANDs over all seven |
| vt `+0xa8` | **SetItem(objectId)** (0 = clear) |
| vt `+0xb0` | **SetQuantity(int)** |

So the reagent-1 slot's insufficient byte is `panel+0xae1`, its name `panel+0xca8`, its need `panel+0xd68`;
the base slot's are `panel+0x7f1`, `panel+0x9b8`, `panel+0xa78`.

---

## 3. The recipe tree

### 3.1 The group table -- `exe+0x198500 = CraftingPanel::BuildGroups(panel)` (called at the end of the ctor), verified

Builds a vector of **26 `GroupDesc`** records, stride **0x58**:

| offset | what |
|---|---|
| `+0x00` | `basic_string<unsigned short>` = **the localized group header** (`LocalizationManager::LocalizeWithoutParams(tag)`) |
| `+0x20/+0x28/+0x30` | `std::vector<int>` #1 -- the artifact/relic classes this group accepts |
| `+0x38/+0x40/+0x48` | `std::vector<int>` #2 -- the item/weapon types this group accepts |
| `+0x50` | int tier (0 except the relic tiers: 0/1/2) |
| **`+0x54`** | byte = **EXPANDED** (the collapsible header's state; cleared by `Show(false)`, toggled by a click) |

The 26 groups in construction order -- which is exactly the on-screen order within a tab (verified against the
live screenshots' header order). Tab assignment is by which formulas land in the group, **inferred** from the
live tabs:

| # | tag | English | tab (inferred) |
|---|---|---|---|
| 1 | `tagCraftingRelicsTier01` | EMPOWERED RELICS | Relics |
| 2 | `tagCraftingRelicsTier02` | TRANSCENDENT RELICS | Relics |
| 3 | `tagCraftingRelicsTier03` | MYTHICAL RELICS | Relics |
| 4 | `tagCraftingRunes` | *(not in this install's `Text_EN.arc` -- expansion text)* | Relics |
| 5 | `tagCraftingAxes` | AXES | Melee |
| 6 | `tagCraftingBlunt` | BLUNT | Melee |
| 7 | `tagCraftingSwords` | SWORDS | Melee |
| 8 | `tagCraftingImplements` | IMPLEMENTS | Melee |
| 9 | `tagCraftingTwoHandedMelee` | TWO-HANDED MELEE | Melee |
| 10 | `tagCraftingSpears` | SPEARS | Melee |
| 11 | `tagCraftingOneHanded` | ONE-HANDED RANGED | Ranged |
| 12 | `tagCraftingTwoHanded` | TWO-HANDED RANGED | Ranged |
| 13 | `tagCraftingStaves` | STAVES | Ranged |
| 14 | `tagCraftingBelts` | BELTS | Armor |
| 15 | `tagCraftingHelms` | HELMS | Armor |
| 16 | `tagCraftingTorso` | CHEST | Armor |
| 17 | `tagCraftingShoulders` | SHOULDERS | Armor |
| 18 | `tagCraftingLegs` | LEGS | Armor |
| 19 | `tagCraftingHands` | HANDS | Armor |
| 20 | `tagCraftingFeet` | FEET | Armor |
| 21 | `tagCraftingOffhands` | FOCI | Armor |
| 22 | `tagCraftingShields` | SHIELDS | Armor |
| 23 | `tagCraftingComponents` | COMPONENTS | Accessories |
| 24 | `tagCraftingAccessories` | ACCESSORIES | Accessories |
| 25 | `tagCraftingConsumables` | CONSUMABLES | Accessories |
| 26 | `tagCraftingMaterials` | MATERIALS | Accessories |

### 3.2 Where the formulas come from -- `exe+0x19a060 = CraftingPanel::SetCategory(panel, int category)`, verified

Called by `OnControlEvent` on a tab press and by `Update` whenever `panel+0x2f73` is set. It rebuilds only when
`category != panel+0x3130`, the formula count differs from `panel+0x3134`, or `panel+0x2f73` is set.

```
formulas = GameEngine::GetPlayerFormulas()            // mem::map<uint,uint>, exported
clear the list box (exe+0x1f9980, exe+0x1f99f0) and the bucket map (panel+0x3198)
AddFormulas(panel, panel+0x3178)                       //   ALWAYS: NpcCrafter::GetRecipes()
if (panel->+0x335d /*LoadAwakenedRecipes*/)
    AddFormulas(panel, GameEngine::GetAwakenedRecipes())
if (!panel->+0x335c /*RestrictsRecipes*/) {
    AddFormulas(panel, formulas)                       //   the player's learned blueprints
    AddFormulas(panel, panel+0x3168)                   //   craftingDefaultRecipes from the record
}
sort each bucket (exe+0x1a0d80), PopulateRows(panel, category), relayout, RefreshRowLabels
panel->+0x3130 = category;  panel->+0x3134 = <formula count>;  panel->+0x2f73 = 0
```

So the answer to "`NpcCrafter::GetRecipes` or `GameEngine::GetPlayerFormulas`" is **both**, plus the record's
default list -- and a crafter with `RestrictsRecipes() == true` shows **only** its own list.

**Classification** (`exe+0x1980fa..0x198159`, inside `AddFormulas` exe+0x197c50, verified): for each formula it
takes the RESULT item (`ItemArtifactFormula::GetArtifact` / `GetArtifactInfo`) and computes

```
if (Weapon* w = dynamic_cast<Weapon*>(item))            weaponType = w->vt[0x6b0]()   // Weapon::GetWeaponType
if (ItemArtifact* a = dynamic_cast<ItemArtifact*>(item)) relicClass = ItemArtifact::GetArtifactClass(a)
category = item->vt[0x568]()                                                          // an item-class/type enum
if (dynamic_cast<ItemEnchantment*>(item)) category = 6
bucket = panel->map(+0x3198)[category]
```

and then matches the formula against each `GroupDesc`'s two int vectors to pick its group.
**Open**: `category = 6` for any `ItemEnchantment` result does not obviously reconcile with COMPONENTS
appearing under the Accessories tab live (components are `ItemRelic` records). Not resolved; the mod should
read the current category from `window+0x19a8` rather than recompute it.

### 3.3 The list box and its rows

`craftingListBox` = `panel+0x2fa0` (ctor exe+0x1f94e0, record `crafting_scrollbox_scrollwindow.dbr`). Rows
live in a `std::vector<Row>` at **listbox `+0xd0` / `+0xd8`** (= `panel+0x3070/+0x3078`), stride **0xd0**.
Group headers and recipes are rows of the same type.

Row fields established (verified):

| offset | what |
|---|---|
| **`+0x00`** | `basic_string<unsigned short>` = **the row's display text** (what `RefreshRowLabels` writes) |
| `+0x28` | int = the row's key (a header and its children share it) |
| `+0x4c` | int row height |
| `+0x50` | int (set to 1 when a row is selected and it was 0) |
| **`+0x58`** | byte = **SELECTED** |
| `+0x59` | byte (scanned by exe+0x1f9e60) |
| **`+0x5d`** | byte = the group's **expanded** flag (copied from `GroupDesc+0x54`) |
| `+0x5e` | byte = **the "new / unread blueprint" badge** (`formulaUnreadIcon`); cleared on click |
| `+0x60` | byte = "the display text has been set" |
| **`+0x64`** | int = **the row's DATA = the `ItemArtifactFormula`'s object id** (0 on a group header) |
| `+0x68` | `std::string` = a per-row record path (`craftingListBoxItem`), set by exe+0x1fa580 |

List-box helpers a mod can call (all verified):

- **`exe+0x1f9dd0(listbox) -> int`** -- index of the row with `+0x58` set, else -1.
- **`exe+0x1f9fd0(listbox, Row* outCopy) -> int`** -- the selected row's `+0x64` (the formula id), and a full
  0xd0-byte copy of the row into `outCopy` (destroy it with exe+0x155140). This is what `Update` uses.
- **`exe+0x1f9f00(listbox, int data) -> bool`** -- find the row whose `+0x64 == data`, set `+0x58 = 1`
  (and `+0x50 = 1` if it was 0). **This is "select a recipe programmatically".**
- **`exe+0x1faa60(listbox, vector<Row>* out) -> vector<Row>*`** -- a by-value copy of the whole row vector.
- `exe+0x1f9b20(listbox, ..., int, bool isRecipe, bool isHeader) -> int rowKey` -- AddRow, used by
  `PopulateRows` (exe+0x1999b0) with `(header=1, recipe=0)` for a group header and `(header=0, recipe=1)` for a
  recipe. Its full argument list was not decoded (**partially resolved**).

**Row text** -- `exe+0x19a3f0 = CraftingPanel::RefreshRowLabels(panel)` (verified), run whenever
`panel+0x2f74` is set (i.e. inventory or money changed) and after every tree rebuild. Per recipe row:

```
formula = ObjectManager::GetObject(row.+0x64)                    // is-a ItemArtifactFormula
n     = formula->vt[0x690](character)  // == ItemArtifactFormula::GetMaximumCraftable(Character const*)
color = GameEngine::GetItemColorText(Item::GetDropClassification(resultItem))
name  = resultItem->vt[0x340](...)     // == Item::GetGameDescription
row.text = color + "[" + <n, thousands-separated> + "] " + name;   row.+0x60 = 1
```

So the **`[N]` is `ItemArtifactFormula::GetMaximumCraftable(player)`** and the **row colour is the RESULT
item's rarity** (`Item::GetDropClassification` -> `GameEngine::GetItemColorText`), not craftability -- which
matches the live observation (yellow magical / green rare / cyan relic while every count read 0).

### 3.4 Clicking a row -- panel `HandleMouseEvent` exe+0x197500 (verified in outline)

The list box's own `vt[0x38]` does the hit test; then

- on a **recipe** row: it looks the formula up, clears the row's `+0x5e` unread badge and calls
  `GameEngine::AddItemToFormulas(uint, uint)` (exported) so the blueprint stops being "new". The selection
  itself is the list box's (`row+0x58`); `Update` copies it into `panel+0x60` on the next frame.
- on a **group header**: it toggles that `GroupDesc+0x54` in the `panel+0x3188` map and sets
  `panel+0x2f73 = 1`, so the tree is rebuilt collapsed/expanded on the next `Update`.

---

## 4. `exe+0x19b680 = CraftingPanel::Update(panel)` -- where everything is recomputed (verified)

```
inv   = Character::GetInventoryItems(player);  money = Character::GetCurrentMoney(player)
if (inv != panel->+0x3340..+0x3348 || money != panel->+0x3358) { cache them; panel->+0x2f74 = 1 }

if (panel->+0x2f76 /*search dirty*/) {                       // section 6.2
    read the edit box text, lower-case it per locale into panel->+0x2f78
    panel->+0x2f77 = !query.empty();  panel->+0x2f73 = 1
}
if (panel->+0x2f73) SetCategory(panel, panel->+0x3130)       // rebuild the tree
if (panel->+0x2f74) RefreshRowLabels(panel)                  // re-do every "[N] Name"

if (ListBox::GetSelectedIndex(listbox) != -1)
    panel->+0x60 = ListBox::GetSelectedRow(listbox, &rowCopy)   // the selected formula id
ListBox::SelectByData(listbox, panel->+0x60)                    // re-assert it

formula = ObjectManager::GetObject(panel->+0x60)                // is-a ItemArtifactFormula
ItemArtifactFormula::GetArtifactInfo(formula, &replicaInfo)     // replicaInfo's first dword = the RESULT ITEM's id
panel->+0x3260 ->vt[0xa8](resultItemId, 0)                      // the result rollover / preview
resultItem = ObjectManager::GetObject(replicaInfo.itemId)       // is-a Item
artifactBox->vt[0xa8](resultItemId);  artifactBox->+0x61 = 0
artifactBox->vt[0xb0](ItemArtifactFormula::GetArtifactCreateQuantity(formula))

cost = ItemArtifactFormula::GetCreationCost(formula, character)
s    = <cost, thousands-separated>                              // exe+0x2a5610
costNumber(+0x2980)->vt[0xb8](s);  tooExpensiveCostNumber(+0x2a78)->vt[0xb8](s)   // BOTH get the text
artifactNameText(+0x2e78)->vt[0xb8](resultItem->vt[0x340]())    // Item::GetGameDescription

if (panel->+0x5c != panel->+0x60) { ClearSelection(panel); panel->+0x5c = panel->+0x60;
                                    <re-fill the result slot> }
FillReagents(panel, panel->+0x60)                               // section 4.1
if (panel->+0x2f74) FillReagents(panel, panel->+0x60)           // again after a refresh

<per-frame vt[0x48] on the 7 reagent slots, the result slot and the search box; rollover bookkeeping>
panel->+0x2f72 = (Character::GetCurrentMoney(character) < cost)  // "cannot afford"
panel->+0x2f71 = (formula != null)                               // "a recipe is selected"
RefreshCombineEnabled(panel)                                     // section 4.2
panel->+0x2f74 = 0
```

Note the cost is written into **both** text elements; which one is visible is a render-time choice driven by
`panel+0x2f72` (**inferred** -- `Render` was not read), and the red style comes from the record
`crafting_costnotenoughmoney.dbr`.

### 4.1 `exe+0x19a930 = CraftingPanel::FillReagents(panel, uint formulaId)` -- the have/need line (verified)

Per reagent (base, then 1..6 -- identical code seven times):

```
need = ItemArtifactFormula::GetReagentBaseQuantityForFormula(formula)   // ...GetReagentNQuantityForFormula
slot->+0x2e8 = need
id = ItemArtifactFormula::GetReagentBaseId(formula)                     // ...GetReagentNId
if (id == 0) { quantityText->+0x28 = 0 /*hide the row*/ }
else {
    quantityText->+0x28 = 1
    have = ItemArtifactFormula::GetReagentBaseCount(formula, character, bool)   // ...GetReagentNCount
    slot->vt[0xa8](id)                                                          // the icon
    slot->+0x228 = ItemArtifactFormula::GetReagentBaseDisplayName(bool)         // the name
    if (have < need) { quantityText.style = "records/ui/styles/text/style_textreddark_sizes_bold.dbr";
                       slot->+0x61 = 1; }
    else             { quantityText.style = "";  slot->+0x61 = 0; }
    text = (have > 99) ? Localize("tagCraftingLargeQuantity", need)   //  "99+/{%d0}"
                       : Localize("tagCraftingQuantity", have, need); //  "{%d0}/{%d1}"
    quantityText->vt[0xb8](text)
}
```

So **the "0/2" is `tagCraftingQuantity` = `{%d0}/{%d1}` with have = `GetReagentNCount(character, bool)` and
need = `GetReagentNQuantityForFormula()`**, and **red = have < need** (style
`style_textreddark_sizes_bold.dbr`; the record also exposes `insufficientMaterialColor` = 0.75/0/0).
The text-element style name lives at element `+0xa8` (so `panel+0x2360` is the base slot's, `panel+0x2458`
reagent 1's, stride 0xf8).

### 4.2 `exe+0x19eb80 = CraftingPanel::RefreshCombineEnabled(panel)` -- exactly when Combine lights up (verified)

```
formula = ObjectManager::GetObject(panel->+0x60)   // is-a ItemArtifactFormula, else null
enabled = panel->+0x2f71                                      // a recipe is selected
       && every slot of { +0x790, +0xa80, +0xd70, +0x1060, +0x1350, +0x1640, +0x1930 } ->vt[0xa0]()
       && panel->+0x2f72 == 0                                 // can afford it
       && formula != null && formula->vt[0x6a0]()             // ItemArtifactFormula::IsBluePrintValid
       && panel->+0x2f75 == 0                                 // not already creating
if (enabled) { button->+0x281 = 0; button->+0x282 = 0; button->+0x284 = 0; }
else         { button->+0x281 = 1; button->+0x282 = 0; button->+0x284 = 1; }
```

`button->+0x281` is `panel+0x2129`. It starts at 1 (disabled) from the ctor.

---

## 5. Opening, closing, escape

### 5.1 `exe+0x26ba30 = CraftingWindow::Show(bool)` (vtable `+0xb0`), verified

```
show (and not already visible):
    if (ObjectManager::GetObject(window->+0x9c))                 // the crafter NPC
        CraftingPanel::SetCrafter(panel, window->+0x9c)          // exe+0x19d900
    GameEngine::UnlockTutorialPage(gGameEngine, 0x29, true)
    press the tab matching window->+0x19a8 through the RADIO registry window+0x4a0 (vt[0x80], no sound)
    panel->vt[0x88](true)                                        // panel Show(true)
    GameEngine::SetSaveEnabled(false)
    <generic framework-B Show exe+0x261cb0: writes +0x68/+0x69, notifies the parent>

hide (and currently visible):
    panel->vt[0x88](false)
    if (window->+0x9c) { npc = GetObject(window->+0x9c); npc->vt[0x960](); window->+0x9c = 0 }
    GameEngine::SetSaveEnabled(true); GameEngine::AutoSave();
    GameEngine::SaveTransferStash(); GameEngine::SaveReagents()
    <generic Show>
```

`Npc::vt[0x960]` is not an exported name (**not resolved**; **inferred**: a "the player is done with me"
notification -- the same slot index the devotion window calls to consume its reset item).

### 5.2 `exe+0x26bc00 = CraftingWindow::SetCrafter(uint npcId)` (vtable `+0xf0`), verified

`window->+0x9c = npcId`, then `ObjectManager::GetObject(npcId)` and a `NpcCrafter::classInfo` RTTI walk
(exact class, then the `RTTI_ClassInfo+0x10` parent chain via exe+0x2a3210). This is what
`GameEngine::DisplayCrafterWindow(npcId)` reaches; the exe's `GameUIInterface` (`[InGameUI+0x98]`, vtable
exe+0x31a680) is the dispatcher, as with every other station window.

`exe+0x19d900 = CraftingPanel::SetCrafter(panel, npcId)` (verified) then reads the NPC:
`panel+0x58 = npcId`, `panel+0x335c = NpcCrafter::RestrictsRecipes()`,
`panel+0x335d = NpcCrafter::LoadAwakenedRecipes()`, `crafterIcon.record = NpcCrafter::GetCrafterBitmapName()`,
`crafterRollover <- NpcCrafter::GetEnhancementTags()`, `panel+0x3178 <- NpcCrafter::GetRecipes()`,
`panel+0x2f73 = 1` (rebuild).

### 5.3 Escape (verified)

- `exe+0x19c200 = CraftingPanel::Escape(panel)`: if the panel is invisible -> false. Else if the search box's
  own `vt[0x68]` consumes it (there is text / it has focus) -> false. Otherwise `ClearSelection(panel)` and
  return **true**.
- `exe+0x26af20 = CraftingWindow::Escape(window)`: if `+0x68 == 0` -> false. Else call the panel's Escape and,
  **when it returns true**, press `closeButton` (`window+0x1a10`) through registry `window+0x19c8`. Always
  returns true.

So one Escape clears the search box, a second clears the selection and closes the window.

### 5.4 `exe+0x19be70 = CraftingPanel::ClearSelection(panel)` (verified)

Clears all 8 slots (`vt[0xa8](0)`), their `+0x61` insufficient flags, their `+0x228` names and `+0x2e8`
quantities, and blanks `costNumber`, `tooExpensiveCostNumber`, `artifactNameText` and the seven quantity
texts. It does **not** touch `panel+0x60`; `Show(false)` (exe+0x19e120) is what zeroes that, sets
`panel+0x3134 = -1` and clears every group's expanded byte.

---

## 6. The two press paths

### 6.1 `exe+0x26af70 = CraftingWindow::OnControlEvent(window+0x90, int event, Control* ctrl, int)` -- verified

Only acts on **event 0**. Offsets translated to window offsets:

```
ctrl == window+0x1a10 (close):  if (!disabled && pressed) pressed = 0;  window->vt[0xb0](false)
ctrl == window+0x4e0:  window->+0x19a8 = 6;  CraftingPanel::SetCategory(panel, 6)
ctrl == window+0x818:  window->+0x19a8 = 2;  SetCategory(panel, 2)
ctrl == window+0xb50:  window->+0x19a8 = 3;  SetCategory(panel, 3)
ctrl == window+0xe88:  window->+0x19a8 = 1;  SetCategory(panel, 1)
ctrl == window+0x11c0: window->+0x19a8 = 4;  SetCategory(panel, 4)
```

The tab registry (`window+0x4a0`, vtable **exe+0x312c68**) is the **radio** variant:
`PressChild` = **exe+0x12b330** -- it walks the registry, fires event **1** on every other pressed, enabled
control and clears its `+0x282`, then presses the target and fires event **0**. Exactly one tab stays pressed.
(Registry A at `window+0x19c8`, vtable exe+0x312cf0, `PressChild` = exe+0x12ac80, is the usual 0/1/2 variant.)

### 6.2 `exe+0x19c3d0 = CraftingPanel::OnControlEvent(panel+0x30, int event, Control* ctrl, int)` -- verified

Only acts on **event 0**:

```
ctrl == panel+0x1ea8 (Combine):      if (panel->+0x2129 /*disabled*/ == 0) DoCombine(panel)   // exe+0x19e260
ctrl == panel+0x238  (clear search): if (panel->+0x2f77) { panel->+0x2f77 = 0;
                                         clear the edit box (exe+0x1947e0); panel->+0x2f76 = 1; }
```

**The search box** filters only through `panel+0x2f76 -> Update -> panel+0x2f78 (lower-cased query) ->
panel+0x2f73 -> SetCategory` -- i.e. it re-runs the whole tree build with the query applied inside
`AddFormulas` (the exact match predicate inside exe+0x197c50 was **not** read; the tooltip
`tagCraftingSearchBoxInfo` says it matches item names **or stats**, which is consistent with the devotion
window's use of a generated search corpus).

---

## 7. `exe+0x19e260 = CraftingPanel::DoCombine(panel)` -- the create (verified)

```
player     = GetObject(panel->+0x54)  as Player
character  = GetObject(panel->+0x54)  as Character
formula    = GetObject(panel->+0x60)  as ItemArtifactFormula
if (!player || !formula) return
if (!artifactBox(panel+0x1c20)->vt[0xa0]()) return      // the result slot is not ready
if (panel->+0x2f75) return                               // already creating
controller = GetObject(Character::GetControllerId(player)) as ControllerPlayer
if (!controller) return

CreateArtifactConfigInfo info;                           // ~0x330 bytes, built on the stack
info.+0x000 = ItemArtifactFormula::GetArtifactCreateQuantity(formula)
info.+0x008 = ObjectManager::CreateObjectID()            // the id the new item will get
info.+0x010 / +0x030 / +0x050 = std::string x3           // record paths (artifact db / result / enhancement)
info.+0x070 = GameEngine::GetRandomSeed()
info.+0x078 = <ItemReplicaInfo-shaped block for the result>
info.+0x1a0 = <reagentBase record>, info.+0x1c0 = GetReagentBaseQuantityForFormula()
info.+0x1c8 = <reagent1 record>,    info.+0x1e8 = GetReagent1QuantityForFormula()   // stride 0x28
info.+0x1f0/+0x210, +0x218/+0x238, +0x240/+0x260, +0x268/+0x288, +0x290/+0x2b0      // reagents 2..6
info.+0x2b4 = ItemArtifactFormula::GetCreationCost(formula, character)
info.+0x2b8 = *(u8*)(formula + 0x10d8)                   // inferred: GetForceRelicComplete
info.+0x2b9 = 1
info.+0x310 = <clamped level from GameEngine::GetPlayerInfo / GetRandomSeed>
info.+0x318 = 0 or 5                                     // a source/kind selector, not decoded
// the enhancement path (a crafter with GetEnhancementTableName) creates a temp object from that table,
// reads it into info.+0x1a8 and destroys it again; formula->vt[0x698] = IsValidArtifact gates it
ControllerCharacter::SendCreateArtifactCmd(controller, info)     // Game.dll, exported, ONE argument
RefreshCombineEnabled(panel)
```

Each `GetReagentN(Character const&, std::string& out)` and `GetReagentNQuantityForFormula()` is an exported
`ItemArtifactFormula` member, so the struct is entirely reconstructible -- but it is long, partially undecoded
(`+0x078`, `+0x310`, `+0x318`) and version-fragile. **Press the button instead** (section 8.5).

---

## 8. What the mod should do

### 8.1 Reach the window and its state -- no new call needed beyond the offsets

```
window = InGameUI + 0x3aa80;   panel = window + 0x1e40;
open        = *(u8*)(window + 0x68);                      // or window->vt[0xb8]()
npcId       = *(u32*)(window + 0x9c);                     // the crafter
playerId    = *(u32*)(window + 0x98);
category    = *(int*)(window + 0x19a8);                   // 6/2/3/1/4 -> Relics/Melee/Ranged/Armor/Misc
selectedId  = *(u32*)(panel  + 0x60);                     // the ItemArtifactFormula's object id, 0 = none
combineOk   = *(u8*)(panel + 0x2129) == 0;                // Combine is enabled
tooPoor     = *(u8*)(panel + 0x2f72);
```

Speak the tab strip from the section 1.3 table (`hooks::localize` on `tagCraftTab*A`, tooltip `...B`), and the
pressed tab from `window+0x19a8` (or the pressed bytes).

### 8.2 Enumerate the rows in the game's own order and grouping

Walk the list box's row vector directly -- it is already the on-screen order, headers included:

```
rows = *(Row**)(panel + 0x3070);   rowsEnd = *(Row**)(panel + 0x3078);   // stride 0xd0
for (r = rows; r != rowsEnd; r += 0xd0) {
    text     = *(u16string*)(r + 0x00);   // "[N] Name" with the game's colour codes, or the CAPS header
    formula  = *(u32*)(r + 0x64);         // 0 => this row is a GROUP HEADER
    selected = *(u8*)(r + 0x58);
    expanded = *(u8*)(r + 0x5d);          // group headers only
    isNew    = *(u8*)(r + 0x5e);          // the "new blueprint" badge
}
```

Strip the colour codes with `LocalizationManager::LocalizerFormatStrip` (the devotion note's recipe), and
speak the rarity separately from `Item::GetDropClassification(resultItem)`. **`[N]` means
`ItemArtifactFormula::GetMaximumCraftable(player)` -- how many you could make right now -- and the colour is
the RESULT's rarity, not craftability.** Both are worth saying; neither is redundant with the other.

Nothing else needs the exe: given a row's formula id, everything is exported --
`GetMaximumCraftable`, `GetCreationCost(character)`, `GetArtifactCreateQuantity`,
`GetArtifact` / `GetArtifactInfo` (the result item's object id),
`GetReagentNId` / `GetReagentNDisplayName(bool)` / `GetReagentNCount(character, bool)` /
`GetReagentNQuantityForFormula()`, and `IsBluePrintValid` through the formula's `vt[0x6a0]`.

### 8.3 Select a recipe programmatically

The list box is the source of truth (`Update` overwrites `panel+0x60` from it every frame), so writing
`panel+0x60` alone does **nothing**. Do it the list box's way:

```
listbox = panel + 0x2fa0;
i = exe+0x1f9dd0(listbox);                       // current selection index, -1 if none
if (i >= 0) *(u8*)(rows + i*0xd0 + 0x58) = 0;    // clear the old row's selected byte
exe+0x1f9f00(listbox, formulaId);                // ListBox::SelectByData -> sets the new row's +0x58
```

The next `Update` fills the reagent panel, the result slot, the name, the cost and the Combine state. Two RVAs
(`exe+0x1f9dd0`, `exe+0x1f9f00`) -- or one, if the mod clears the old byte itself by scanning `+0x58`.

To expand/collapse a group there is no accessor -- toggle that `GroupDesc+0x54` in the `panel+0x3188` map and
set `panel+0x2f73 = 1`. Simpler for a screen reader: **ignore collapsing entirely** and read the rows that are
there; the mod's own graph can group by the header rows.

### 8.4 Read the reagent requirements

Prefer the exports on the selected formula (they are what the window itself calls, and they are correct
whether or not the window has caught up):

```
for n in { base, 1..6 }:
    id   = GetReagentNId(formula);           if (id == 0) skip
    name = GetReagentNDisplayName(false);    // u16, by value, hidden pointer
    have = GetReagentNCount(character, false);
    need = GetReagentNQuantityForFormula();
    -> "<name>, <have> of <need>" (+ "short" when have < need)
cost = GetCreationCost(formula, character);  money = Character::GetCurrentMoney(character)
```

The window's mirrors, if wanted: need at slot `+0x2e8`, name at slot `+0x228`, short flag at slot `+0x61`
(slots `panel+0x790`, `+0xa80`, `+0xd70`, `+0x1060`, `+0x1350`, `+0x1640`, `+0x1930`), the rendered "0/2" at
the text elements `panel+0x22b8 + k*0xf8`.

**The result item and its tooltip**: `ItemArtifactFormula::GetArtifact()` (exported) -- or the first dword of
`GetArtifactInfo(ItemReplicaInfo&)`, which is what the exe uses -- is a **live `Item` object id**. It is the
formula's template artifact, not a rolled instance, which is why its tooltip reads stat RANGES
("+52 Health [52-78]", "+Random Stat(s)" = `tagCraftingRandom`). Feed that id to the mod's existing
item-tooltip path (`Item::GetUIDisplayText` / `Item::GetGameDescription`) exactly like any bag item; no exe
RVA needed.

### 8.5 Press Combine

Do **not** rebuild `CreateArtifactConfigInfo`. Press the button the game's way, which runs `OnControlEvent`
-> `DoCombine`:

```
if (*(u8*)(panel + 0x2129) != 0) -> speak why (no recipe / missing materials / not enough iron bits) and stop;
registry = panel + 0x1e60;                                   // vtable exe+0x312cf0
registry->vt[0x80](registry, panel + 0x1ea8, /*playSound*/ true);
```

The reason to speak comes straight from section 4.2: no selection (`panel+0x2f71 == 0`), a reagent short (any
slot's `+0x61`, or recompute have/need), `panel+0x2f72` = not enough iron bits, or the blueprint is invalid.

Closing: `window->vt[0xb0](window, false)` -- note `Show(false)` also runs `AutoSave` + `SaveTransferStash` +
`SaveReagents`, so prefer it over hiding the window by hand.

Switching tabs: press the tab through the radio registry
`registry(window+0x4a0)->vt[0x80](registry, window+0x4e0 /*or +0x818/+0xb50/+0xe88/+0x11c0*/, true)`,
which sets `window+0x19a8` and rebuilds the tree via `OnControlEvent`. (Writing `window+0x19a8` and calling
`SetCategory` directly also works but leaves the pressed bytes wrong.)

---

## 9. Byte signatures (first 16 bytes, for an `exe_ui::available()`-style check)

```
exe+0x269e80 CraftingWindow::ctor                   48 89 4c 24 08 55 56 57 41 54 41 55 41 56 41 57
exe+0x26a4b0 CraftingWindow::dtor(deleting)         48 89 5c 24 08 57 48 83 ec 20 8b da 48 8b f9 e8
exe+0x26a700 CraftingWindow::Render                 48 8b c4 55 53 41 56 48 8b ec 48 83 ec 70 80 79
exe+0x26aa40 CraftingWindow::HandleMouseEvent       40 53 57 41 56 41 57 48 83 ec 68 44 0f b6 79 68
exe+0x26ad50 CraftingWindow::Update                 40 57 48 81 ec b0 00 00 00 80 79 68 00 48 8b f9
exe+0x26af20 CraftingWindow::HandleEscape           40 53 48 83 ec 20 80 79 68 00 48 8b d9 74 33 48
exe+0x26af70 CraftingWindow::OnControlEvent         85 d2 0f 85 f8 00 00 00 48 8d 81 80 19 00 00 4c
exe+0x26b080 CraftingWindow::SetRecord              48 8b c4 55 48 8b ec 48 83 ec 70 48 c7 45 b0 fe
exe+0x26ba00 CraftingWindow::SetPlayerId            89 91 98 00 00 00 48 81 c1 40 1e 00 00 48 8b 01
exe+0x26ba30 CraftingWindow::Show(bool)             48 89 5c 24 10 57 48 83 ec 20 0f b6 fa 48 8b d9
exe+0x26bc00 CraftingWindow::SetCrafter             40 57 48 83 ec 50 48 c7 44 24 20 fe ff ff ff 48

exe+0x196220 CraftingPanel::ctor                    48 89 4c 24 08 55 56 57 41 54 41 55 41 56 41 57
exe+0x196870 CraftingPanel::dtor(deleting)          48 89 5c 24 08 57 48 83 ec 20 8b da 48 8b f9 e8
exe+0x1971f0 CraftingPanel::Render                  48 89 5c 24 10 48 89 74 24 18 48 89 7c 24 20 55
exe+0x197500 CraftingPanel::HandleMouseEvent        48 8b c4 4c 89 48 20 4c 89 40 18 55 56 57 41 54
exe+0x197c50 CraftingPanel::AddFormulas             48 89 54 24 10 48 89 4c 24 08 55 53 56 57 41 54
exe+0x198500 CraftingPanel::BuildGroups             40 55 56 57 41 56 41 57 48 8d ac 24 70 fe ff ff
exe+0x1999b0 CraftingPanel::PopulateRows            48 89 4c 24 08 53 56 57 41 56 48 81 ec e8 00 00
exe+0x19a060 CraftingPanel::SetCategory             40 55 56 57 41 54 41 55 41 56 41 57 48 83 ec 50
exe+0x19a3f0 CraftingPanel::RefreshRowLabels        40 55 56 57 41 54 41 55 41 56 41 57 48 8d ac 24
exe+0x19a930 CraftingPanel::FillReagents            4c 89 44 24 18 55 56 57 41 54 41 55 41 56 41 57
exe+0x19b680 CraftingPanel::Update                  89 54 24 10 55 56 57 41 54 41 55 41 56 41 57 48
exe+0x19be70 CraftingPanel::ClearSelection          48 8b c4 55 41 54 41 55 41 56 41 57 48 8b ec 48
exe+0x19c200 CraftingPanel::HandleEscape            40 53 48 83 ec 20 80 79 50 00 48 8b d9 74 1f 48
exe+0x19c3d0 CraftingPanel::OnControlEvent          85 d2 75 5b 53 48 83 ec 20 48 8d 81 78 1e 00 00
exe+0x19c430 CraftingPanel::SetRecord               48 8b c4 55 56 57 41 54 41 55 41 56 41 57 48 8d
exe+0x19d850 CraftingPanel::SetPlayerId             48 89 5c 24 08 57 48 83 ec 20 48 8b d9 89 51 54
exe+0x19d900 CraftingPanel::SetCrafter              40 55 56 57 41 54 41 55 41 56 41 57 48 8d ac 24
exe+0x19e120 CraftingPanel::Show(bool)              40 53 48 83 ec 20 48 8b d9 38 51 50 0f 84 1d 01
exe+0x19e260 CraftingPanel::DoCombine               40 55 53 56 57 41 54 41 55 41 56 41 57 48 8d ac
exe+0x19eb80 CraftingPanel::RefreshCombineEnabled   48 89 5c 24 10 57 48 83 ec 20 8b 59 60 48 8b f9

exe+0x1f94e0 ListBox::ctor                          48 89 4c 24 08 55 48 8b ec 48 83 ec 50 48 c7 45
exe+0x1f9b20 ListBox::AddRow                        48 8b c4 55 48 8d 68 b9 48 81 ec 00 01 00 00 48
exe+0x1f9dd0 ListBox::GetSelectedIndex              48 8b 91 d8 00 00 00 49 bb c5 4e ec c4 4e ec c4
exe+0x1f9f00 ListBox::SelectByData                  48 89 5c 24 08 48 89 7c 24 10 44 8b d2 4c 8b c9
exe+0x1f9fd0 ListBox::GetSelectedRow                48 89 5c 24 08 48 89 74 24 10 48 89 7c 24 18 55
exe+0x1fa580 ListBox::SetRowRecord                  4c 89 44 24 18 56 57 41 56 48 83 ec 30 48 c7 44
exe+0x1faa60 ListBox::GetRows                       40 53 48 83 ec 30 48 8b da c7 44 24 20 00 00 00
exe+0x12b330 RadioRegistry::PressChild              41 54 41 56 48 83 ec 28 4c 8b 49 30 45 0f b6 e0
```

Vtables touched: exe+0x31cf38 (CraftingWindow), exe+0x31d030 / exe+0x31d038 (its two sub-vtables),
exe+0x31cb90 (the framework-B window base), exe+0x316140 / exe+0x3161d8 / exe+0x316138 (CraftingPanel and its
listener), exe+0x3162a8 + exe+0x3161e8 (the two big-slot classes), exe+0x313860 (Image),
exe+0x31c4d0 / exe+0x31c2b0 / exe+0x316368 (the three text-element classes), exe+0x312c68 (the radio/tab
registry), exe+0x312cf0 (registry A). Game.dll vtables used by index: `ItemArtifactFormula`
(Game.dll+0x73b628) `+0x690 GetMaximumCraftable`, `+0x698 IsValidArtifact`, `+0x6a0 IsBluePrintValid`;
`Item` `+0x340 GetGameDescription`, `+0x568` the item-class getter (folds onto
`WeaponMelee_Mace::GetWeaponType`); `Weapon` `+0x6b0 GetWeaponType`.

---

## 10. The `.arz` side (`tools/arz.py`)

- `records/ui/hud/hud_mastertable.dbr` -> `hudEnchanterWindow = records/ui/inventor/inventor_mastertable.dbr`.
  There is **no** `hudCraftingWindow`; the crafting window shares that record.
- `records/ui/inventor/inventor_mastertable.dbr` (template `ingameui/enchanterwindow.tpl`):
  `enchanterBaseBitmap`, `enchanterArtifactTabBitmap`, `enchanterNameText` (`crafter_name.dbr`),
  `crafterHeadingText` (`crafter_title.dbr`), `enchanterCloseButton`, `enchanterArtifactTab`
  (`craftingpanel/crafting_table.dbr`), `craftingTabArtifactButton`, `craftingTab1..4Button` and their
  `...Rollover` twins. `windowDefaultExtentX/Y = 346/715`, aligned Left/Center.
- `records/ui/inventor/craftingpanel/crafting_table.dbr` (template `ingameui/enchanterartifacttab.tpl`):
  `searchBox`, `clearSearchButton`, `craftingListBox`, `craftingListBoxItem`, `craftingListBoxHeaderFont`,
  `reagentBaseBox/Quantity`, `reagent1..6Box/Quantity`, `artifactBox`, `artifactNameText`,
  `createArtifactButton`, `costText`, `costNumber`, `tooExpensiveCostNumber`, `crafterIcon`,
  `crafterRollover`, `displayHelpText`, `formulaUnreadIcon`, `insufficientMaterialColor.r/g/b = 0.75/0/0`,
  and **`craftingDefaultRecipes`** -- an 87-entry list of blueprint records every crafter offers
  (`craft_random*`, `craft_component_base_*`, `craft_relic_b00*`, the resist potions, ...).
  `artifactFormulaBox`, `reagentNDisplay`, `reagentNDisplayX/Y` and `reguirementsText` (sic) are **not**
  referenced by any code path read here.
- `records/ui/inventor/craftingpanel/crafting_completeitembutton.dbr` -> `textTag = tagCraftingCombine`.

Strings (`tools/arc_unpack.py`, `tags_ui.txt`):
`tagCraftingQuantity = "{%d0}/{%d1}"`, `tagCraftingLargeQuantity = "99+/{%d0}"`,
`tagCraftingCost = "Cost:"`, `tagCraftingCombine = "Combine"`,
`tagCraftingButtonInfo`, `tagCraftingSearchBoxInfo`, `tagCraftingDisplayBoxHelp`,
`tagCraftingRandom = "{^E}+Random Stat(s)"`, `tagCraftingLearn = "[Right-Click to Learn]"`,
`tagCraftingComponentCount`, `tagCraftingComponentMissing`, plus the 26 group headers of section 3.1 and the
five tab tag pairs of section 1.3.

---

## 11. Open / not resolved

- The tab-category classification (`category = item->vt[0x568](); if (dynamic_cast<ItemEnchantment*>(item))
  category = 6;`) does not obviously explain COMPONENTS appearing under the Accessories tab. The exact
  `Item` class enum behind `vt[0x568]` was not named (the COMDAT-folded symbol resolves to
  `WeaponMelee_Mace::GetWeaponType`).
- `GroupDesc`'s two int vectors (`+0x20`, `+0x38`) are read but their per-group contents were only sampled
  (RelicsTier01 = {1} / {0xb}, Runes = {0x15} / {6}); the matching predicate inside `AddFormulas`
  (exe+0x197c50) was read only in outline.
- `ListBox::AddRow` (exe+0x1f9b20)'s full argument list, and the rest of the 0xd0-byte Row (`+0x20`,
  `+0x30..+0x48`, `+0x5a..+0x5c`, `+0x5f`, `+0x6c..+0xd0`), were not decoded.
- The search filter's actual match predicate was not read (see 6.2).
- `Render` (exe+0x1971f0 / exe+0x26a700) was skimmed only for the control order; the choice between
  `costNumber` and `tooExpensiveCostNumber` is **inferred** from `panel+0x2f72`.
- `CreateArtifactConfigInfo` fields `+0x078`, `+0x310`, `+0x318` and the byte at `formula+0x10d8` are
  **inferred** / undecoded. The mod is told to press the button instead.
- `Npc::vt[0x960]` (called on the crafter when the window hides) is not an exported name.
- `panel+0x570` is an item-slot control that this window never gives a record; **inferred** to be the legacy
  blueprint slot.
- `panel+0x2f70`, `panel+0x3290`, row `+0x59` and the panel's second listener (`+0x38`) were not chased.
