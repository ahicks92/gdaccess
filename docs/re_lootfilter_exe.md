# The Loot Filter window: the exe side (Grim Dawn v1.3.0.8 x64, exe timestamp 0x6a85fbec, image 0x482000)

Static RE on `build/GrimDawn.unpacked.bin` with `tools/exe_dis.py` / `tools/arz.py` / `tools/arc_unpack.py`,
2026-08-29. **Nothing here has been exercised live** (one live screenshot of the window was supplied by the
user and agrees with the layout derived below). All addresses are RVAs (`exe+...`); "verified" = read in
disassembly, everything else is marked **inferred**. Companion to `docs/exe-ui-layout.md` (frameworks A/B),
`docs/ingame-ui-survey.md` (whose "Loot filter (`+0xab410`) -- MEDIUM" entry this supersedes) and
`docs/re_devotion_exe.md` (the format this follows).

Anchors: loot filter window = `InGameUI + 0xab410`, ctor exe+0x1c7c30, vtable exe+0x317640, size **0xd88**. `InGameUI = [[main_obj+0x90]+0x2f0]`, `main_obj = [exe+0x3ceef8]`.
Proof of the offset: `InGameUI`'s ctor at exe+0x20686c does `lea rcx,[rdi+0xab410]; call exe+0x1c7c30`.

**Headline**: the window is a thin skin over `Player::GetLootFilter` / `SetLootFilter` / `SetLootFilterDefaults`
(all exported). It builds **42 check boxes** (41 without Forgotten Gods) into a `std::map<CheckBox*, int>` at
`window+0xd58`, refreshes every one from the player on `Show(true)`, and writes one option back on every click.
There is no deferred commit and no per-option label anywhere but the code — the option -> caption-tag table in
section 3 is the only place the mapping exists, and it is **not** a straight `tagLootFilterNN -> N-1`.

---

## 1. The window class

- `LootFilterWindow` — `InGameUI+0xab410`, size **0xd88**, ctor exe+0x1c7c30, primary vtable exe+0x317640.
  Secondary vtables written by the ctor: `+0x08` -> exe+0x317638 (slot0 exe+0x1c9bf0, a `this-8` thunk),
  `+0x90` -> exe+0x317740 (slot0 **exe+0x1c99b0 = OnControlEvent**).
- Deleting destructor exe+0x1c7ee0 (`mov edx, 0xd88` gives the size); the plain dtor exe+0x1c7f20 walks the
  check-box map and deletes every check box (`node->+0x20 -> vt[0x10](obj, 1)`).

### 1.1 Vtable slots used (exe+0x317640), verified

| slot | RVA | what |
|---|---|---|
| `+0x10` | exe+0x1c7ee0 | deleting destructor |
| `+0x18` | exe+0x1c8550 | **SetRecord(std::string const&)** — builds every control (section 2/3) |
| `+0x20` | exe+0x1c9460 | Render |
| `+0x38` | exe+0x1c9790 | HandleMouseEvent |
| `+0x48` | exe+0x1c9640 | **Update** — the reset-confirm poll (section 5) |
| `+0x58` | exe+0x32340 | HandleKeyEvent (shared `return false` stub) |
| `+0x68` | exe+0x1c9970 | **Escape** — presses the close button |
| `+0xb0` | exe+0x1c9860 | **Show(bool)** — refreshes the check boxes from the player (section 5) |
| `+0xb8` | exe+0x10d5f0 | IsVisible (the generic one: returns `+0x68`) |
| `+0xe8` | exe+0x139980 | **SetPlayerId(uint)** = `mov [rcx+0x98], edx` |
| `+0xf0` | exe+0x1c8180 | **AddOption** (the check-box factory; NOT OnControlEvent on this class) |
| `+0xf8` | exe+0x1c83d0 | **AddSpacer** |

Note the deviation from the usual framework-B convention: on most windows `+0xf0` is `OnControlEvent`.
Here `OnControlEvent` is only reachable through the listener sub-object at `window+0x90`
(vtable exe+0x317740, slot 0 = exe+0x1c99b0), and `+0xf0` is this window's own `AddOption`.

### 1.2 Member offsets (verified from the ctor exe+0x1c7c30 and SetRecord exe+0x1c8550)

Framework-B base:
- `+0x28` base-control visible byte (1 in the ctor), `+0x30` host pointer, `+0x38` parent,
  `+0x40..+0x4c` float rect (x, y, w, h), `+0x50/+0x54` the record's `windowDefaultX/Y`,
  **`+0x68` = the window's own visible byte** (what `IsVisible` returns, what `Show` writes),
  `+0x69` = "the mouse is inside" latch (written by HandleMouseEvent),
  **`+0x90` = the listener sub-object** (its `vt[0]` = OnControlEvent),
  **`+0x98` = the player object id** (dword; written by `InGameUI` at exe+0x218c84 via
  `window->vt[0xe8](InGameUI+0xa0)`; every handler resolves the player with
  `ObjectManager::GetObject(window+0x98)`).

Controls (all by-value members unless noted):
- `+0xa8` — the window background image (0x60 bytes, vtable exe+0x313860; record `lootFilterBitmap`).
- **`+0x108` = registry A** (vtable exe+0x312cf0; `PressChild` = exe+0x12ac80, the full 0/1/2 click sequence).
  Holds the **close** and **reset** buttons.
- **`+0x150` = registry B** (vtable exe+0x3148c0; `PressChild` = exe+0x12b680, the **toggle** variant, one
  event per press). Holds **every check box**.
- `+0x190` — **closeButton** (Button, ctor exe+0x10aee0, record `lootFilterCloseButton`).
- `+0x4c8` — **resetButton** (TextButton, ctor exe+0x126fe0, record `lootFilterResetButton`,
  `textTag = tagLootFilterReset` = "Defaults").
- `+0x878` — window title text element (record `lootFilterWindowTitle`, `textTag = tagLootFilterWindowTitle`
  = "Loot Filter").
- `+0x970 / +0xa68 / +0xb60 / +0xc58` — the four **column headers** (records `lootFilterTitle01..04`,
  tags `tagLootFilterTitle01..04` = **Quality / Type / Damage / Character**).
  All five text elements are the same class (0xf8 bytes, ctor exe+0x2595c0, vtable exe+0x31c590): the u16
  caption is at `element+0x40`, the style name at `+0x88`, a second `std::string` at `+0xa8`. Same layout as
  the other framework-B text elements (`WidgetB::text` reads `+0x40`).

State built by `SetRecord`:
- **`+0xd50` int = the running Y cursor** for the column being laid out (reset to 0 at the top of columns 2,
  3 and 4; `AddOption`/`AddSpacer` add their row spacing to it).
- **`+0xd58 / +0xd60` = `std::map<CheckBox*, int>`** — key = the check box, value = **the
  `LootFilterOption` enum value**. `_Myhead` at `+0xd58`, size at `+0xd60`; node value pair is
  `{Control* at node+0x20, int at node+0x28}`. This is the whole option table at runtime.
- **`+0xd68 / +0xd70 / +0xd78` = `std::vector<Image*>`** — the horizontal separator images (record
  `lootFilterSpacer`), purely decorative.
- **`+0xd80` byte = "the Defaults confirm dialog is pending"**. Set by `OnControlEvent` when the reset
  button fires event 2; cleared by `Update` when the answer arrives and by `Show(false)`. While it is set,
  `OnControlEvent` and Escape are dead (every control event returns immediately).

---

## 2. The control factories

### 2.1 `exe+0x1c8180 = LootFilterWindow::AddOption(...)` (vtable `+0xf0`), verified

```
void AddOption(this,                       // rcx
               int option,                 // edx   the LootFilterOption enum value
               std::string const& record,  // r8    always the mastertable's lootFilterCheckbox
               char const* captionTag,     // r9    "tagLootFilterNN"
               char const* infoTag,        // [rsp+0x20]  "tagLootFilterNNInfo"
               int xIndent,                // [rsp+0x28]  0 / ColumnIndent2 / 3 / 4, x UI scale
               int ySpacing)               // [rsp+0x30]  RowSpacing (36) or RowSpacing2 (30), x UI scale
{
    cb = new CheckBox(0x3b8 bytes);                  // exe+0x2ae44c alloc, TextButton ctor exe+0x126fe0
    cb->vptr  = exe+0x313b18;  cb->vptr2 = exe+0x313bf0;   // the CheckBox class (see 2.2)
    cb->+0x3b0 = 2; cb->+0x3b4 = 2;
    cb->vt[0x18](record);                            // SetRecord -> the bitmaps of lootFilter_checkbox.dbr
    cb->vt[0xc8](std::string(captionTag));           // SetTextTag: tag -> cb+0x338, localized u16 -> cb+0x358
    cb->vt[0xb0](std::string(infoTag));              // SetRolloverTag: tag -> cb+0x318
    pos = { cb->+0x260 + xIndent, cb->+0x264 + this->+0xd50 };
    cb->vt[0xb8](&pos, true);                        // SetPosition
    exe+0x12a800(this+0x150, cb, this+0x90);         // register with registry B, listener = window+0x90
    map[this+0xd58][cb] = option;
    this->+0xd50 += ySpacing;
}
```

`cb->+0x260/+0x264` come from the record's `bitmapPositionX/Y` (30, 125), so every check box's position is
`(30 + columnIndent, 125 + runningY)` in window space.

### 2.2 The check-box class (verified)

A `TextButton` subclass: **vtable exe+0x313b18** (primary, 27 slots) + exe+0x313bf0 (secondary), size
**0x3b8**. It differs from the plain TextButton (vtable exe+0x313ce8) only in the destructor, `SetRecord`
(`+0x18` = exe+0x128120, which loads the five check-box bitmap states), `Render`, `HandleMouseEvent`
(`+0x38` = exe+0x127f90) and `+0x70`. The same class is used by the caravan/stash window (xref exe+0x1346e8).

Inherited TextButton layout, all confirmed in use here:
- **`+0x281` byte = disabled** (never set on these check boxes)
- **`+0x282` byte = CHECKED** — this is the check-box state. `Show`/`Update` write `Player::GetLootFilter`
  into it, `OnControlEvent` reads it back and passes it to `Player::SetLootFilter`.
- `+0x283` byte = rollover / hovered (written by exe+0x127f90)
- `+0x280` byte = `isCircular` (False in the record, so the hit test is the plain rect)
- `+0x260..+0x26c` float rect, `+0x270`/`+0x278` the up/down SoundPaks
- **`+0x318` `std::string` = the rollover/tooltip TAG** (`tagLootFilterNNInfo`), set by `vt[0xb0]` = exe+0x126c00
- **`+0x338` `std::string` = the caption TAG** (`tagLootFilterNN`), **`+0x358` u16 = the localized caption**,
  both set by `vt[0xc8]` = exe+0x1271d0 (`LocalizationManager::LocalizeWithoutParams(tag) -> +0x358`)

`CheckBox::HandleMouseEvent` (exe+0x127f90) does **hit-testing only** — it sets `+0x283` and reports itself
as the hovered control. The state flip lives in the registry (section 6).

### 2.3 `exe+0x1c83d0 = LootFilterWindow::AddSpacer(record, xIndent, ySpacing)` (vtable `+0xf8`), verified

Allocates a 0x60-byte image control (vtable exe+0x313860, the same class as the window background), sets its
record (`lootFilterSpacer` = `ui/lootfilter/lootfilter_linespacer.tex`), positions it at
`(imgX + xIndent, imgY + window->+0xd50)`, pushes it onto the vector at `window+0xd68`, and adds `ySpacing`
to `window+0xd50`. Decoration only — no listener, no state.

---

## 3. Every control the window builds

Read from `SetRecord` (exe+0x1c8550) in construction order, which is exactly the on-screen order:
four columns, top to bottom, left to right. `window+0xd50` is zeroed at the start of columns 2, 3 and 4
(exe+0x1c8d30, exe+0x1c8f56, exe+0x1c9190), which is what makes them columns.

Record constants (`records/ui/lootfilter/lootfilter_mastertable.dbr`, `tools/arz.py`):
`lootFilterColumnIndent2 = 244`, `Indent3 = 488`, `Indent4 = 732`, `lootFilterRowSpacing = 36`,
`lootFilterRowSpacing2 = 30` (the tighter spacing used on the row *before* a separator). All are multiplied
by `GraphicsEngine::GetUIScaleFactor()`.

### 3.1 The option table (VERIFIED — the `mov edx, N` at each `AddOption` call)

Legend: **opt** = the `LootFilterOption` enum value passed to `Player::Get/SetLootFilter` (and the bit index
in the player's bitset at `Player+0x4c00`); *sep* = a separator image, not a control.

**Column 1 — `tagLootFilterTitle01` = "Quality"** (header `window+0x970`, indent 0)

| # | opt | caption tag | English | info tag |
|---|---|---|---|---|
| 1 | **0** | tagLootFilter01 | Common | tagLootFilter01Info |
| 2 | **1** | tagLootFilter02 | Magic | tagLootFilter02Info |
| 3 | **2** | tagLootFilter03 | Rare | tagLootFilter03Info |
| 4 | **3** | tagLootFilter04 | Monster Infrequent | tagLootFilter04Info |
| (4a) | **39** | tagLootFilter40 | *(Forgotten Gods only — see 3.3)* | tagLootFilter40Info |
| 5 | **4** | tagLootFilter05 | Epic | tagLootFilter05Info |
| 6 | **5** | tagLootFilter06 | Legendary | tagLootFilter06Info |
| 7 | **6** | tagLootFilter07 | Sets | tagLootFilter07Info |
| — | *sep* | | | |
| 8 | **7** | tagLootFilter08 | Always Show Uniques | tagLootFilter08Info |
| 9 | **38** | tagLootFilter39 | Always Show Double Rare | tagLootFilter39Info |

**Column 2 — `tagLootFilterTitle02` = "Type"** (header `window+0xa68`, indent 244)

| # | opt | caption tag | English |
|---|---|---|---|
| 1 | **8** | tagLootFilter09 | 1h Melee |
| 2 | **9** | tagLootFilter10 | 2h Melee |
| 3 | **10** | tagLootFilter11 | 1h Ranged |
| 4 | **11** | tagLootFilter12 | 2h Ranged |
| 5 | **12** | tagLootFilter13 | Dagger/Scepter |
| 6 | **13** | tagLootFilter14 | Caster Off-Hand |
| 7 | **14** | tagLootFilter15 | Shield |
| — | *sep* | | |
| 8 | **15** | tagLootFilter16 | Armor |
| — | *sep* | | |
| 9 | **16** | tagLootFilter17 | Accessories |
| 10 | **17** | **tagLootFilter38** | **Components** |

**Column 3 — `tagLootFilterTitle03` = "Damage"** (header `window+0xb60`, indent 488)

| # | opt | caption tag | English |
|---|---|---|---|
| 1 | **18** | tagLootFilter18 | Physical |
| 2 | **19** | tagLootFilter19 | Pierce |
| 3 | **20** | tagLootFilter20 | Fire |
| 4 | **21** | tagLootFilter21 | Cold |
| 5 | **22** | tagLootFilter22 | Lightning |
| 6 | **23** | tagLootFilter23 | Acid |
| 7 | **24** | tagLootFilter24 | Vitality |
| 8 | **25** | tagLootFilter25 | Aether |
| 9 | **26** | tagLootFilter26 | Chaos |
| 10 | **27** | tagLootFilter27 | Bleed |
| — | *sep* | | |
| 11 | **28** | tagLootFilter28 | Pet Bonuses |

**Column 4 — `tagLootFilterTitle04` = "Character"** (header `window+0xc58`, indent 732)

| # | opt | caption tag | English |
|---|---|---|---|
| 1 | **29** | tagLootFilter29 | My Masteries |
| 2 | **30** | tagLootFilter30 | Other Masteries |
| — | *sep* | | |
| 3 | **31** | tagLootFilter31 | Speed |
| 4 | **32** | tagLootFilter32 | Cooldown Reduction |
| 5 | **33** | tagLootFilter33 | Crit Damage |
| 6 | **34** | tagLootFilter34 | Offensive Ability |
| 7 | **35** | tagLootFilter35 | Defensive Ability |
| — | *sep* | | |
| 8 | **40** | **tagLootFilter41** | **Health** |
| 9 | **41** | **tagLootFilter42** | **Health Regeneration** |
| 10 | **36** | tagLootFilter36 | Resistances |
| — | *sep* | | |
| 11 | **37** | tagLootFilter37 | Retaliation |

### 3.2 The tag numbering is NOT the enum order

`opt = NN - 1` holds for `tagLootFilter01..17` (opts 0..16) and again for `tagLootFilter18..37`
(opts 18..37), but four tags are out of place, which is why the table above has to be transcribed from the
code rather than generated:

- `tagLootFilter38` ("Components") = **opt 17** — it was slotted into the middle of the enum, not appended.
- `tagLootFilter39` ("Always Show Double Rare") = opt 38, `tagLootFilter40` = opt 39,
  `tagLootFilter41` ("Health") = opt 40, `tagLootFilter42` ("Health Regeneration") = opt 41 — appended to the
  enum but placed mid-column in the layout.

`AddOption` is called with a literal `mov edx, N` (or `lea edx,[rbx+N]` with `rbx == 0`, exe+0x1c9190
onward) at every site, so the values are unambiguous.

### 3.3 The 42nd option and the missing English string

The full enum is **42 values, 0..41, no gaps** — every one of them appears exactly once in `SetRecord`.
Option **39** (`tagLootFilter40`) is the only one behind a runtime gate: exe+0x1c8be0 calls
`Engine::IsExpansion3Loaded()` and skips the `AddOption` when it is false, so a base-game install shows
**41 check boxes** in the Quality column's 4th/5th position order `Monster Infrequent -> (opt 39) -> Epic`.

The user's screenshot shows 41 boxes and no row between Monster Infrequent and Epic, i.e. Forgotten Gods is
not installed on the dev machine — consistent. `tagLootFilter40` / `tagLootFilter40Info` are **not in the
base game's `resources/Text_EN.arc`** (they ship in `gdx3/resources/Text_EN.arc`, which this install does not
have), so the English text is **unresolved** here. The mod should localize it at runtime like every other
caption rather than hardcode it; when the option is absent from the window, the bit still exists in the
player's bitset.

### 3.4 Buttons

| offset | class | record | tag | what the handler does |
|---|---|---|---|---|
| `+0x190` | Button (exe+0x10aee0) | `lootFilterCloseButton` | — (icon only) | `OnControlEvent` event **0**: clears its own `+0x282` and calls `window->vt[0xb0](false)` = `Show(false)` |
| `+0x4c8` | TextButton (exe+0x126fe0) | `lootFilterResetButton` | `tagLootFilterReset` = "Defaults" (`tagLootFilterResetInfo` = "Reset the filter settings to their defaults.") | `OnControlEvent` event **2**: sets `window+0xd80 = 1` and posts the confirm dialog (section 5.2) |

There are **no tabs**, no scrolling, and no "show all / hide all" control inside this window (see section 7
for the game's separate Hide-All-Items toggle, which is not part of this window).

---

## 4. `exe+0x1c99b0 = OnControlEvent(listener /*= window+0x90*/, int event, Control* ctrl, int)` — verified

All offsets in the body are relative to `window+0x90`; translated to window offsets:

```
if (window->+0xd80) return;                     // a Defaults confirm is pending: swallow everything
if (event == 0) {
    if (ctrl == window+0x190 /*close*/) {
        if (!close->+0x281 && close->+0x282) close->+0x282 = 0;
        window->vt[0xb0](window, false);        // Show(false)
        return;
    }
} else if (event == 2) {
    if (ctrl == window+0x4c8 /*reset*/) {
        window->+0xd80 = 1;
        // registers as observed at exe+0x1c9aeb..0x1c9b3a:
        //   rcx = GameEngine::GetDialogManager(gGameEngine)
        //   edx = 1 (DialogType Yes/No), r8d = 0, r9d = 0x1d (InterestedParty)
        //   [rsp+0x20] = std::string("tagLootFilterResetConfirm")
        //   [rsp+0x28] = (bool)1, [rsp+0x30] = (int)0, [rsp+0x38] = (bool)1
        //   [rsp+0x40] = std::string(""), [rsp+0x48] = std::string("")
        DialogManager::AddDialog(...);
        return;
    }
}
// every other control, and every event on a check box:
player = ObjectManager::GetObject(window->+0x98);
if (!player) return;
it = window->map(+0xd58).find(ctrl);            // std::map<CheckBox*, LootFilterOption>
if (it == end()) return;                        // the close/reset buttons land here and do nothing
Player::SetLootFilter(player, it->second, ctrl->+0x282);
```

`tagLootFilterResetConfirm` = "Reset filter settings to their defaults?".
**InterestedParty 0x1d (29) = the loot filter's Defaults confirm.**

The check-box branch runs for **any** event value (0, 1 or 2), because the registry's toggle press fires
exactly one of 0 or 1 depending on the direction (section 6). It always re-reads `ctrl->+0x282`, i.e. the
state the registry has already written — the listener never flips anything itself.

---

## 5. Refreshing from the player

### 5.1 `exe+0x1c9860 = Show(bool)` (vtable `+0xb0`), verified

```
exe+0x261cb0(this, visible);                   // the generic framework-B Show: writes +0x68, +0x69,
                                               //   notifies the parent at +0x38
if (!visible) { this->+0xd80 = 0; return; }    // drop any pending Defaults confirm

GameEngine::UnlockTutorialPage(gGameEngine, 0x44, true);
player = ObjectManager::GetObject(this->+0x98);
if (!player) return;
for (auto& [checkbox, option] : map(this+0xd58))
    checkbox->+0x282 = Player::GetLootFilter(player, option);
```

**That loop is the whole "read the truth from the player" story**: the check boxes are only ever a mirror,
refreshed on open and after a Defaults reset, and written straight back on click. Nothing else in the window
caches filter state — so the mod can read `Player::GetLootFilter(opt)` at any time and be correct whether the
window is open or not, and it never has to poll the check boxes.

Note `Show(false)` does **not** save anything and does **not** touch the filter: every click has already been
applied to the player.

### 5.2 `exe+0x1c9640 = Update()` (vtable `+0x48`), verified

```
if (!this->+0x68) return;                      // not visible
if (!this->+0xd80) return;                     // no Defaults confirm pending -- Update does nothing else
dm = GameEngine::GetDialogManager(gGameEngine);
if (DialogManager::GetNumResponsesFor(dm, 0x1d) <= 0) return;
DialogManager::GetResponseFor(dm, &resp, 0x1d);
if (resp.yes /* [rsp+0x34] == 1 */) {
    player = ObjectManager::GetObject(this->+0x98);
    if (player) {
        Player::SetLootFilterDefaults(player);
        for (auto& [checkbox, option] : map(this+0xd58))
            checkbox->+0x282 = Player::GetLootFilter(player, option);   // the same refresh loop
    }
}
this->+0xd80 = 0;
```

### 5.3 `exe+0x1c9970 = HandleEscape()` (vtable `+0x68`), verified

If visible and `+0xd80 == 0`, presses the close button through registry A:
`registry(window+0x108)->vt[0x80](registry, window+0x190, /*playSound*/ true)` and returns true.
Otherwise returns false.

### 5.4 `exe+0x1c9790 = HandleMouseEvent` (vtable `+0x38`), verified

Returns false when `+0x68 == 0`. Hit-tests the window rect (`+0x40..+0x4c` offset by the caller's origin);
inside, it dispatches to registry A (`+0x108`) then registry B (`+0x150`), each via the registry's own
`vt[0x38]`, and sets `+0x69`. There is no per-control list on the window itself.

---

## 6. Pressing a check box "the game's way"

Registry B (`window+0x150`, vtable exe+0x3148c0) is the **toggle** registry — the same class the InGameUI HUD
host uses. Two entry points, both verified:

- **`exe+0x12b680` = `PressChild(registry, Control* ctrl, bool playSound)` = registry `vt[0x80]`**
  ```
  find ctrl in registry->map(+0x30);  if not found or ctrl->+0x281 (disabled) -> return
  if (ctrl->+0x282) { ctrl->+0x282 = 0;  if (playSound) play ctrl->+0x270;  event = 1; }
  else              { ctrl->vt[0x90](ctrl, 0);   /* sets +0x282 = 1, plays +0x278 */   event = 0; }
  exe+0x12a9a0(registry, ctrl, &listeners, event, 1);   // -> listener->vt[0](listener, event, ctrl, 0)
  ```
  So one press = **one** listener event: **1 when it just turned OFF, 0 when it just turned ON**. Either way
  `OnControlEvent` reaches the `SetLootFilter` path and reads the already-updated `+0x282`.
- `exe+0x12b4d0` = registry `vt[0x38]` (the mouse path) does the same thing on a real left-DOWN (event type 1)
  over a control whose `HandleMouseEvent` returned true.

`ctrl->vt[0x90]` = exe+0x124c30 = `TextButton::SetPressed(Control* only, bool playSound)`: refuses when
disabled or already pressed, sets `+0x282 = 1`, optionally plays `+0x278`.

**Recommended mod path — do NOT drive the window.** `Player::SetLootFilter(option, bool)` is exported and is
the single source of truth; the game's own item code reads the bitset live, and the window is only a mirror.
So:

```
p = GameEngine::GetMainPlayer();
Player::SetLootFilter(p, option, value);                     // exported, immediate
// only if the window happens to be open and you care about its pixels:
for (auto& [cb, opt] : map(window+0xd58)) if (opt == option) cb->+0x282 = value;
```

Reset to defaults, likewise: `Player::SetLootFilterDefaults(p)` directly (skipping the
`tagLootFilterResetConfirm` modal entirely), then the same mirror loop. If the game's confirm prompt IS
wanted, press the reset button through **registry A**:
`registry(window+0x108)->vt[0x80](registry, window+0x4c8, true)` — registry A is the exe+0x12ac80 variant
that fires 0, 1, 2, and the 2 is what posts the dialog.

Closing is `window->vt[0xb0](window, false)`; the window's Escape (`vt[0x68]`) does the same through the
close button.

---

## 7. Related, but NOT part of this window

- **Opening it**: `InGameUI::HandleKeyAction(ui, 0x38, ...)` (exe+0x211980) does
  `registry(InGameUI+0x72f8)->vt[0x80](InGameUI+0x8940, true)` at exe+0x211d49 — it presses the HUD's loot
  filter button, whose listener shows the window. The window's open state is also mirrored into the HUD
  button's pressed byte `InGameUI+0x8bc2` by `InGameUI::OnWindowVisibilityChanged` (exe+0x213110, writes 0 at
  exe+0x21334e and 1 at exe+0x21352a next to `lea rax,[rbx+0xab410]`). Either byte or `window->vt[0xb8]()`
  answers "is the loot filter window open".
- **"Toggle Hide All Items"** (key action **0x39**, unbound by default) is a different feature:
  exe+0x2114ba flips the byte **`InGameUI+0x72f5`**, mirrors `!it` into `InGameUI+0x8bc1` / `+0x8bc4`, clears
  `+0x8bc2` when it turns on, and posts a notification localized from `tagLootFilterToggleOn` ("Items Shown")
  / `tagLootFilterToggleOff` ("Items Hidden"). That byte is consumed where the exe calls
  `Item::PassLootFilter` — at exe+0x28be64 it selects the `InGameUIActorCapture::ItemIgnore` argument, and at
  exe+0x20fc6 it picks `3` vs `0` for the same argument. The exact `ItemIgnore` enum is **not traced**
  (**inferred**: it is the "ignore the filter / ignore everything" switch for the floating labels).
  A mod key for this would write `InGameUI+0x72f5` — the only new offset, and the notification text is
  already localizable.
- `GameEngine::GetLootMode()` / `GoldGenerator::SetLootMode()` are the multiplayer loot-distribution mode,
  unrelated to this window.

---

## 8. The `.arz` side (`tools/arz.py`)

- `records/ui/hud/hud_mastertable.dbr` -> `hudLootFilterWindow = records/ui/lootfilter/lootfilter_mastertable.dbr`
  (plus `hudLootFilterButton` / `...Rollover`, the HUD button).
- `records/ui/lootfilter/lootfilter_mastertable.dbr` (template `ingameui/lootfilterwindow.tpl`):
  `lootFilterBitmap`, `lootFilterCheckbox`, `lootFilterCloseButton`, `lootFilterResetButton`,
  `lootFilterSpacer`, `lootFilterTitle01..04`, `lootFilterWindowTitle`,
  `lootFilterColumnIndent2/3/4 = 244/488/732`, `lootFilterRowSpacing = 36`, `lootFilterRowSpacing2 = 30`,
  `windowDefaultExtentX/Y = 1001/699`, centred on screen.
- `records/ui/lootfilter/lootfilter_checkbox.dbr` (template `ingameui/checkbox.tpl`) carries only art,
  sounds and styles — **no caption and no option id**; both come from the code.
- The captions/tooltips are `tagLootFilter01..42` + `tagLootFilterNNInfo` in `tags_ui.txt`
  (`resources/Text_EN.arc`), except `tagLootFilter40*` (expansion 3).

---

## 9. Byte signatures (first 16 bytes, for an `exe_ui::available()`-style check)

```
exe+0x1c7c30 LootFilterWindow::ctor        48 89 4c 24 08 55 56 57 41 54 41 55 41 56 41 57
exe+0x1c8180 LootFilterWindow::AddOption   40 55 56 57 41 56 41 57 48 8b ec 48 83 ec 60 48
exe+0x1c83d0 LootFilterWindow::AddSpacer   48 89 5c 24 10 48 89 6c 24 18 56 57 41 56 48 83
exe+0x1c8550 LootFilterWindow::SetRecord   48 8b c4 55 41 54 41 55 41 56 41 57 48 8d 68 a1
exe+0x1c9460 LootFilterWindow::Render      48 89 74 24 18 57 48 83 ec 30 80 79 68 00 48 8b
exe+0x1c9640 LootFilterWindow::Update      40 57 48 83 ec 20 80 79 68 00 48 8b f9 0f 84 2d
exe+0x1c9790 LootFilterWindow::HandleMouse 48 89 5c 24 10 48 89 6c 24 18 48 89 74 24 20 57
exe+0x1c9860 LootFilterWindow::Show(bool)  48 89 5c 24 10 57 48 83 ec 20 0f b6 da 48 8b f9
exe+0x1c9970 LootFilterWindow::HandleEsc   48 83 ec 28 80 79 68 00 48 8b d1 74 2a 80 b9 80
exe+0x1c99b0 LootFilterWindow::OnCtrlEvent 40 57 48 81 ec c0 00 00 00 48 c7 44 24 50 fe ff
```

Vtables touched: exe+0x317640 (LootFilterWindow), exe+0x317638 / exe+0x317740 (its two sub-vtables),
exe+0x313b18 + exe+0x313bf0 (CheckBox), exe+0x313ce8 (TextButton), exe+0x313e78 (Button),
exe+0x31c590 (the text element), exe+0x313860 (image control), exe+0x312cf0 (registry A),
exe+0x3148c0 (registry B, the toggle variant).

---

## 10. What the mod actually needs

1. **No new exe RVA is required to read or write the filter.** `Player::GetLootFilter(opt)` /
   `SetLootFilter(opt, bool)` / `SetLootFilterDefaults()` are exported; the option enum is the 42-value table
   in 3.1 and the captions/tooltips are `LocalizationManager::LocalizeWithoutParams("tagLootFilterNN" /
   "...Info")`. A mod screen can be built entirely from that table plus the exports, with no window at all.
2. **To model the game's screen** (the design rule: our graph over the game's own objects), the useful
   offsets are `InGameUI+0xab410` for the window, `+0x68` visible, `+0xd58` the `map<CheckBox*, option>`
   (option value at `node+0x28`, control at `node+0x20`), `+0x282` on each control for the checked byte,
   `+0x338`/`+0x358` for its tag/caption, `+0x318` for its tooltip tag, `+0x190`/`+0x4c8` for
   Close/Defaults, `+0x970..+0xc58` for the four column headers, `+0x150`/`+0x108` for the two registries.
   Walking the map gives the live control set (41 or 42 entries) but **not** the display order — the map is
   keyed by pointer. Use the section 3.1 table for order and grouping, and the map only to find the control
   for an option.
3. **Grouping to speak**: four groups named by `tagLootFilterTitle01..04` (Quality / Type / Damage /
   Character) with the row order of 3.1; the separators are the game's own visual sub-grouping and are worth
   preserving as pauses/sub-headings but carry no text.
4. **Refresh discipline**: read `Player::GetLootFilter` per row every time the screen is rendered — the
   window itself only refreshes on `Show(true)` and after a Defaults reset, so its bytes are stale in exactly
   the case where something else changed the filter.

## 11. Open / not resolved

- `tagLootFilter40`'s English text (option 39) — the string lives in the Forgotten Gods text arc, which is
  not installed on this machine. Its position (Quality column, between Monster Infrequent and Epic) is
  verified.
- `CheckBox+0x3b0 / +0x3b4` are both set to 2 by `AddOption` (the plain TextButton ctor leaves them alone);
  **inferred** to be an alignment/style pair, not traced.
- `Render` (exe+0x1c9460) was not read: it should be pure drawing (the ctor gives it no state beyond the
  controls), but the column headers' final screen positions come from the records, not from code.
- The `InGameUIActorCapture::ItemIgnore` enum behind the Hide-All-Items toggle (section 7) is **inferred**.
- The HUD loot-filter button's own listener (the thing `HandleKeyAction 0x38` presses) was not read; the
  window is reached through it, but `window->vt[0xb0](true)` is the direct equivalent.
