#pragma once
// The exe's private UI objects, reached by base-relative layout (static RE of Grim Dawn v1.3.0.8 x64, 2026-08-22;
// docs/exe-ui-layout.md has the evidence). Two widget frameworks live in the exe, neither exported:
//   A -- the menu tree (main menu and its dialogs, options, multiplayer): a parent/children widget tree under a
//        DisplayWidget root; buttons carry their caption and a listener list, and a click is a listener call.
//   B -- the in-world windows owned by InGameUI (character, quest, exit menu, NPC dialog, prompt box ...):
//        by-value members at fixed offsets; open = a virtual IsVisible(); buttons are pressed through the host
//        widget's "click this child" virtual, the same path the game's own key bindings use.
// Message boxes sit on the EXPORTED GAME::DialogManager and are read/answered through it.
// Rules: everything is exe_base()+rva or an offset off a live pointer (ASLR-safe); every read is SEH-guarded;
// every call happens on the game thread; no widget pointer is held across frames (resolve each frame).
#include <cstdint>
#include <string>
#include <vector>

namespace gd::exe_ui {

// Installed once; false when the exe does not match the layout this module was written against (a game
// patch). Screens built on this module report inactive then, so the unsupported fallback takes over.
bool install();
bool available();
std::string version_line();   // the exe's PE timestamp / size, for the log and /health

struct Rect { float x = 0, y = 0, w = 0, h = 0; };

// ---- framework A: the menu tree ----
struct WidgetA {
  void* p = nullptr;  // the tree node (the "B" subobject as stored in child vectors)
  explicit operator bool() const { return p != nullptr; }
  bool operator==(const WidgetA& o) const { return p == o.p; }
  WidgetA parent() const;
  std::vector<WidgetA> children() const;
  Rect rect() const;       // parent-relative
  Rect abs_rect() const;   // summed up the parent chain (scroll offsets ignored)
  bool active() const;     // +0x50: gates rendering and all input
  bool enabled() const;    // +0x51: a disabled (greyed) button has 0 -- measured live on Create Character's Next
  uintptr_t vtable_rva() const;
  bool is_button() const;  // caption at +0x268; toggle buttons (radio/check) keep their state in pressed()
  bool is_text() const;    // a static text widget: string at +0xc0 (wrapped at draw time)
  bool is_edit() const;    // an edit box: string at +0x238, caret index +0x21c, focus byte +0x218
  std::string text() const;  // the widget's own string (caption / static text / edit contents), UTF-8
  std::string caption() const { return is_button() ? text() : std::string(); }
  std::string tooltip_tag() const;  // a button's rollover localization tag (+0x78, std::string); empty if none
  bool hovered() const;
  bool pressed() const;    // +0x249: pressed, or for a toggle button its checked/selected state
  bool is_toggle() const;  // +0x24a: a radio / check box (left-down flips pressed and fires the listeners once)
  std::string edit_state() const;  // dev: the edit box's state bytes
  // What the game's own mouse path does on a click, minus the sounds: a plain button gets press (0) then
  // release-inside (2) through its listeners; a toggle flips its state and fires once. Game thread. False if
  // this is not a button or a call faulted.
  bool activate() const;
  // Children of one kind, in tree (= draw) order.
  std::vector<WidgetA> buttons() const;
  std::vector<WidgetA> texts() const;
  std::vector<WidgetA> edits() const;
  // ---- the options screen's value controls (docs/exe-ui-layout.md, "Options") ----
  bool is_slider() const;          // exe+0x30d4c0: float value +0x324 in [min +0x320, max +0x31c]
  float slider_value() const;      // normalised to 0..1 (the options sliders are 0..1 already)
  bool set_slider(float v01) const;  // writes the value and fires the slider's listeners (what a drag does)
  bool is_combo() const;           // exe+0x30c278: items vector +0xd0 (0x30 stride, u16 text first), selected +0xec
  std::vector<std::string> combo_items() const;
  int combo_index() const;
  bool set_combo(int index) const;   // writes the index and fires the listeners (what choosing a row does)
  bool is_list() const;            // exe+0x30c530: the key-binding table, rows of u16 cells (action, key, key)
  std::vector<std::vector<std::string>> list_rows() const;
};

// A modal popup of the menu tree (e.g. "A character with that name already exists."): a layer widget at the
// root holding one window with text widgets and buttons. Null when none is up.
struct Popup {
  WidgetA window;
  explicit operator bool() const { return (bool)window; }
  std::string text() const;              // its text widgets, joined
  std::vector<WidgetA> buttons() const { return window.buttons(); }
};
Popup popup();

// App state: 3/4/8 main menu, 5 options, 6 multiplayer, 7 and 9 unnamed, 10 the world. 0 = unknown.
int app_state();
WidgetA root();             // the tree root (MenuManager+8); null outside the menus

// The main-menu manager (app state 3/4/8) and its named slots.
struct MainMenu {
  void* p = nullptr;
  explicit operator bool() const { return p != nullptr; }
  WidgetA button(unsigned slot_off) const;   // one of the kBtn* offsets below
  void* sub_window(unsigned slot_off) const; // kWinCreateCharacter ... (null when not open)
  void* current_sub_window() const;          // +0xf8
  // Slots measured live 2026-08-22 against the captions (click handler exe+0xd8f80 names the behaviours).
  static constexpr unsigned kBtnCreate = 0x2a0, kBtnDelete = 0x2b0, kBtnOptions = 0x2f8 /*icon, no caption*/, kBtnCredits = 0x300,
                            kBtnExit = 0x308 /*icon, no caption*/, kBtnDLC = 0x310, kBtnGameGuide = 0x318, kBtnResume = 0x320 /*null so far*/,
                            kBtnCommunity = 0x328, kBtnMultiplayer = 0x330, kBtnStart = 0x338, kBtnDifficulty = 0x340 /*"Normal Difficulty"*/,
                            kBtnGameMode = 0x348 /*"Main Campaign"*/, kBtnSlotA = 0x2a8;
  static constexpr unsigned kWinCreateCharacter = 0x2b8, kWinDeleteCharacter = 0x2c0, kWinDifficulty = 0x2d0,
                            kWin4th = 0x2d8, kWinHiding = 0x2e0;
  // A sub-window object stays allocated while hidden behind the next dialog; these bytes say so.
  static constexpr unsigned kCreateCharacterHidden = 0x288, kDifficultyHidden = 0x350, k4thHidden = 0x29e;
};
MainMenu main_menu();
// A sub-window object's tree node: the window object IS its node (measured live 2026-08-22: the Create
// Character window pointer appears in the manager's child vector as is).
WidgetA window_node(void* window);
bool window_hidden_flag(void* window, unsigned flag_off);  // CreateCharacter +0x288, Difficulty +0x350

// The options screen (app state 5): seven tab toggles (their rollover tags name them), the active page's
// controls in declaration order (a TEXT label immediately precedes the slider/combo it names), and the
// Default / Apply / Close buttons. Page switch = the tab toggle's listeners (exe+0xcd300).
struct OptionsScreen {
  WidgetA screen, panel, page;
  std::vector<WidgetA> tabs, buttons;  // buttons: in tree order (Apply, Close, Default)
  explicit operator bool() const { return screen && page && tabs.size() == 7; }
  int tab_index() const;               // screen+0x280
};
OptionsScreen options_screen();

// ---- framework B: InGameUI's embedded windows ----
void* ingame_ui();  // null outside the world
struct WindowB {
  void* p = nullptr;
  explicit operator bool() const { return p != nullptr; }
  bool visible() const;    // vtable +0xb8
  void show(bool on) const;  // vtable +0xb0
};
WindowB ingame_window(unsigned off);   // InGameUI + off (kWin* below)
// ---- the riftgate travel map (docs/exe-ui-layout.md "Riftgate travel"): the world map in riftgate mode ----
struct Riftgate {
  std::string name;      // the zone's localized name ("Devil's Crossing")
  int pos[3] = {};       // integer world coordinates (what the map's travel call takes)
  unsigned object_id = 0;  // the gate entity when loaded, else 0
  unsigned owner = 0;    // a personal riftgate's player id; 0 for the static gates
  int uid[4] = {};       // UniqueId (the discovered-set key)
  bool current = false;  // the gate the player is standing at
};
bool riftgate_map_open();                  // MiniMap visible + shown + mode byte 0 (the exe's own predicate, exe+0x21be20)
std::vector<Riftgate> riftgates();         // the discovered gates the map draws, in its section order
bool riftgate_travel(const Riftgate& g);   // what the click does: SetLastUsedTeleportId + the map's travel call (exe+0x291520)
void riftgate_map_close();                 // the close button: MiniMap Show(false)
namespace ingame {
constexpr unsigned kPromptBox = 0x7378, kCharacter = 0x52258 /*the multiplayer Inspect twin*/, kInventory = 0xbbf0 /*the C/I window*/,  // (+0xb138/+0xb158 are record-path strings)
                   kQuest = 0x285a0, kSkills = 0x3fc20, kMiniMap = 0x42260, kExit = 0x4a300, kParty = 0x4b540,
                   kFactions = 0x6c9b8, kAchievements = 0x7d150, kDevotion = 0x813a0, kStack = 0x83ed8,
                   kPotions = 0x8a300, kQuestReward = 0x8efd8, kObjective = 0x90390, kLootFilter = 0xab410,
                   kTrade = 0x29cc8, kMarket = 0x2b538, kEnchanter = 0x30dd8, kTransmuter = 0x85378, kAltar = 0x87628,
                   kFactionVendor = 0x2e188, kCaravan = 0x4fd08, kShrine = 0x7da50, kCrafting = 0x3aa80, kAscension = 0x8baa8;
constexpr unsigned kHost = 0x7338;  // the widget host whose vtable +0x80 presses a child button
}
struct WidgetB {
  void* p = nullptr;
  explicit operator bool() const { return p != nullptr; }
  uintptr_t vtable_rva() const;
  bool is_button() const;         // the plain bitmap button (no caption of its own)
  bool is_text_button() const;    // TextButton: localized caption at +0x358 (ctor exe+0x126fe0)
  bool is_text() const;           // text element: string at +0x40
  std::string text() const;       // caption / text, UTF-8
  bool visible() const;           // +0x28
  bool enabled() const;           // !+0x281 (the host's PressChild refuses disabled controls too)
  bool pressed() const;           // +0x282
  std::string state_bytes() const;  // dev
  // Press through a listener registry's PressChild (vtable +0x80): the registry of the window that owns the
  // control (window + its registry offset) performs a complete click (events 0,1,2); InGameUI's HUD host at
  // +0x7338 toggles instead. Returns false when the registry refused (control unregistered or disabled) or
  // the call faulted. Game thread.
  bool press(void* registry) const;
};

// The in-world Escape menu (hudExitWindow, ctor exe+0x26e060): Return to Game / Options Menu / Exit to Main
// Menu / Quit to Desktop as TextButtons at fixed offsets, pressed through the window's own registry (+0x108).
struct ExitWindow {
  void* p = nullptr;
  explicit operator bool() const { return p != nullptr; }
  bool visible() const;
  static constexpr unsigned kResume = 0x150, kExit = 0x500, kExitGame = 0x8b0, kOptions = 0xc60, kRegistry = 0x108, kTitle = 0x1010;
  WidgetB button(unsigned off) const { return {(char*)p + off}; }
  std::vector<WidgetB> buttons() const { return {button(kResume), button(kOptions), button(kExit), button(kExitGame)}; }  // player order
  bool press(WidgetB b) const { return b.press((char*)p + kRegistry); }
};
ExitWindow exit_window();
// The game's own key-binding actions by id (InGameUI::HandleKeyAction, signature-checked): 0x37 = Pickup (the
// nearest item on the ground), 0x36 = Interact, 1 = Character window ... (docs/ingame-ui-survey.md). Game thread.
bool ingame_key_action(int action);
// The skills window's panes (SkillsWindow::SetPane, signature-checked): put mastery `pane_index` (the mastery's
// enumeration, 0 = Soldier ...) or the class-selection pane (kSkillsClassSelectPane) on tab 0 / 1. The game's own
// choose-a-class path; permanent once the mastery skill has a point. skills_tab() = the window's current tab.
// The quickbar page the HUD shows (InGameUI+0x72f0, 0..3; the Y key cycles it), -1 outside the world.
int quickbar_page();
// A vendor window's market id (its marketGrid +0x2410 keeps it at +0x54; 0 outside a vendor).
unsigned vendor_market_id(const WindowB& vendor_window);
constexpr int kSkillsClassSelectPane = 0x50;
bool skills_set_pane(int tab, int pane_index);
int skills_tab();

// The NPC conversation window (allocated on demand, pointer at InGameUI+0x8efd0; ctor exe+0x16e9a0): the
// speaker and speech text, and the response rows, each carrying its display text and the step it selects
// (null = end conversation). Choosing a row goes through the game's own click path (the step's quest
// actions run there): a click at the row's own rectangle.
struct ConvRow {
  void* p = nullptr;
  std::string text() const;     // +0x1c8
  void* step() const;           // +0x48
  Rect rect() const;            // +0x38, relative to the window
};
struct ConvWindow {
  void* p = nullptr;
  explicit operator bool() const { return p != nullptr; }
  bool open() const;            // +0x28 visible and fade state +0x1ab8 != closed
  std::string speaker() const;  // the +0x2a0 text element
  std::string speech() const;   // the full NPC speech +0x1ac0
  std::string page_text() const;  // the currently shown page (+0x378 text element)
  std::vector<ConvRow> rows() const;  // +0x1a60 vector
  Rect rect() const;            // the window's own rect (+0x260)
  bool choose(const ConvRow& r) const;  // click at the row's rectangle (game thread queues the events)
};
ConvWindow conv_window();
std::string conv_elements_dump();  // dev: the vtables/texts of the window's speaker and page elements
std::string peek(uintptr_t ptr, int n);  // dev: hex dump of n bytes (qwords that point into the exe are annotated)

// ---- tutorial tips / notifications: the tip manager at [main_obj+0xbe0] (ctor exe+0x1087f0) ----
// A tip is a heap struct holding its already-localized, line-split text (line 0 is the title); kind 1 =
// tutorial tip; state 0 fading in, 1 shown, 2 fading out, 3 dismissed (what a right click sets).
struct Tip {
  void* p = nullptr;
  explicit operator bool() const { return p != nullptr; }
  std::vector<std::string> lines() const;
  int state() const;
  int kind() const;
  int page() const;   // help page id, -1 when not clickable
  bool showing() const { return p && state() < 2; }
  void dismiss() const;  // state 3 + the fade-out timer, as the right-click path does
};
std::vector<Tip> tips();  // live tips, oldest first

// ---- message boxes (exported DialogManager) ----
bool dialog_open();
std::string dialog_text();
int dialog_type();               // 0 Okay, 1 Yes/No, -1 none
bool answer_dialog(bool yes);    // game thread; Okay boxes are just removed

// ---- dev dumps (game thread) ----
std::string ui_dump();           // app state, main menu slots, the whole framework A tree
std::string ingame_dump();       // every known InGameUI window with IsVisible(), the prompt state
std::string dialog_dump();
bool activate_ptr(uintptr_t p);  // /ui/activate?ptr= -- pointer must be a node in the current tree
}  // namespace gd::exe_ui
