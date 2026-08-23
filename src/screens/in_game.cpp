#include "screens/in_game.h"
#include <windows.h>
#include <cmath>
#include <format>
#include "app.h"
#include "audio.h"
#include "combat.h"
#include "rooms.h"
#include "core/graph_builder.h"
#include "core/message_builder.h"
#include "core/strings.h"
#include "log.h"
#include "speech.h"
#include "exe_ui.h"
#include "hooks.h"
#include "textcap.h"
#include "world.h"

namespace gd::screens {
using namespace gd::core;

namespace walltones {
// The wotr wall tones, ported as-is: four AUTHORED loops (assets/audio/walltones/1: north.wav ahead and
// south.wav behind both centred, east.wav hard right, west.wav hard left), volume = (1 - d/range)^2 of the
// free distance in that ear-fixed direction, applied directly (no smoothing, like wotr's WallTones
// channel). Probed every 100 ms; each probe walks the navmesh in kStep. Tone set 2 (obstacles) is vendored
// but unused until a wall/obstacle classifier exists here.
// Range: wotr's default is 15 ft (4.6 units; a Grim Dawn unit is about a metre), tuned for dungeon corridors.
// Grim Dawn's outdoors are wide open, so 10 by the user's ear on 2026-08-21 (live-tunable: /walltones?range=).
static float g_range = 10.0f;
static float g_gain = 1.0f;  // wotr's wall-tone volume setting (its default is 60 %; start hot, tune by ear)
constexpr float kStep = 0.5f;
constexpr const char* kFile[4] = {"north.wav", "east.wav", "south.wav", "west.wav"};  // forward, right, back, left
void set_range(float units) { if (units > 0.5f) g_range = units; }
void set_gain(float gain) { g_gain = gain < 0 ? 0 : gain > 1 ? 1 : gain; }
constexpr float kPan[4] = {0.0f, 1.0f, 0.0f, -1.0f};
constexpr int kToneId = 100;
static bool g_enabled = true;
static bool g_loaded = false;
static float g_dist[4] = {g_range, g_range, g_range, g_range};
static double g_last = 0;

constexpr int kObstacleToneId = 110;  // tone set 2: blockers the character just walks round
static void ensure_loaded() {
  if (g_loaded) return;
  g_loaded = true;
  std::string dir = audio::module_dir() + "assets\\audio\\walltones\\";
  for (int i = 0; i < 4; ++i) {
    audio::load_loop(kToneId + i, dir + "1\\" + kFile[i], kPan[i]);
    audio::load_loop(kObstacleToneId + i, dir + "2\\" + kFile[i], kPan[i]);
  }
}
static void silence() { for (int i = 0; i < 4; ++i) { audio::set_loop_volume(kToneId + i, 0.0f); audio::set_loop_volume(kObstacleToneId + i, 0.0f); } }
void set_enabled(bool on) { g_enabled = on; if (!on) silence(); }
bool enabled() { return g_enabled; }
// Screen-up in world xz for the camera yaw, measured 2026-08-21 against the game's own WASD movement at the
// default yaw 0.8727: W moved along (-0.776, -0.631) = (-sin yaw, -cos yaw), D along (0.642, -0.766) =
// (cos yaw, -sin yaw).
static void forward_vec(float yaw, float& fx, float& fz) { fx = -std::sin(yaw); fz = -std::cos(yaw); }
static void right_vec(float yaw, float& rx, float& rz) { rx = std::cos(yaw); rz = -std::sin(yaw); }

static void tick() {
  // Every frame, like wotr (the 100 ms throttle was audible as lag); ~80 navmesh point tests per frame.
  double t = app::now();
  g_last = t;
  if (!g_enabled || !world::in_world()) { silence(); return; }
  ensure_loaded();
  // Follow the game's own focus behaviour: it mutes when its window is not the foreground, so do we.
  HWND fg = GetForegroundWindow();
  bool audible = fg && fg == FindWindowA("Grim Dawn", nullptr);
  float yaw = world::camera_yaw();
  float fx, fz, rx, rz; forward_vec(yaw, fx, fz); right_vec(yaw, rx, rz);
  const float dirs[4][2] = {{fx, fz}, {rx, rz}, {-fx, -fz}, {-rx, -rz}};  // forward, right, back, left
  world::Vec3 me; world::player_position(me);
  for (int i = 0; i < 4; ++i) {
    float d = world::free_distance(dirs[i][0], dirs[i][1], g_range, kStep);
    g_dist[i] = d;
    float v = d >= g_range ? 0.0f : 1.0f - d / g_range;
    float vol = audible ? v * v * g_gain : 0.0f;
    // Which bank: a blocker shorter than a character is an obstacle (set 2), otherwise a wall (set 1).
    bool obstacle = false;
    if (vol > 0.0f) {
      world::Vec3 stop{me.x + dirs[i][0] * (d + kStep * 0.5f), me.y, me.z + dirs[i][1] * (d + kStep * 0.5f)};
      obstacle = world::classify_block(stop, dirs[i][0], dirs[i][1]) == world::BlockKind::Obstacle;
    }
    audio::set_loop_volume(kToneId + i, obstacle ? 0.0f : vol);
    audio::set_loop_volume(kObstacleToneId + i, obstacle ? vol : 0.0f);
  }
}
std::string status() {
  return std::format("enabled={} range={:.2f} vol={:.2f} forward={:.1f} right={:.1f} back={:.1f} left={:.1f}\n", g_enabled, g_range, g_gain, g_dist[0], g_dist[1], g_dist[2], g_dist[3]);
}
}  // namespace walltones

void speak_where() {
  world::Vec3 p;
  if (!world::player_position(p)) { speech::speak(strings::kNotInWorld, true); return; }
  MessageBuilder m;
  strings::push_where(m, p.x, p.z, world::region_name(), world::life(), world::life_max());
  speech::speak(m.build(), true);
}

class InGameScreen : public Screen {
 public:
  std::string_view key() const override { return "in_game"; }
  // The player is live AND the exe's world screen holds its InGameUI (null at the menus; the engine-side
  // player/controller pointers outlive a trip back to the main menu).
  bool is_active() override { return world::in_world() && exe_ui::ingame_ui() != nullptr; }
  std::string screen_name() const override { return std::string(strings::kInGame); }
  int layer() const override { return 0; }
  // We own the keyboard in the world: the frequent game keys pass straight through (movement, quickbar,
  // evade, potions, interact, the menu key, the show-items modifier, pets), every other game function is
  // lifted to Ctrl + its default key by app.cpp's "game.*" actions (which inject the plain key), and the
  // rest of the keyboard is ours. See docs/controls.md for the game's defaults.
  bool owns_keyboard() const override { return true; }
  bool passes_key(int code) const override {
    static const int direct[] = {0x11, 0x1e, 0x1f, 0x20,                                       // W A S D
                                 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b,   // 1..9 0 quickbar
                                 0x39, 0x12, 0x13, 0x16, 0x01,                                 // Space evade, E energy, R health, U interact, Escape menu
                                 0x38, 0x76,                                                   // Alt / Right Alt: show items (held)
                                 0x3c, 0x3d, 0x3e, 0x3f, 0x40, 0x41};                          // F2..F7 pets
    for (int k : direct) if (k == code) return true;
    return false;
  }
  bool allows_typeahead() const override { return false; }
  bool start_unfocused() const override { return true; }
  std::vector<InputCategory> input_categories() const override { return {InputCategory::InGame}; }
  void build(GraphBuilder&) override {}
  void on_update() override {
    world::pin_camera();
    world::tick();
    combat::tick();
    rooms::tick();
    walltones::tick();
    // The mouse buttons as keys, with real hold semantics: J (or Enter) = left, I = right, for as long as the
    // key is down and no modifier is held (Ctrl+J/I are lifted game keys).
    constexpr int kJ = 0x24, kI = 0x17, kEnter = 0x1c;
    const KeySource& ks = hooks::key_source();
    bool mods = ks.ctrl() || ks.shift() || ks.alt();
    world::mouse_key(1, !mods && (ks.held(kJ) || ks.held(kEnter)));
    world::mouse_key(2, !mods && ks.held(kI));
  }
  void on_unfocus() override { walltones::silence(); world::mouse_key(1, false); world::mouse_key(2, false); }
  void on_pop() override { walltones::silence(); world::mouse_key(1, false); world::mouse_key(2, false); rooms::reset(); }
};

std::unique_ptr<Screen> make_in_game() { return std::make_unique<InGameScreen>(); }
}  // namespace gd::screens
