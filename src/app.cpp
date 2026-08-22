#include "app.h"
#include <windows.h>
#include <memory>
#include "core/graph_announcer.h"
#include "core/message_builder.h"
#include "core/navigator.h"
#include "core/strings.h"
#include "hooks.h"
#include "log.h"
#include "screens/conversation.h"
#include "screens/create_character.h"
#include "screens/difficulty_select.h"
#include "screens/in_game.h"
#include "screens/loading.h"
#include "screens/main_menu.h"
#include "screens/message_box.h"
#include "screens/pause_menu.h"
#include "screens/tip.h"
#include "speech.h"
#include "world.h"

namespace gd::app {
using namespace gd::core;

static InputManager g_input;
static ScreenManager g_screens;
static std::unique_ptr<GraphNavigator> g_nav;
static bool g_owns_keyboard = false;  // last decision applied to the key hook; changes are logged
static bool g_inited = false;
static double g_last_tick = 0;

double now() {
  static LARGE_INTEGER freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
  LARGE_INTEGER c; QueryPerformanceCounter(&c);
  return (double)c.QuadPart / (double)freq.QuadPart;
}

bool owns_keyboard() { return g_owns_keyboard; }
InputManager& input() { return g_input; }
ScreenManager& screens() { return g_screens; }
GraphNavigator* navigator() { return g_nav.get(); }

// Typematic at the user's own OS keyboard settings, so nav key-repeat feels like the rest of the system.
static InputManager::Typematic os_typematic() {
  InputManager::Typematic t;
  int delay = 0, speed = 0;
  if (SystemParametersInfoW(SPI_GETKEYBOARDDELAY, 0, &delay, 0) && SystemParametersInfoW(SPI_GETKEYBOARDSPEED, 0, &speed, 0)) {
    delay = delay < 0 ? 0 : delay > 3 ? 3 : delay;           // 0..3 -> 250..1000 ms
    speed = speed < 0 ? 0 : speed > 31 ? 31 : speed;         // 0..31 -> 2.5..30 cps
    t.initial_delay = (delay + 1) * 0.25;
    t.repeat_interval = 1.0 / (2.5 + (speed / 31.0) * 27.5);
    log::writef("OS key repeat: delay={:.3f}s interval={:.3f}s (raw delay={} speed={})", t.initial_delay, t.repeat_interval, delay, speed);
  } else {
    log::write("OS key repeat query failed; using defaults");
  }
  return t;
}

// Key codes in the game's Button enum (DIK for plain keys; extended keys measured -- see CLAUDE.md).
namespace keys {
constexpr int Escape = 0x01, Tab = 0x0f, Enter = 0x1c, Backspace = 0x0e, Space = 0x39, F1 = 0x3b, Backslash = 0x2b;
constexpr int Home = 0x78, Up = 0x79, PageUp = 0x7a, Left = 0x7b, Right = 0x7c, End = 0x7d, Down = 0x7e, PageDown = 0x7f;  // from the game's own key names (tools/exports/keynames.txt)
constexpr int P = 0x19;
}  // namespace keys

static void register_actions() {
  auto& m = g_input;
  struct Ui { std::string_view id; const char* label; int key; bool shift, ctrl; bool repeat; };
  const Ui ui[] = {
    {ui_actions::Up, "Previous item", keys::Up, false, false, true}, {ui_actions::Down, "Next item", keys::Down, false, false, true},
    {ui_actions::Left, "Left", keys::Left, false, false, true}, {ui_actions::Right, "Right", keys::Right, false, false, true},
    {ui_actions::Next, "Next panel", keys::Tab, false, false, true}, {ui_actions::Prev, "Previous panel", keys::Tab, true, false, true},
    {ui_actions::Home, "First", keys::Home, false, false, false}, {ui_actions::End, "Last", keys::End, false, false, false},
    {ui_actions::RegionPrev, "Previous region", keys::Up, false, true, false}, {ui_actions::RegionNext, "Next region", keys::Down, false, true, false},
    {ui_actions::Activate, "Activate", keys::Enter, false, false, false}, {ui_actions::Secondary, "Secondary action", keys::Backspace, false, false, false},
    {ui_actions::Back, "Back", keys::Escape, false, false, false}, {ui_actions::Tooltip, "Tooltip", keys::Space, false, false, false},
    {ui_actions::Drag, "Drag", keys::Backslash, false, false, false},
  };
  for (const Ui& u : ui) {
    auto& a = m.register_action(std::string(u.id), u.label, InputCategory::UI).bind(u.key, u.ctrl, u.shift, false);
    if (u.repeat) a.repeating();
  }
  m.find(ui_actions::Tooltip)->bind(keys::F1);
  m.register_action("ingame.where", "Where am I", InputCategory::InGame, [] { screens::speak_where(); }).bind(keys::P, true, true).bind(0x25);  // K
  // The review cursor (wotr's scanner keys): Period enemies, N people, B bystanders, M objects; Shift
  // cycles backward. Each landing speaks name / distance / clock bearing and parks the virtual cursor on it.
  struct Cycle { const char* id; const char* label; int key; world::ScanGroup group; int dir; bool shift; };
  const Cycle cycles[] = {
    {"scan.enemyNext", "Next enemy", 0x34, world::ScanGroup::Enemies, 1, false}, {"scan.enemyPrev", "Previous enemy", 0x34, world::ScanGroup::Enemies, -1, true},
    {"scan.neutralNext", "Next person", 0x31, world::ScanGroup::Neutrals, 1, false}, {"scan.neutralPrev", "Previous person", 0x31, world::ScanGroup::Neutrals, -1, true},
    {"scan.bystanderNext", "Next bystander", 0x30, world::ScanGroup::Bystanders, 1, false}, {"scan.bystanderPrev", "Previous bystander", 0x30, world::ScanGroup::Bystanders, -1, true},
    {"scan.objectNext", "Next object", 0x32, world::ScanGroup::Objects, 1, false}, {"scan.objectPrev", "Previous object", 0x32, world::ScanGroup::Objects, -1, true},
  };
  for (const Cycle& c : cycles) {
    world::ScanGroup g = c.group; int dir = c.dir;
    m.register_action(c.id, c.label, InputCategory::InGame, [g, dir] { speech::speak(world::cycle_review(g, dir), true); }).bind(c.key, false, c.shift, false);
  }
  m.register_action("scan.ping", "Ping the reviewed thing", InputCategory::InGame,
                    [] { if (world::ping_reviewed().empty()) speech::speak(strings::kNoTarget, true); }).bind(0x27);  // Semicolon
  m.register_action("scan.interact", "Interact with the reviewed thing", InputCategory::InGame,
                    [] { if (!world::interact_reviewed()) speech::speak(strings::kNoTarget, true); }).bind(0x17).bind(keys::Enter);  // I, Enter
  // The game's less frequent functions, lifted to Ctrl + their default key (docs/controls.md): the chord is
  // ours, the plain key is injected into the game's poll, so the game's own map stays untouched and the
  // plain letters are free for the mod. Frequent keys (WASD, 1-0, Space, E, R, U, Escape) pass through
  // directly in screens/in_game.cpp.
  struct Lift { const char* id; const char* label; int code; char16_t ch; };
  const Lift lifted[] = {
    {"game.character", "Character window", 0x2e, u'c'}, {"game.skills", "Skill window", 0x31, u'n'}, {"game.codex", "Codex window", 0x10, u'q'},
    {"game.map", "Map window", 0x32, u'm'}, {"game.lootFilter", "Loot filter window", 0x18, u'o'}, {"game.group", "Group window", 0x25, u'k'},
    {"game.gameMenu", "Game menu", 0x22, u'g'}, {"game.help", "Help window", 0x23, u'h'}, {"game.factions", "Factions window", 0x24, u'j'},
    {"game.achievements", "Achievements window", 0x2f, u'v'}, {"game.riftgate", "Personal riftgate", 0x26, u'l'}, {"game.drop", "Drop item", 0x30, u'b'},
    {"game.tooltips", "Show item tooltips", 0x2d, u'x'}, {"game.showItems", "Show items (filter common)", 0x2c, u'z'}, {"game.quickbarSwitch", "Quickbar switch", 0x15, u'y'},
    {"game.pause", "Pause game", 0x19, u'p'}, {"game.petDisplay", "Toggle pet display", 0x0e, 0}, {"game.partyDisplay", "Toggle party display", 0x2b, u'\\'},
    {"game.toggleUi", "Toggle UI", 0x1b, u']'}, {"game.cameraLeft", "Camera rotate left", 0x33, u','}, {"game.cameraRight", "Camera rotate right", 0x34, u'.'},
    {"game.chat", "Chat window", 0x1c, 0}, {"game.pushToTalk", "Push to talk", 0x0f, 0},
  };
  for (const Lift& l : lifted) {
    int code = l.code; char16_t ch = l.ch;
    m.register_action(l.id, l.label, InputCategory::InGame, [code, ch] { hooks::push_key(code, false, false, false, ch); }).bind(l.code, true, false, false);
  }
}

void init() {
  if (g_inited) return;
  g_inited = true;
  g_input.set_typematic(os_typematic());
  // Announcer wording comes from the strings module.
  GraphAnnouncer::position_text = [](int i, int n) { MessageBuilder m; strings::push_position(m, i, n); return m.build(); };
  GraphAnnouncer::expanded_state_text = [](bool e) { return std::string(e ? strings::kExpanded : strings::kCollapsed); };
  g_nav = std::make_unique<GraphNavigator>(NavigatorHost{
      [](std::string_view t, bool interrupt) { speech::speak(t, interrupt); },
      {},  // hover sound: none yet
      [] { return g_screens.owns_keyboard(); },  // live: the navigator attaches from inside the screen tick
      [] { return (int)hooks::frame(); }});
  Screen::set_host([](std::string_view s) { speech::speak(s, false); }, [](Screen* s) { if (g_nav) g_nav->screen_closed(s); });
  g_screens.set_navigator([](Screen* s) { if (g_nav) g_nav->attach(s); }, [] { if (g_nav) g_nav->ensure_focus(); });
  register_actions();
  hooks::set_game_key_filter([](int code) { Screen* s = g_screens.current(); return s && s->passes_key(code); });
  g_screens.register_screen(screens::make_unsupported());
  g_screens.register_screen(screens::make_main_menu());
  g_screens.register_screen(screens::make_create_character());
  g_screens.register_screen(screens::make_difficulty_select());
  g_screens.register_screen(screens::make_in_game());
  g_screens.register_screen(screens::make_message_box());
  g_screens.register_screen(screens::make_pause_menu());
  g_screens.register_screen(screens::make_loading());
  g_screens.register_screen(screens::make_tip());
  g_screens.register_screen(screens::make_conversation());
  g_last_tick = now();
  log::writef("app: initialized with {} actions", g_input.actions().size());
}

void tick() {
  if (!g_inited) return;
  double t = now();
  double dt = g_last_tick > 0 ? t - g_last_tick : 0;
  g_last_tick = t;
  // A screen capturing raw input this frame keeps the keys it read in on_update from also reaching the
  // navigator (the Enter that ends a text edit must not re-activate the field).
  Screen* before = g_screens.current();
  bool raw = before && before->captures_raw_input();
  g_screens.tick();
  // The screen stack decides who gets the keyboard; the hook is only touched when the decision changes so a
  // dev override (/gamekeys) holds until the next screen change.
  bool owns = g_screens.owns_keyboard();
  if (owns != g_owns_keyboard) {
    g_owns_keyboard = owns;
    hooks::set_game_keys_muted(owns);
    Screen* s = g_screens.current();
    log::writef("keyboard: {} ({})", owns ? "mod" : "game", s ? s->key() : "no screen");
  }
  g_input.set_live_categories(g_screens.live_categories());
  Screen* cur = g_screens.current();
  if (raw || (cur && cur->captures_raw_input())) return;
  const KeySource& ks = hooks::key_source();
  g_input.tick(t, ks, [](InputAction& a) { return g_nav && g_nav->on_action(a.key()); });
  if (g_nav) {
    TypeaheadInput in;
    in.typed = hooks::typed_chars();
    in.ctrl = ks.ctrl(); in.alt = ks.alt(); in.shift = ks.shift();
    in.escape_pressed = ks.just_pressed(keys::Escape);
    in.up_held = ks.held(keys::Up); in.down_held = ks.held(keys::Down);
    in.dt = dt;
    g_nav->tick_typeahead(in);
  }
}

bool fire_action(std::string_view key) {
  InputAction* a = g_input.find(key);
  if (!a) return false;
  bool consumed = a->category() == InputCategory::UI && g_nav && g_nav->on_action(a->key());
  if (!consumed) a->invoke_performed();
  return true;
}

std::string action_keys() {
  std::string s;
  for (auto& a : g_input.actions()) s += a->key() + "\n";
  return s;
}

std::string gui_dump() { return g_nav ? g_nav->dump() : "(no navigator)\n"; }

void shutdown() {
  g_inited = false;
  g_nav.reset();
  hooks::set_game_key_filter({});
  if (g_owns_keyboard) hooks::set_game_keys_muted(false);
  g_owns_keyboard = false;
}
}  // namespace gd::app
