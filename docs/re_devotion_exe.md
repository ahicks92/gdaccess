# The Devotion window: the exe side (Grim Dawn v1.3.0.8 x64, exe timestamp 0x6a85fbec, image 0x482000)

Static RE on `build/GrimDawn.unpacked.bin` with `tools/exe_dis.py` / `tools/dll_dis.py` / `tools/arz.py`,
2026-08-27. **Nothing here has been exercised live.** All addresses are RVAs (`exe+...`, `Game.dll+...`);
"verified" = read in disassembly, "inferred" is marked. Companion to `docs/exe-ui-layout.md` (frameworks A/B),
`docs/ingame-ui-survey.md` (the InGameUI window map, whose "Devotion (+0x813a0) -- HARD" entry this supersedes)
and `docs/re_pets_exe.md` (the model note this follows).

Anchors: devotion window = `InGameUI + 0x813a0`, ctor exe+0x185640, vtable exe+0x315d80, size 0x2b38.
`InGameUI = [[main_obj+0x90]+0x2f0]`, `main_obj = [exe+0x3ceef8]`.

**Correction to the survey**: the survey said "no widget list, a screen would be built from the .arz graph".
That is wrong in the useful direction — the exe DOES build a full object graph (110 `Constellation` objects,
each holding its `Star` objects with skill ids, links, positions and per-star eligibility flags), and it is all
reachable by base-relative reads plus five vectors. The `.arz` is only needed for the ORDER/authoring, not to
know the graph at runtime.

---

## 1. Object graph

### 1.1 The three exe classes

- `DevotionWindow` — `InGameUI+0x813a0`, size 0x2b38, ctor exe+0x185640, primary vtable exe+0x315d80.
  Secondary vtables written by the ctor: `+0x08` -> exe+0x315d70 (slot0 exe+0x18b810, the map renderer),
  `+0x90` -> exe+0x315d78 (slot0 **exe+0x18bd50 = OnControlEvent**), `+0x98` -> exe+0x315d68
  (slot0 **exe+0x186620 = OnSkillPicked**), `+0xa0` -> exe+0x317748 (stub).
- `Constellation` — heap, size **0x228**, ctor exe+0x17f7c0(this, DevotionWindow*), vtable exe+0x315ab0.
- `Star` (the exe's "devotion button") — heap, size **0x190**, ctor exe+0x17d5a0(this, DevotionWindow*,
  Constellation*), vtable exe+0x315a28 (only 17 slots, 0x00..0x80).

### 1.2 DevotionWindow offsets (all verified from the ctor exe+0x185640 and the loader exe+0x186cf0)

Framework-B base: `+0x28` visible-ish (base control), `+0x30` -> the world/session host X (so
`[[window+0x30]+0x2f0] == InGameUI`), `+0x38/+0x3c` position, `+0x40..+0x4c` rect, `+0x68` **the window's
own visible byte** (what `IsVisible` returns; `Show` writes it), `+0x90` listener sub-object,
`+0x98` skill-pick listener sub-object.

Constellation data:
- **`+0xa8 / +0xb0 / +0xb8` = `std::vector<Constellation*>`** — built by the loader from
  `devotionConstellation%u`, u = 1..0x6e (110 slots tried; 87 are populated in 1.3.0.8).
  Evidence: exe+0x1870c2..0x187233 (the `%u` loop, `cmp r15d, 0x6e`), each non-empty field allocates 0x228
  bytes, runs exe+0x17f7c0, `vt[0x80]` = exe+0x17fe80 (its loader) and is pushed onto `+0xa8`.
- `+0xd8` background texture (`bgTile`), `+0x128` `std::string emptySkillBitmap`, `+0x148`
  `std::string levelTextString`.
- `+0x100/+0x104` centre offset (`centerOffsetX/Y`), `+0x108/+0x10c/+0x110/+0x114` the map viewport rect
  (x, y, w, h — `positionX/positionY` + `sizeX/sizeY`), `+0xe0/+0xe4` **pan offset**, `+0x118/+0x11c` last
  drag position, `+0x120` byte "dragging", **`+0x124` float zoom** (ctor 0x3f4ccccd = 0.8f; `Show(true)`
  resets it to 0.8f).
- `+0x23f0/+0x23f8/+0x2400` `std::vector<NebulaSection*>` (0x60 bytes each, from `nebulaSections`) — purely
  decorative background art.

Controls (all by-value members; registries are the framework-B kind, `exe+0x12a800(registry, control,
listener)` registers). Record field name -> offset, read at exe+0x187a5f..0x188690:
- `+0x168` **the main control registry**; `+0x1338` a second registry (the three tab buttons).
- `+0x1b0` **undoButton** (TextButton, ctor exe+0x126fe0)
- `+0x560` **closeButton** (Button, ctor exe+0x10aee0)
- `+0x898` **helpButton** (Button)
- `+0xbd0` **zoomInButton**, `+0xf08` **zoomOutButton** (Buttons)
- `+0x1240` **zoomText**
- `+0x1378` **skillsTab1Button**, `+0x1728` **skillsTab2Button**, `+0x1ad8` **skillsTab3Button** (= the
  Devotion tab, i.e. the tab this window IS). TextButtons: caption u16 at `+0x358`, disabled byte `+0x281`,
  pressed byte `+0x282`. So `window+0x15fa / +0x19aa / +0x1d5a` are the three pressed bytes and
  `window+0x15f9 / +0x19a9` the disabled bytes.
- `+0x1e88` **searchBox** (edit control, ctor exe+0x192dd0; its text is the `std::string`-shaped field at
  `+0x1ef8`, cleared by `Show(true)`), `+0x2058` searchBoxBackground, `+0x20b8` **clearSearchButton**.
- `+0x2448` an image element (`edgeFade`), `+0x24b0` `effectBgBitmap`, `+0x2510` `disabledBitmap`,
  `+0x2570` `starColorPulseBitmap`.
- **`+0x25d0` = the celestial-power skill picker** (`skillSelect`), a whole sub-window, size 0x4e0,
  ctor exe+0x1d4f90, vtable exe+0x318448. See section 4.
- **`+0x2408` = the info panel** (0x1df8 bytes, ctor exe+0x18ce90(this, window); it keeps the window back-
  pointer at `panel+0x98`). Draws the points/affinity readout — section 5.

State:
- **`+0x2410` = the player object id** (dword). Written by `InGameUI` at exe+0x218bd2 from `InGameUI+0xa0`
  (`mov [rsi+0x837b0], edi` with rsi = InGameUI; 0x837b0 - 0x813a0 = 0x2410). Every handler resolves the
  player with `ObjectManager::GetObject(window+0x2410)`.
- `+0x2418` int, 1 in the ctor (unused elsewhere in what was read — **inferred** cosmetic).
- **`+0x2419` byte = RECLAIM / "undo" mode** (the analogue of the skills window's `+0x1f4c`). Read by
  `Star::HandleMouseEvent` (exe+0x17ea56), `RefreshEligibility` (exe+0x184e5c), `Star::BuildRollover`
  (exe+0x17f19a), the listener (exe+0x18c103), the info panel (exe+0x18f263). **Set only by
  `GameUIInterface::DisplayDevotionWindow(itemId != 0)`** at exe+0x21aabc; cleared by `Show(false)`
  at exe+0x18b000.
- **`+0x241a` byte = "there are uncommitted changes"** (set at exe+0x185489 whenever a star's pending
  counter goes above 0; cleared by Undo and by `Show(false)`).
- `+0x241b` byte = "search text dirty, re-filter" (set by `Show(true)`, by clearSearchButton, by the edit box).
- `+0x241c` byte = "the search box is non-empty".
- `+0x2420` `std::basic_string<unsigned short>` = the **lower-cased search query**; `+0x2440` int.
- **`+0x2440` int = the object id of the devotion-reset item** that opened reclaim mode (a
  `GAME::ItemDevotionReset`, i.e. Tonic of Clarity). Consumed on close (exe+0x18b024 calls the object's
  `vt[0x960]`).
- `+0x2aa0` int = the currently-selected star's **host skill id** (a copy of `Star+0x10c`), `+0x2aa4` int =
  the selected star's **skill id** (a copy of `Star+0x108`). These two are literally `skillSelect+0x4d0` and
  `skillSelect+0x4d4` (0x25d0 + 0x4d0 = 0x2aa0), i.e. the picker's inputs.
- `+0x2ab0/+0x2ab4` float = the selected star's screen position; **`+0x2ab8` = `Star*` the pick is for**
  (nulled by the bind).
- `+0x2a98` = `window+0x98` (the picker's listener pointer; `skillSelect+0x4c8`).
- `+0x2ac0/+0x2ac8/+0x2ad0` connection line textures (active / inactive / locked), `+0x2ae0` float
  `connectionWidth`, `+0x2ad8` the `shaders/basictexture.ssh` shader, `+0x2ae8` `devotionButtonSound`.
- **`+0x2ae4` int = the net reclamation-point delta** accumulated by the session's clicks.
- `+0x2af0` ColorPulse (`constellationColorPulse`), `+0x2af8/+0x2afc` `starPulseDelayMin/Max`,
  `+0x2b0c` an RNG seed, `+0x2b20/+0x2b28/+0x2b30` a vector of 15 star ColorPulses.
- `+0x2b10` int, **-1 = idle**, otherwise "a modal confirm is pending and this is the tab we want to go to".
- `+0x2b14` byte, "the window was closed on purpose" (**inferred**; set by `GoToSkillsTab` in reclaim mode
  and cleared by `Show(false)`).
- **`+0x2414` int = the skill id waiting on the "replace the existing celestial power?" confirm.**

### 1.3 Constellation offsets (ctor exe+0x17f7c0, loader exe+0x17fe80 = vtable `+0x80`)

- `+0x30` byte **affinity requirement met** (cached result of exe+0x1815e0)
- `+0x31` byte **any star of this constellation is learned** (cached by exe+0x181790)
- `+0x32` byte **complete** (every star learned; cached by exe+0x181690)
- `+0x33`, `+0x34` bytes — render/flash state
- `+0x38` `std::string` **constellationDisplayTag** (the localization tag of the constellation's NAME)
- `+0x48` `std::string` `constellationRollover` record path
- `+0x58` `std::string` **constellationInfoTag** (the description tag)
- `+0x68` `std::string` background texture path, `+0xb0` its loaded texture, `+0xc0` the image control
- **`+0x78 / +0x80 / +0x88` = `std::vector<Star*>`** (from `devotionButton1..N`)
- **`+0x90 / +0x98 / +0xa0` = `mem::vector<std::pair<AffinityType, unsigned>>` = affinity REQUIRED**
  (from `affinityRequiredName%u` / `affinityRequired%u`, u = 1..3; pairs are 8 bytes, `{int type, int amount}`,
  sorted by exe+0x181980)
- **`+0xa8 / +0xb0 / +0xb8` = the same shape = affinity GIVEN** (`affinityGivenName%u` / `affinityGiven%u`,
  u = 1..3)
- `+0x120` ColorPulse (`constellationFlashPulse`), `+0x128..+0x164` the RGBA colour sets
  (unavailable / available / active, plus one per affinity)
- `+0x1c8/+0x1c9` render flags, `+0x1d0` -> the DevotionWindow, `+0x1d8` last mouse position
- **`+0x1e0` = `mem::vector<GameTextLine>` = the constellation's built ROLLOVER**, `+0x218` = a pointer to it
  when the mouse is inside (else 0). Built by exe+0x180fd0 (section 5.2).
- `+0x208` `std::string`, `+0x220` `constellationCompleteSound`

**AffinityType enum, read off the loader's `_stricmp` chain (exe+0x180699..0x180711), verified:**
`ascendant = 0`, `chaos = 1`, `eldritch = 2`, `order = 3`, `primordial = 4`; an unrecognised name gives -1.
(This matters — the info panel reads them back in that numeric order, exe+0x18f1ad..0x18f236.)

### 1.4 Star offsets (ctor exe+0x17d5a0, loader exe+0x17da10 = vtable `+0x18`)

- `+0x30` -> the DevotionWindow, `+0x38` -> the owning Constellation
- `+0x40/+0x44` float `bitmapPositionX/Y` (map-space; multiplied by the window's zoom `+0x124`)
- `+0x48` `std::string` **skillName** = the star's `.dbr` record path (`records/skills/devotion/tierN_XXy.dbr`)
- `+0x68` `std::string` = the dynamic icon path (the bound skill's icon), loaded into `+0xb0`
- `+0x88/+0x8c` float `skillOffsetX/Y`
- `+0x90` texture up, `+0x98` down, `+0xa0` in-focus, `+0xa8` disabled, `+0xb0` the overlay icon
- `+0xe8` `std::string levelTextStyle`
- **`+0x108` int = the star's Skill object id** (resolved from `skillName`)
- **`+0x10c` int = the object id of the skill this star's celestial power is bound to** (0 = unbound; kept in
  sync with `Skill::GetDevotionParent()` on the star's own Skill)
- `+0x110` int = the host skill id remembered across a reclaim, so Undo can re-bind it
- **`+0x118/+0x120/+0x128` = `mem::vector<int>` = `devotionLinks%u`** — 1-BASED indices into the owning
  constellation's star vector (the code does `stars[links[i] - 1]`, exe+0x184ea0). These are the star's
  prerequisites/neighbours.
- `+0x130` int = **pending point changes made on this star since the window opened** (what Undo reverts)
- **`+0x134` byte = "blocked: no linked predecessor star is learned"** (in reclaim mode it means
  "blocked: a learned successor depends on this star")
- **`+0x135` byte = "blocked: the constellation's affinity requirement is unmet"** (in reclaim mode it is the
  "removing this would break another constellation" result)
- `+0x136` byte hovered, `+0x137` byte "a left-down landed on me", `+0x13c/+0x140` last mouse position
- **`+0x148/+0x150/+0x158` = `std::vector<u16string>` = the lower-cased searchable text** (section 6)
- **`+0x160 .. +0x16d` = a 14-byte `GAME::SkillReasons`** passed straight to `GenerateUIDevotionText`
  (section 5.1)
- `+0x170..+0x184` colour, `+0x188` `soundNameDown` SoundPak

### 1.5 The `.arz` side (for authoring order / offline tables, `tools/arz.py`)

- `records/ui/skills/devotion/devotion_mastertable.dbr` — `devotionConstellation1..87`, plus the window's
  own art/field names (`bgTile`, `emptySkillBitmap`, `levelTextString`, `searchBox`, `undoButton`,
  `zoomInButton`, `affinity01..05Bitmap/Number/Rollover`, `affinityTitle`, `nebulaSections`, ...).
- `records/ui/skills/devotion/constellations/constellationNN.dbr` — `constellationDisplayTag`
  (e.g. `tagDevotion_A01` = "Bat"), `constellationInfoTag` (`tagDevotion_A01Desc`),
  `affinityRequired1..3` + `affinityRequiredName1..3`, `affinityGiven1..3` + `affinityGivenName1..3`,
  `devotionButton1..5`, `devotionLinks2..5`.
- `records/ui/skills/devotion/tierN_XXy.dbr` — `skillName` -> `records/skills/devotion/tierN_XXy.dbr`,
  `bitmapPositionX/Y`.
- The runtime graph is identical to this, so **the mod does not need the .arz** except to fix a display order.

---

## 2. Actions: clicking a star

### 2.1 Dispatch chain (verified)

1. `DevotionWindow::HandleMouseEvent` = vtable `+0x38` = **exe+0x18a820**. Returns early if `+0x68` == 0.
   Dispatch order: the skill picker `+0x25d0`, then registry `+0x168` (undo/close/help/zoom/clear-search),
   registry `+0x1338` (the tab strip), the info panel `+0x2408`, the search box `+0x1e88`, then
   **exe+0x185240** (the star map), then wheel zoom (event types 0x11/0x12 -> exe+0x18ae50) and map panning
   (`+0x120` / `+0xe0`).
   The constellation *hover* is handled separately at exe+0x18ab88: it walks `+0xa8..+0xb0` and calls
   `exe+0x180fd0(constellation, event, origin, zoom)` which builds the constellation rollover.
   Finally the hovered thing is handed to the shared tooltip helper `exe+0x2371e0(InGameUI+0xb800, hovered,
   changed, 0)` (`InGameUI+0xb838` = last hovered, `+0xb849` = a latch).
2. **exe+0x185240 = `DevotionWindow::HandleStarMouse(window, MouseEvent*, Vec2* origin, void** outHovered)`**:
   for every constellation, copies its star vector and calls
   `exe+0x17ea10(star, event, origin, zoom /*xmm3 = window+0x124*/, &outOpenPicker /*byte*/,
   &outReclaimDelta /*int*/, window+0x2ae8 /*sound*/)`.
   On a handled star: `*outHovered = star`; `window+0x2ae4 += outReclaimDelta`; and **if the event type is 9
   (LEFT UP)** it runs `exe+0x184db0` (RefreshEligibility) and, when `outOpenPicker` is set, fills
   `window+0x2ab8/+0x2ab0/+0x2ab4/+0x2aa4/+0x2aa0` and calls `skillSelect->vt[0xb0](true)` = Show — i.e.
   **the celestial-power picker opens**. It also sets `window+0x241a = 1` if the star has pending changes.

### 2.2 `Star::HandleMouseEvent` = exe+0x17ea10 — the whole spend/reclaim semantics (verified)

`r12 = window->+0x2419` (reclaim mode). Hit test = the star's rect (`+0x40/+0x44` scaled by zoom, size from
texture `+0x90`); miss returns false. On hit, `star+0x136 = 1`.

**Event type 1 (left DOWN)**: `star+0x137 = 1`, plus a click sound (only in reclaim mode, and only when
the reclaim is affordable: `!exe+0x17f030(player) && !exe+0x17f080(player)`).

**Event type 9 (left UP) with `star+0x137` set** — clear it, then:

```
if (star->+0x134 != 0 || star->+0x135 != 0)     // BLOCKED (link or affinity)
    goto REOPEN;
level = min(Skill::GetSkillLevel(s), Skill::GetMaxLevel(s));
if (window->+0x2419)  goto RECLAIM;

/* ---- LEARN ---- */
wasComplete = constellation->+0x32;
if (level == 0)                       goto SPEND;
if (Skill::GetSkillOperation(s) == 1) goto SPEND;   // multi-rank star
REOPEN:
    if (Skill::GetSkillOperation(s) == 3) *outOpenPicker = 1;   // a celestial power: re-pick its host
    return true;

SPEND:
    if (Character::GetDevotionPoints(player) == 0) goto REOPEN;   // no points -> just re-offer the picker
    s->vt[0x48](s, 1);                          // Skill::IncrementSkillLevel(1)
    Character::SubtractDevotionPoint(player);
    if (Skill::GetDevotionLevel(s) < 1)
        s->vt[0xa0](s);                         // Skill::IncrementDevotionLevel()
    star->+0x130 += 1;
    if (!wasComplete && exe+0x181690(constellation))            // became complete
        exe+0x181870(constellation, /*playSound*/ true);        //   -> AddAffinity for each affinityGiven
    if (Skill::GetSkillOperation(s) == 3) {
        GameEngine::UnlockTutorialPage(gGameEngine, 0x3c, true);
        *outOpenPicker = 1;                     // open the celestial-power picker
    }
    play star->+0x188;

/* ---- RECLAIM (window->+0x2419 set) ---- */
    if (level == 0) return true;                                 // nothing spent here
    wasComplete = constellation->+0x32;
    if (!SkillManager::UseDevotionReclamationPoints(sm, 1)) return true;   // cost/afford gate
    *outReclaimDelta -= 1;
    if (--level == 0) {
        s->vt[0x50](s, 0);                      // Skill::SetSkillLevel(0)
        if (wasComplete && !exe+0x181690(constellation))
            exe+0x181910(constellation);        //   -> SubtractAffinity for each affinityGiven
    }
    if (Skill::GetSkillLevel(s) == 0 && Skill::GetSkillOperation(s) == 3) {
        host = ObjectManager::GetObject(star->+0x10c);
        if (host) host->vt[0x5e0](host, nullptr, "", false);   // Skill::SetAutocastSkill -> unbind
        s->vt[0x50](s, 0);
        star->+0x110 = star->+0x10c;            // remember it for Undo
        reload star->+0xb0 icon;
    }
    star->+0x130 += 1;
    Character::AddDevotionPoints(player, 1);
```

`Skill_Operation` (CORRECTED against the Game.dll loader, `docs/re_devotion_gamedll.md`: 0 = ordinary
skill, 1 = "Skill", 2 = "Passive", 3 = "Effect"; the devotion tree ships only 2 and 3): an ordinary star is
**2**, so a learned star falls to REOPEN and does nothing -- **one point per star**; the `== 1` branch
(spend again on a learned multi-rank star) is dead code for shipped data. **3 = a celestial power / proc**
(clicking a learned one re-opens the assignment picker). The enum is `Skill+0x4a0`, `Skill::GetSkillOperation`
is exported.

Vtable slots used, resolved against `Game.dll!??_7Skill@GAME@@6B@` (verified):
`+0x48 Skill::IncrementSkillLevel`, `+0x50 Skill::SetSkillLevel`, `+0x58 Skill::DecrementSkillLevel`,
`+0xa0 Skill::IncrementDevotionLevel`, `+0x5e0 Skill::SetAutocastSkill`, `+0x5e8 Skill::SetPetAutocast`.
Note the devotion code uses **SetSkillLevel(0)**, not DecrementSkillLevel.

### 2.3 The eligibility gate — `exe+0x184db0` = `DevotionWindow::RefreshEligibility(window)` (verified)

Runs on `Show(true)`, after every star click, and after Undo. For each constellation it first refreshes
`+0x31` (exe+0x181790), then for each star:

- `star->+0x134 = 0`
- **normal mode** (`window->+0x2419 == 0`):
  - if the star has links: `star->+0x134 = !(any `stars[links[i]-1]`'s Skill has `GetSkillLevel != 0`)`.
    A star with NO links is always link-eligible — that is the constellation's entry star.
  - `star->+0x135 = !(for every pair in constellation->+0x90 (affinityRequired):
    Character::GetAffinity(player, type) >= amount)`
- **reclaim mode**: if the star's Skill is learned, it marks **each of its link targets** `+0x134 = 1`
  (you cannot reclaim a star that a learned neighbour rests on); then
  `star->+0x135 = exe+0x18c3f0(window, constellation, &dependentNames, &selfLocked)` and it publishes the
  result to the game object with `Skill::SetConstellationDependencies(s, dependentNames)` and
  `Skill::SetConstellationSelfLocked(s, selfLocked)` — which is exactly what the exported
  `Skill::GetConstellationDependencies()` / `GetConstellationSelfLocked()` read back.

`exe+0x18c3f0 = ComputeReclaimBlockers(window, Constellation*, mem::vector<std::string>* out, bool* selfLocked)`
(verified head, tail read in outline): builds `affinities[0..4] = Character::GetAffinity(player, t)`,
subtracts this constellation's `affinityGiven` pairs, then walks every constellation with a learned star and
records the `constellationDisplayTag` of each one whose `affinityRequired` would no longer be met; the flag
is set when the failing constellation is this one. **This is the "constellation self-lock".**

Cost helpers (verified):
- `exe+0x17f030(player)` -> `SkillManager::GetDevotionReclamationAetherCost() > Player::GetCurrentAether()`
- `exe+0x17f080(player)` -> `SkillManager::GetCurrentDevotionReclamationCost() > Character::GetCurrentMoney()`

### 2.4 Constellation helpers (verified)

- `exe+0x1815e0(c, bool recompute)` = **IsAffinityMet**: with `recompute == 0` returns the cached `c+0x30`;
  otherwise all of `Character::GetAffinity(player, pair.type) >= pair.amount` over `c+0x90`.
- `exe+0x181690(c)` = **IsComplete**: every star's `min(level, maxLevel) > 0`; caches into `c+0x32`.
- `exe+0x181790(c)` = **HasAnyStarLearned**; caches into `c+0x31`.
- `exe+0x181870(c, bool playSound)` = **GrantAffinity**: if `c+0x32`, `Character::AddAffinity(player, type,
  amount)` for each pair of `c+0xa8`, then plays `c+0x220` (`constellationCompleteSound`).
- `exe+0x181910(c)` = **RevokeAffinity**: `Character::SubtractAffinity` over the same list.

### 2.5 Undo — inside `OnControlEvent` (exe+0x18be02 .. 0x18c183, verified)

The `undoButton` (`window+0x1b0`) reverts **every** pending change made since the window opened:

```
total = 0; affinityChanged = false
for each constellation C, for each star S with S->+0x130 != 0:
    s = ObjectManager::GetObject(S->+0x108)
    if (!window->+0x2419) {                                  // undo the SPENDS
        wasComplete = IsComplete(C)
        s->vt[0x50](s, 0)                                    // SetSkillLevel(0)
        if (wasComplete && !IsComplete(C)) { RevokeAffinity(C); affinityChanged = true }
        if (GetSkillLevel(s) == 0 && GetSkillOperation(s) == 3) {
            host = GetObject(S->+0x10c); if (host) host->vt[0x5e0](host, nullptr, "", false)
            s->vt[0x50](s, 0); S->+0x10c = 0; reload icon
        }
    } else {                                                 // undo the RECLAIMS
        wasComplete = IsComplete(C)
        s->vt[0x48](s, S->+0x130)                            // IncrementSkillLevel(n)
        if (!wasComplete && IsComplete(C)) { GrantAffinity(C, false); affinityChanged = true }
        if (S->+0x110) { window->+0x2ab8 = S; (window+0x98)->vt[0](S->+0x110) }   // re-bind the power
    }
    total += S->+0x130;  S->+0x130 = 0;  S->+0x110 = 0
if (affinityChanged) refresh every C's +0x30 (and start its flash pulse)
if (!window->+0x2419) Character::AddDevotionPoints(player, total)
else                  total x Character::SubtractDevotionPoint(player)
SkillManager::UseDevotionReclamationPoints(sm, window->+0x2ae4)     // "Failure in the reclamation point
                                                                    //  system.  Very Bad" on false
window->+0x241a = 0;  window->+0x2ae4 = 0;  RefreshEligibility(window)
```

### 2.6 There is NO deferred commit

Everything is applied to the live `Skill` / `Character` objects the instant you click. What the window
does instead is **`GameEngine::SetSaveEnabled(false)` on open and `GameEngine::AutoSave()` on close**
(`Show`, exe+0x18b0f7 / exe+0x18b059). `ControllerCharacter::SendReclaimDevotionPointCmd(int,int)` and
`ReclaimDevotionPointConfigCmd` are **not** imported by the exe at all (checked with
`exe_dis.py imp`) — they are the multiplayer server path inside Game.dll, not something this window calls.
The survey's "commit path exe+0x18c0a0 calls SendReclaimDevotionPointCmd" is wrong: exe+0x18c0a0 is the
middle of `OnControlEvent`'s Undo body.

### 2.7 Recipes a mod can call without the mouse

**Spend one point on star X** (X = a `Star*` from the window's graph, or just a devotion `Skill*` you
resolved yourself plus the constellation you know it belongs to):

```
s   = the star's Skill                       // ObjectManager::GetObject(star->+0x108),
                                             //   or SkillManager::FindSkillId("records/skills/devotion/…")
p   = GameEngine::GetMainPlayer()
// gate (replicate RefreshEligibility + HandleMouseEvent):
//   Character::GetDevotionPoints(p) > 0
//   every affinityRequired pair of the constellation is met (Character::GetAffinity)
//   at least one linked star is learned, unless the star has no links
//   Skill::GetDevotionLevel(s) < Skill::GetDevotionMaxLevel(s)
wasComplete = <all stars of the constellation have level > 0>
s->vt[0x48](s, 1);                           // Skill::IncrementSkillLevel(1)
Character::SubtractDevotionPoint(p);
if (Skill::GetDevotionLevel(s) < 1) s->vt[0xa0](s);        // Skill::IncrementDevotionLevel()
if (!wasComplete && <now complete>)
    for each affinityGiven pair: Character::AddAffinity(p, type, amount);
if (Skill::GetSkillOperation(s) == 3) <offer the celestial-power picker, section 4>
```

**Reclaim one point from star X** (only legitimate with a devotion-reset item; the game has no
"reclaim anywhere" mode):

```
s = the star's Skill; p = GameEngine::GetMainPlayer(); sm = Character::GetSkillManager(p)
// gate: Skill::GetSkillLevel(s) > 0
//       no learned star links to this one
//       ComputeReclaimBlockers: subtracting this constellation's affinityGiven must not break any
//         other constellation whose affinityRequired is currently met
//       SkillManager::GetCurrentDevotionReclamationCost() <= Character::GetCurrentMoney()
//       SkillManager::GetDevotionReclamationAetherCost()  <= Player::GetCurrentAether()
wasComplete = <constellation complete>
if (!SkillManager::UseDevotionReclamationPoints(sm, 1)) return;    // this is the charge
s->vt[0x50](s, 0);                            // Skill::SetSkillLevel(0)
if (wasComplete && <no longer complete>)
    for each affinityGiven pair: Character::SubtractAffinity(p, type, amount);
if (Skill::GetSkillOperation(s) == 3) {
    host = ObjectManager::GetObject(Skill::GetDevotionParent(s));
    if (host) host->vt[0x5e0](host, nullptr, "", false);            // Skill::SetAutocastSkill(null,"",false)
    Skill::SetDevotionParent(s, 0);
}
Character::AddDevotionPoints(p, 1);
```

`Skill::GetDevotionLevel` / `GetDevotionMaxLevel` / `GetDevotionExperience`, `Character::GetDevotionPoints` /
`GetTotalDevotionPoints` / `GetMaxDevotionPoints` / `AddDevotionPoints` / `RemoveDevotionPoints` /
`SubtractDevotionPoint` / `GetAffinity` / `AddAffinity` / `SubtractAffinity`,
`SkillManager::GetNumDevotionPointsSpent` / `UseDevotionReclamationPoints` /
`GetCurrentDevotionReclamationCost` / `GetDevotionReclamationAetherCost`,
`Skill::Get/SetDevotionParent`, `Skill::GetConstellationDependencies` / `GetConstellationSelfLocked`,
`Skill::GetAffinityBonus` / `GetAffinityDependencies` (the per-SKILL versions, distinct from the
constellation's) and `GameEngine::GetAffinityBitmap` / `DevotionPointsInUse` are **all exported by name**.
`Skill::IncrementSkillLevel` / `SetSkillLevel` / `IncrementDevotionLevel` / `SetAutocastSkill` are exported
too, but the exe calls them virtually — use the vtable slots above so overrides run (same rule as
`Object::GetRTTIClassInfo` elsewhere in this codebase).

---

## 3. How the window opens and closes

- **The window pointer without any new RVA**: `GameEngine::GetUI()` (exported) returns
  **`InGameUI + 0x98`** (proved at exe+0x20ad4: `mov rax,[rdi+0x2f0]; lea rdx,[rax+0x98]; …
  GameEngine::SetUI`). Its `GameUIInterface` vtable is **exe+0x31a680**, and:
  - `vt[0xf0]` = exe+0x21a680 = `lea rax,[rcx+0x81308]; ret` -> **the devotion window** (0x98 + 0x81308 = 0x813a0)
  - `vt[0xf8]` = exe+0x21a690 = `lea rax,[rcx+0x3fb88]` -> the skills window (0x3fc20)
  - `vt[0x100]` = exe+0x21a6a0 -> the caravan/stash window (0x4fd08)
  - `vt[0x90]` = **exe+0x21aa10 = `DisplayDevotionWindow(unsigned int resetItemId)`**
  - `vt[0x60]` = exe+0x21a6f0, the target of the exported `GameEngine::DisplaySkillReallocationWindow`
  - `vt[0xe8]` = exe+0x21b060 -> **exe+0x18c9d0 = ResetAllDevotion** (below)
- **`GameUIInterface::DisplayDevotionWindow(itemId)`** (exe+0x21aa10, verified):
  - `itemId == 0` -> `CloseAllWindows` (exe+0x219750(InGameUI, true)) then `devotion->Show(true)`.
  - `itemId != 0` -> if `SkillManager::GetNumDevotionPointsSpent() == 0` it posts a `tagReclaimNoPoints`
    prompt and returns false; else CloseAllWindows, `devotion->+0x2419 = 1` + RefreshEligibility,
    `devotion->+0x2440 = itemId`, `Show(true)`. **This is the only writer of the reclaim byte.**
  - There is **no `GameEngine::DisplayDevotionWindow` export** — the mod must go through
    `GetUI()->vt[0x90]` (one new offset) or set `window+0x2419` itself.
- **`DevotionWindow::Show(bool)` = vtable `+0xb0` = exe+0x18afb0** (verified):
  - show: clear the search box, `+0x241b = 1`, zoom = 0.8f, pan reset to `+0x100/+0x104`,
    `GameEngine::UnlockTutorialPage(0x3b, true)`, tab bytes `+0x15fa = 0`, `+0x19aa = 0`, `+0x1d5a = 1`
    (the Devotion tab reads pressed), `RefreshEligibility`, `GameEngine::SetSaveEnabled(false)`.
  - hide: hide the search box, `+0x2b14 = 0`, `+0x2440 = 0`; if reclaim mode was on, clear `+0x2419` and
    refresh; if a reset item was set, call its `vt[0x960]` (consume it); `GameEngine::AutoSave()`.
- **Escape** = vtable `+0x68` = exe+0x18af40: sets `+0x2b10 = -1`; if visible, no pending confirm
  (`+0x2414 == 0`), and neither the skill picker nor the search box consumes it, it presses the
  `closeButton` through registry `+0x168`.
- **The Skill Window key (N)** — `InGameUI::HandleKeyAction` action 2 (exe+0x211980, decoded at
  exe+0x21122e): if the devotion window is visible it calls `devotion->Show(false)`; otherwise it presses the
  HUD skills button. So N is a *toggle-off* for devotion, never a toggle-on. Confirmed against the survey.
- **The Devotion tab of the skills window** (`skills+0x1cc8`) goes through
  **`SkillsWindow::SetTab(skillsWnd, int tab)` = exe+0x27c6f0** (tab 0/1 = the two masteries, **tab 2 =
  Devotion**; `skills+0x2630` is the current tab). It copies the two mastery tab captions and disabled bytes
  from the skills window into the devotion window's `+0x1378` / `+0x1728` buttons (proof:
  exe+0x27c78f uses `ui->vt[0xf0]` to get the devotion window and writes `+0x1378`, `+0x15f9`, `+0x15fc`,
  `+0x2b08`), and for tab 2 calls `ui->vt[0x90](ui, 0)` = DisplayDevotionWindow(0). For tabs 0/1 it shows the
  skills pane instead. **So yes: opening devotion hides the skills window** (CloseAllWindows runs first).
- **Going back** — the devotion window's own `skillsTab1Button` / `skillsTab2Button` land in
  `OnControlEvent` and call **`exe+0x18c360 = GoToSkillsTab(window, tab)`**:
  `SkillsWindow::SetTab(GetUI()->vt[0xf8](), tab)`, then in reclaim mode `+0x2b14 = 1` +
  `GameEngine::DisplaySkillReallocationWindow(gGameEngine, window->+0x2440)` (the reset item follows you to
  the skills window), else `ui->vt[0x68](ui, 0)`.
  Both are guarded by **`exe+0x18b110 = ConfirmPendingChanges(window, tab)`**: with `+0x241a` set and
  `+0x2b10 == -1` it posts the `tagConfirmSkillChanges` Yes/No dialog with **InterestedParty 0x16 (22)** and
  stores the wanted tab in `+0x2b10`; `Update` picks the answer up and calls `GoToSkillsTab` on "yes"
  (exe+0x188837).
- `helpButton` -> `GameEngine::ShowTutorialPage(0x3b)`. `closeButton` -> `Show(false)`.
  `zoomIn/zoomOut` -> exe+0x18ae50 with the max/min zoom constants. `clearSearchButton` -> clear
  `+0x241c`, clear the edit box (exe+0x1947e0), `+0x241b = 1`.
- **Reset ALL devotion** (the Tonic of Clarity's real effect): `GameUIInterface::vt[0xe8]` ->
  **exe+0x18c9d0 = `ResetAllDevotion(window)`** (verified): for every star, `SetSkillLevel(0)`, revoke the
  constellation affinity when it stops being complete, unbind any celestial power
  (`SetAutocastSkill(null,"",false)` + `SetDevotionParent(0)`) and reload the icon; then recompute every
  constellation's `+0x30/+0x31/+0x32`; then
  `Character::AddDevotionPoints(p, min(total - current, total))`.
  `Game.dll!ItemDevotionReset::Use` (Game.dll+0x322130) is what reaches it, gated on
  `player+0x1770 < player+0x1774` (spent < total).

---

## 4. Celestial powers: where a proc star gets bound to a skill

This is **not** in the skills window and **not** a drag. It is a modal picker owned by the devotion window.

### 4.1 The picker object

`window+0x25d0`, size **0x4e0**, ctor exe+0x1d4f90, vtable **exe+0x318448**. Framework-B slots:
`+0x18` SetRecord exe+0x1d5260, `+0x38` HandleMouseEvent exe+0x1d58c0, `+0xb0` Show exe+0x1d5f60,
`+0xb8` IsVisible exe+0x10d5f0, `+0xe8` **Populate = exe+0x1d4550**, `+0xf0` **AddCandidates = exe+0x1d4af0**.
Its record is the mastertable's `skillSelect` field (loaded into the string at `+0x498`, i.e. window+0x2a68).

Offsets (verified):
- `+0x450/+0x458/+0x460` = the row groups (stride 0x28; each group has its own `std::vector<Row*>` at
  group `+0x08/+0x10`, its column count at group `+0x20` and a height at `+0x24`)
- `+0x468` group count, `+0x46c` selected group, `+0x470` widest row count, `+0x474/+0x47c/+0x480` layout,
  `+0x48c/+0x490` paddings, `+0x3f0/+0x3f4` computed size
- `+0x498` `std::string` the row record path
- **`+0x4b8` = the player object id**
- **`+0x4d0` = the star's CURRENT host skill id** (excluded from the list) — this is `window+0x2aa0`
- **`+0x4d4` = the devotion star's own skill id** — this is `window+0x2aa4`
- **`+0x4c8` = the listener** (`= window+0x98`, written by the devotion ctor at exe+0x185b96)
- Row objects are 0x228 bytes (ctor exe+0x1e4020(row, HotSlotOptionSkill*)) with `+0x30` = the skill
  `Object*`, `+0x1b9` disabled, `+0x1ba` "this is the empty/unassign row", `+0x1bb` "this skill already
  has a devotion power", `+0x1bc` an extra flag from `Skill+0x2f9`.

### 4.2 How it enumerates candidates (exe+0x1d4550 Populate -> exe+0x1d4af0 AddCandidates, verified)

Populate adds a leading row with `+0x1ba = 1` (the "none" row; it also copies `Skill+0x2f9` from the current
host into `+0x1bc`), then calls `AddCandidates` three times:

- `Character::GetSkillList(player)` with group-by-mastery = true
- `Character::GetItemSkillList(player)` with grouping = false
- `Character::GetItemSkillCache(player)` with grouping = false

`AddCandidates` keeps a skill S only if **all** of:
1. `Object::GetObjectId(S) != picker->+0x4d0` (not the skill it is already on)
2. `!Skill::IsSkillTheMasterySkill(S)`
3. `!SkillManager::IsDefaultSkill(sm, id)`
4. `!Skill::IsSkillModifier(S)`
5. `!Skill::HasAutocastInDbr(S)`
6. `S->+0x2fa == 0` (a byte; **inferred**: "this skill is itself an autocast/devotion skill")
7. `Skill::GetSkillOperation(S) == 0` (a plain activatable skill)
8. `Skill::IsSkillA(devotionSkill, S->GetRTTIClassInfo())` — the devotion power's own class filter
9. `!Skill::IsSkillBlackListed(devotionSkill, Object::GetObjectName(S))` — the power's `.dbr` blacklist of
   record paths
10. when grouping: `Skill::GetMasteryId(S)` must be one of `SkillManager::GetSkillMasteryIds(sm, out)`

Row flags: `Skill::GetSkillState(S)` with `*(int*)ret == 0` -> `+0x1b9` (greyed);
else `Skill::HasAutocastSkill(S)` -> `+0x1bb` (already carries a power).
All of 1..10 are exported calls, so **a mod can rebuild the candidate list exactly, with no picker.**

### 4.3 The pick (exe+0x1d5cd0, verified)

A row whose skill has `GetSkillLevel != 0` fires
`listener->vt[0](listener, Object::GetObjectId(row->+0x30))`, i.e. **exe+0x186620** on `window+0x98`.
The "none" row calls `listener->vt[0](listener, 0)`.

### 4.4 `exe+0x186620 = OnSkillPicked(window+0x98, unsigned int chosenSkillId)` (verified)

```
star = window->+0x2ab8;  if (!star || chosenSkillId == star->+0x10c) return
devSkill = GetObject(star->+0x108); chosen = GetObject(chosenSkillId)
if (chosen && Skill::HasAutocastSkill(chosen)) {
    window->+0x2414 = chosenSkillId
    DialogManager::AddDialog(dm, 1, 0, 0x19, "tagDevotionConfirm", 0, true, "", "", true)   // replace?
} else
    exe+0x1867f0(window, chosenSkillId)
```

`Update` (exe+0x18879c) reads `DialogManager::GetResponseFor(0x19)`; on **yes** it runs
`exe+0x1869b0(window, id)` (find whatever star currently owns that skill and detach it) **then**
`exe+0x1867f0(window, id)`; on no it does nothing; either way `+0x2414 = 0`.
**InterestedParty 0x19 (25) = the devotion "replace power" prompt; 0x16 (22) = the leave-with-changes prompt.**

### 4.5 `exe+0x1867f0 = BindCelestialPower(window, unsigned int hostSkillId)` — the actual call (verified)

```
star = window->+0x2ab8; if (!star || hostSkillId == star->+0x10c) return
devSkill = ObjectManager::GetObject(star->+0x108); if (!devSkill) return
name = Skill::GetTemplateAutoCast(devSkill)                    // std::string, hidden-pointer return
old  = ObjectManager::GetObject(star->+0x10c)
if (old) { old->vt[0x5e0](old, nullptr, "", false); devSkill->+0x1d0 = 0; }   // unbind + SetDevotionParent(0)
exe+0x17eec0(star, hostSkillId)                                // star->+0x10c = id, reload the star icon
newHost = ObjectManager::GetObject(hostSkillId)
if (newHost && !name.empty()) {
    newHost->vt[0x5e0](newHost, devSkill, name, false);        // Skill::SetAutocastSkill
    devSkill->+0x1d0 = Object::GetObjectId(newHost);           // Skill::SetDevotionParent(hostId)
}
window->+0x2ab8 = 0
```

So the mod-callable sequence is exactly:

```
name = Skill::GetTemplateAutoCast(devotionSkill)               // "" -> the power is not assignable
old  = ObjectManager::GetObject(Skill::GetDevotionParent(devotionSkill))
if (old) { old->vt[0x5e0](old, nullptr, emptyStdString, false); Skill::SetDevotionParent(devotionSkill, 0); }
newHost->vt[0x5e0](newHost, devotionSkill, name, false)        // Skill::SetAutocastSkill(Skill*, string, bool)
Skill::SetDevotionParent(devotionSkill, Object::GetObjectId(newHost))
```

Signatures (from the export listing, verified):
`public: virtual void Skill::SetAutocastSkill(Skill*, std::string const&, bool)`,
`public: void Skill::SetDevotionParent(unsigned int)` (`Skill+0x1d0`, non-virtual),
`public: unsigned int const Skill::GetDevotionParent(void) const` (`mov eax,[rcx+0x1d0]; ret`),
`public: std::string Skill::GetTemplateAutoCast(void) const`,
`public: std::string Skill::GetAutoCastControllerName(void) const`,
`public: Skill* Skill::GetAutoCastSkill(void) const` (`mov rax,[rcx+0x3b8]; ret`),
`public: bool Skill::HasAutocastSkill(void) const`, `Skill::HasAutocastInDbr`.
Inside `SetAutocastSkill` (Game.dll+0x479650, read): it detaches whatever was at `this+0x3b8`, stores the new
devotion skill there, copies the controller name to `this+0x398`, parses it with
`SkillAutoCastController::GetParams` into `attached+0x320/+0x324`, and sets `attached+0x2fb/+0x2fd = 1`.

### 4.6 Reading an assignment back

- **Which skill a power sits on**: `Skill::GetDevotionParent(devotionSkill)` -> an object id (0 = unassigned).
  The exe mirrors it in `Star+0x10c`.
- **Which power a skill carries**: `Skill::HasAutocastSkill(hostSkill)` /
  `Skill::GetAutoCastSkill(hostSkill)` -> `Skill*` (`hostSkill+0x3b8`).
- The exe re-validates the pair every frame in **`exe+0x17dfc0 = Star::ValidateAutocastBinding(star)`**
  (run from `Star::UpdateState` when `star+0x138` is set): if
  `Skill::GetAutoCastControllerName(host) != Skill::GetTemplateAutoCast(devSkill)` or the is-a test
  `Skill::IsSkillA(devSkill, host->GetRTTIClassInfo())` fails, it clears the binding
  (`host->SetAutocastSkill(null, "", false)`, `devSkill+0x1d0 = 0`).
- The **tooltip** naming the host is produced by `GenerateUIDevotionText`'s second argument (section 5.1).

---

## 5. Text a mod can call

### 5.1 A star — `exe+0x17f0d0 = Star::BuildRollover(star, out)` (star vtable slot 0), verified

```
devSkill  = ObjectManager::GetObject(star->+0x108)          // the devotion star's Skill
hostSkill = ObjectManager::GetObject(star->+0x10c)          // may be null
mem::vector<GameTextLine> lines = {0,0,0}
sm = Character::GetSkillManager(GameEngine::GetMainPlayer())
ironCost   = SkillManager::GetCurrentDevotionReclamationCost(sm)
aetherCost = SkillManager::GetDevotionReclamationAetherCost(sm)
GameEngine::GenerateUIDevotionText(
    devSkill,                       // rcx  Skill const*   the star
    hostSkill,                      // rdx  Skill const*   the skill its power is bound to (may be null)
    lines,                          // r8   mem::vector<GameTextLine>&
    (SkillReasons const*)(star + 0x160),   // r9
    false,                          // [rsp+0x20] bool
    window->+0x2419,                // [rsp+0x28] bool  reclaim mode
    ironCost,                       // [rsp+0x30] int
    aetherCost,                     // [rsp+0x38] int
    0x31);                          // [rsp+0x40] GameTextClass
GameTextLineToString(lines, &out->+0x60)
```

That matches the exported signature exactly:
`static void GameEngine::GenerateUIDevotionText(Skill const*, Skill const*, mem::vector<GameTextLine>&,
SkillReasons const*, bool, bool, int, int, GameTextClass)`.

**`SkillReasons` here is the 14-byte block at `Star+0x160..+0x16d`**, filled by `Star::UpdateState`
(exe+0x17de10) as:
- `+0x00` (`star+0x160`): the star is unlearned **and** `Character::GetDevotionPoints() == 0`
- `+0x02` (`+0x162`): copy of `star+0x134` (link requirement unmet)
- `+0x03` (`+0x163`): `Skill::GetDevotionLevel >= Skill::GetDevotionMaxLevel` (maxed)
- `+0x08` (`+0x168`): `GetCurrentDevotionReclamationCost() > Character::GetCurrentMoney()`
- `+0x09` (`+0x169`): the constellation's affinity requirement is met (passed in by the caller)
- `+0x0a` (`+0x16a`): copy of `star+0x134` again
- `+0x0b` (`+0x16b`): `Skill::GetSkillLevel(s) == 0`
- `+0x0c` (`+0x16c`): copy of `star+0x135` (affinity requirement unmet)
- `+0x01`, `+0x04..+0x07`, `+0x0d` are never written (zeroed by the ctor)

A mod that only wants the text can build the same 14 bytes itself from exported calls and pass
`hostSkill = ObjectManager::GetObject(Skill::GetDevotionParent(devSkill))`.

### 5.2 A constellation — `exe+0x180fd0 = Constellation::BuildRollover(c, event, origin, zoom)`, verified

Returns true when the mouse is inside the constellation's background rect, and leaves a
`mem::vector<GameTextLine>` at `c+0x1e0` (pointed to by `c+0x218`). It is composed by hand out of exported
calls, so a mod can reproduce it with **no exe RVA at all**:

1. `LocalizationManager::LocalizeWithoutParams(c->+0x38 /*constellationDisplayTag*/)` in style
   `records/ui/styles/text/style_rollover_title.dbr` — **the constellation's name**.
2. `LocalizeWithoutParams(c->+0x58 /*constellationInfoTag*/)` in `style_rollover_description.dbr`.
3. If `affinityRequired` is non-empty: the header `"FailedDevotionDependency"` (localized), rendered in
   `style_nooutline_textred_sizen_bold.dbr` when `c->+0x30 == 0` (requirement unmet) and in
   `style_rollover_title.dbr` when met; then one line per pair — the amount, `GameEngine::GetAffinityBitmap(type)`
   as the line's inline icon, red style when `Character::GetAffinity(player, type) < amount`.
4. If `affinityGiven` is non-empty: the header `"tagDevotionAffinityBonus"`, then one line per pair
   (amount + affinity bitmap).

The window title is built in Render at exe+0x182cd9:
`LocalizationManager::Localize("tagDevotionJournalTitle", Character::GetTotalDevotionPoints(),
Character::GetMaxDevotionPoints())`.

### 5.3 The info panel `window+0x2408` (verified, exe+0x18efa0..0x18f3f0)

- `panel+0x850` text = `"DevotionPointsAvailableSingular"` (n == 1), `"DevotionPointsAvailable"` (n > 1) or
  `"NoDevotionPointsAvailable"`, from `Character::GetDevotionPoints(player)`. `panel+0x948` is its ColorPulse,
  enabled (`panel+0x8c0`) while points > 0.
- `panel+0x758` text = `Localize("DevotionPointsTotal", Character::GetTotalDevotionPoints(),
  Character::GetMaxDevotionPoints())`.
- Five affinity gauges at `panel+0xb00`, `+0xd48`, `+0xf90`, `+0x11d8`, `+0x1420` (stride 0x248), fed
  `Character::GetAffinity(player, t)` for **t = 0,1,2,3,4 in that order** — so gauge 1 = ascendant,
  2 = chaos, 3 = eldritch, 4 = order, 5 = primordial, matching the mastertable's
  `affinity01..05Bitmap/Number/Rollover` fields. Their names/tooltips come from
  `records/ui/skills/devotion/affinity_0Nrollover.dbr`, not from any export.
- In reclaim mode it additionally shows `panel+0x1760` = `Character::GetCurrentMoney()` and
  `panel+0x19b0` = `Player::GetCurrentAether()`.

---

## 6. The search box

- The edit control is `window+0x1e88` (its text buffer at `window+0x1ef8`). Typing sets `window+0x241b`.
- `Update` (exe+0x188891..0x1888db) lower-cases the query per locale
  (`LocalizationManager::GetLocale()` + `_towlower_l`) into `window+0x2420`, sets `window+0x241c` when the
  query is non-empty, and then walks every star.
- Per star, **`exe+0x17f440 = Star::BuildSearchStrings(star)`** (star vtable `+0x80`) fills
  `star+0x148` (a `std::vector<u16string>`) by calling
  **`GameEngine::GenerateUIDevotionSearchText(devSkill, lines, /*GameTextClass*/ 0x31)`** (exe+0x17f4a7 —
  the third argument is set with `lea r8d,[r13+0x31]` just before) and then, per `GameTextLine`,
  `LocalizationManager::LocalizerFormatStrip(instance, &line.text, &out)` to drop the colour codes,
  followed by a per-character `_towlower_l`.
- A star that does not match is drawn dimmed; matching is a substring test over that cached vector
  (the comparison itself is in Render, not read in detail — **inferred**).
- `clearSearchButton` (`window+0x20b8`) clears both the edit box and `window+0x241c`.

The upshot for a mod: `GenerateUIDevotionSearchText(skill, out, 0x31)` gives you a ready-made
"everything this star is about" text block (name + its bonus lines), which is a better search corpus than
the tooltip.

---

## 7. Byte signatures (first 16 bytes, for an `exe_ui::available()`-style check)

```
exe+0x185640 DevotionWindow::ctor                48 89 4c 24 08 55 56 57 41 54 41 55 41 56 41 57
exe+0x186cf0 DevotionWindow::SetRecord           48 8b c4 48 89 50 10 48 89 48 08 55 53 56 57 41
exe+0x188700 DevotionWindow::Update              89 54 24 10 55 53 56 57 41 54 41 55 41 56 41 57
exe+0x189450 DevotionWindow::Render              48 8b c4 f3 0f 11 58 20 48 89 50 10 55 56 57 41
exe+0x18a820 DevotionWindow::HandleMouseEvent    48 89 6c 24 20 56 57 41 56 48 81 ec 90 00 00 00
exe+0x18afb0 DevotionWindow::Show(bool)          48 89 5c 24 08 48 89 74 24 10 57 48 83 ec 20 0f
exe+0x18af40 DevotionWindow::HandleEscape        40 53 48 83 ec 20 80 79 68 00 48 8b d9 c7 81 10
exe+0x184db0 DevotionWindow::RefreshEligibility  48 89 4c 24 08 53 55 56 57 41 54 41 55 41 56 41
exe+0x185240 DevotionWindow::HandleStarMouse     48 8b c4 4c 89 48 20 4c 89 40 18 48 89 50 10 48
exe+0x18bd50 DevotionWindow::OnControlEvent      48 8b c4 57 41 54 41 55 41 56 41 57 48 81 ec 80
exe+0x186620 DevotionWindow::OnSkillPicked       48 8b c4 57 48 81 ec c0 00 00 00 48 c7 40 88 fe
exe+0x1867f0 DevotionWindow::BindCelestialPower  48 8b c4 41 56 48 83 ec 70 48 c7 40 a8 fe ff ff
exe+0x18c360 DevotionWindow::GoToSkillsTab       83 fa 02 0f 87 86 00 00 00 48 89 5c 24 08 57 48
exe+0x18c3f0 DevotionWindow::ComputeReclaimBlock 48 8b c4 4c 89 48 20 48 89 50 10 48 89 48 08 55
exe+0x18c9d0 DevotionWindow::ResetAllDevotion    48 8b c4 57 41 54 41 55 41 56 41 57 48 83 ec 60
exe+0x18b110 DevotionWindow::ConfirmPending      (see exe_dis; guarded by +0x241a / +0x2b10)
exe+0x17d5a0 Star::ctor                          45 33 c9 c7 41 10 00 00 00 00 66 c7 41 14 00 00
exe+0x17da10 Star::SetRecord                     48 8b c4 55 48 8b ec 48 83 ec 70 48 c7 45 b0 fe
exe+0x17de10 Star::UpdateState                   48 89 74 24 18 48 89 7c 24 20 41 56 48 83 ec 20
exe+0x17dfc0 Star::ValidateAutocastBinding       48 8b c4 56 57 41 56 48 83 ec 70 48 c7 40 98 fe
exe+0x17ea10 Star::HandleMouseEvent              48 8b c4 55 56 57 41 54 41 55 41 56 41 57 48 81
exe+0x17eec0 Star::SetHostSkillId                48 89 5c 24 08 48 89 74 24 10 57 48 83 ec 20 48
exe+0x17f0d0 Star::BuildRollover                 48 8b c4 41 54 41 56 41 57 48 81 ec 90 00 00 00
exe+0x17f440 Star::BuildSearchStrings            40 55 56 57 41 54 41 55 41 56 41 57 48 8b ec 48
exe+0x17f7c0 Constellation::ctor                 c7 41 10 00 00 00 00 48 8d 05 e2 62 19 00 48 89
exe+0x17fe80 Constellation::SetRecord            48 8b c4 55 57 41 54 41 56 41 57 48 8b ec 48 83
exe+0x180fd0 Constellation::BuildRollover        48 8b c4 55 41 54 41 55 41 56 41 57 48 8d 68 98
exe+0x1815e0 Constellation::IsAffinityMet        40 55 48 83 ec 30 48 8b e9 84 d2 75 0a 0f b6 41
exe+0x181690 Constellation::IsComplete           40 53 55 41 56 48 83 ec 20 48 8b 59 78 4c 8b f1
exe+0x181790 Constellation::HasAnyStarLearned    48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 18 57
exe+0x181870 Constellation::GrantAffinity        48 89 6c 24 18 57 48 83 ec 20 80 79 32 00 0f b6
exe+0x181910 Constellation::RevokeAffinity       48 89 74 24 10 57 48 83 ec 20 48 8b f9 48 8b 0d
exe+0x1d4550 SkillSelect::Populate               40 55 56 57 48 83 ec 30 48 c7 44 24 20 fe ff ff
exe+0x1d4af0 SkillSelect::AddCandidates          4c 89 4c 24 20 44 88 44 24 18 48 89 54 24 10 53
exe+0x1d4f90 SkillSelect::ctor                   48 89 4c 24 08 57 48 83 ec 30 48 c7 44 24 20 fe
exe+0x21aa10 GameUI::DisplayDevotionWindow       40 55 56 57 48 81 ec c0 00 00 00 48 c7 44 24 50
exe+0x27c6f0 SkillsWindow::SetTab                83 fa 03 0f 83 59 02 00 00 48 8b c4 41 54 41 56
```

Vtables touched: exe+0x315d80 (DevotionWindow), exe+0x315d68/0x315d70/0x315d78 (its three sub-listeners),
exe+0x315ab0 (Constellation), exe+0x315a28 (Star), exe+0x318448 (SkillSelect), exe+0x31a680
(`GameUIInterface`, i.e. `[InGameUI+0x98]`).

---

## 8. Recommended mod path (minimum new RVAs)

1. **Read the whole tree with exactly two RVAs.** `InGameUI+0x813a0` (already known) gives the window;
   `window+0xa8/+0xb0` is the constellation vector; `c+0x38`/`c+0x58` are the name/description tags;
   `c+0x90`/`c+0xa8` are the affinity require/give pairs; `c+0x78` is the star vector; `star+0x108` is the
   Skill id; `star+0x118` the 1-based link indices; `star+0x134/+0x135` the two blocked flags;
   `star+0x160` the SkillReasons for the tooltip. **No screen space, no clicks.** All the offsets above are
   plain member reads, in the same risk class as the other window layouts in `exe-ui-layout.md`.
2. **Speak a star** with `GameEngine::GenerateUIDevotionText` exactly as section 5.1 passes it
   (`hostSkill` from `Skill::GetDevotionParent`) and a constellation with the four exported pieces in 5.2 —
   no exe function needed for either.
3. **Act** with the export sequences in 2.7 rather than by driving the window: the window applies everything
   immediately anyway, so the only thing the mod must reproduce is the gate (points / affinity / links /
   maxed) and the affinity grant-revoke when a constellation flips complete. Calling
   `exe+0x184db0(window)` after each change keeps the game's own `star+0x134/+0x135` and the rollovers
   correct for free — worth one RVA.
4. **Open/close** through `InGameUI::HandleKeyAction` where possible; the only true opener is
   `GameEngine::GetUI()->vt[0x90](ui, 0)` (`GetUI` is exported, so the cost is one vtable index, not an RVA).
   Closing is `window->vt[0xb0](false)`, which is the generic framework-B `Show`.
5. **Celestial powers**: build the candidate list from the ten exported predicates in 4.2 and bind with the
   four-call sequence in 4.5. The picker window itself never has to be shown, and doing it this way sidesteps
   the `tagDevotionConfirm` modal entirely (do the detach yourself).
6. **Reclaim** only when `window+0x2419` is set (a devotion-reset item is in play), mirroring
   `screens/skills.cpp`'s spirit-guide rule; outside that the game charges nothing and offers nothing.

## 9. Open / not resolved

- `Skill+0x2fa` (candidate filter step 6) and `Skill+0x2f9` (row flag `+0x1bc`) were not traced to an
  accessor; both are **inferred**.
- (`Skill_Operation` and the one-point-per-star question were resolved by the Game.dll note; see 2.2.)
- `Render` (exe+0x189450) was only skimmed: the exact substring test used by the search filter, the
  connection-line drawing and the `nebulaSections` art were not read.
- The info panel's ctor (exe+0x18ce90) record fields were not enumerated; the offsets in 5.3 come from its
  update path, not from the loader.
- `GameUIInterface` vtable slots other than `+0x60`, `+0x90`, `+0xe8`, `+0xf0`, `+0xf8`, `+0x100` are
  unexamined.
- `exe+0x18c3f0`'s tail (the actual push of each blocked constellation's display tag and the return value)
  was read in outline only; the affinity-subtraction and per-constellation re-check loops are verified,
  the string push is **inferred** from `Skill::SetConstellationDependencies` taking
  `mem::vector<std::string>` and the getter being what the tooltip reads.
