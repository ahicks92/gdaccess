#include "screens/in_game.h"
#include <windows.h>
#include <cmath>
#include <format>
#include "app.h"
#include "audio.h"
#include "casts.h"
#include "combat.h"
#include "rooms.h"
#include "screens/hotbar_manager.h"
#include "screens/pets.h"
#include "screens/quickbar.h"
#include "sonar.h"
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
// Loudness match (measured 2026-08-22, A-weighted over the mono decode the mixer plays): the files are RMS-matched
// (bank 1 all -23.0 dBFS, bank 2 all -20.0) but pitched very differently (bank 1: north 942 Hz, east/west ~487,
// south 248; bank 2: 1034/759/479/856), so at equal dBFS south sounds 7.7 dB (bank 1) / 4.8 dB (bank 2) softer
// than north, and bank 2 is 3.8-6.7 dB louder than bank 1. These trims bring every file to bank 1 north's
// -23.4 dB(A); the loudest resulting peak is -3.6 dBFS. Order = kFile (north, east, south, west), in dB.
// Reversible: /walltones?trim=off zeroes them, ?trim=default restores, ?trim=<bank 1|2>,<n|e|s|w>,<dB> sets one.
constexpr float kDefaultTrimDb[2][4] = {{0.0f, 2.7f, 7.7f, 2.7f}, {-3.8f, -2.7f, 1.0f, -2.7f}};
static float g_trim_db[2][4] = {{0.0f, 2.7f, 7.7f, 2.7f}, {-3.8f, -2.7f, 1.0f, -2.7f}};
static float db_to_gain(float db) { return std::pow(10.0f, db / 20.0f); }
static void apply_trims() {
  for (int i = 0; i < 4; ++i) {
    audio::set_loop_gain(kToneId + i, db_to_gain(g_trim_db[0][i]));
    audio::set_loop_gain(kObstacleToneId + i, db_to_gain(g_trim_db[1][i]));
  }
}
float trim_gain(int bank, int dir) { return (bank == 1 || bank == 2) && dir >= 0 && dir < 4 ? db_to_gain(g_trim_db[bank - 1][dir]) : 1.0f; }
void set_trim(int bank, int dir, float db) {   // bank 1|2, dir 0..3 = north east south west; bank 0 = all off, -1 = defaults
  if (bank == 0) { for (auto& b : g_trim_db) for (float& t : b) t = 0.0f; }
  else if (bank == -1) { for (int b = 0; b < 2; ++b) for (int i = 0; i < 4; ++i) g_trim_db[b][i] = kDefaultTrimDb[b][i]; }
  else if ((bank == 1 || bank == 2) && dir >= 0 && dir < 4) g_trim_db[bank - 1][dir] = db;
  if (g_loaded) apply_trims();
}
static void ensure_loaded() {
  if (g_loaded) return;
  g_loaded = true;
  std::string dir = audio::module_dir() + "assets\\audio\\walltones\\";
  for (int i = 0; i < 4; ++i) {
    audio::load_loop(kToneId + i, dir + "1\\" + kFile[i], kPan[i]);
    audio::load_loop(kObstacleToneId + i, dir + "2\\" + kFile[i], kPan[i]);
  }
  apply_trims();
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
std::string probe_timing(int iters) {   // dev: time the navmesh probing part of one tick (no audio writes)
  if (iters < 1) iters = 1;
  if (!world::in_world()) return "not in world\n";
  float yaw = world::camera_yaw();
  float fx, fz, rx, rz; forward_vec(yaw, fx, fz); right_vec(yaw, rx, rz);
  const float dirs[4][2] = {{fx, fz}, {rx, rz}, {-fx, -fz}, {-rx, -rz}};
  world::Vec3 me; world::player_position(me);
  int probes = 0;   // count on_navmesh calls this pass (free_distance stops at the first block, so it varies)
  LARGE_INTEGER freq, t0, t1;
  QueryPerformanceFrequency(&freq);
  QueryPerformanceCounter(&t0);
  for (int n = 0; n < iters; ++n) {
    for (int i = 0; i < 4; ++i) {
      float d = world::free_distance(dirs[i][0], dirs[i][1], g_range, kStep);
      if (n == 0) probes += (d >= g_range ? (int)(g_range / kStep) : (int)(d / kStep) + 1);
      if (d < g_range) {
        world::Vec3 stop{me.x + dirs[i][0] * (d + kStep * 0.5f), me.y, me.z + dirs[i][1] * (d + kStep * 0.5f)};
        world::classify_block(stop, dirs[i][0], dirs[i][1]);
        if (n == 0) probes += 2;
      }
    }
  }
  QueryPerformanceCounter(&t1);
  double us = (double)(t1.QuadPart - t0.QuadPart) * 1e6 / (double)freq.QuadPart / iters;
  return std::format("range={:.1f} step={:.2f} on_navmesh_calls~{} iters={} avg={:.1f} us/tick ({:.3f} ms)\n",
                     g_range, kStep, probes, iters, us, us / 1000.0);
}
std::string status() {
  return std::format("enabled={} range={:.2f} vol={:.2f} forward={:.1f} right={:.1f} back={:.1f} left={:.1f}\n"
                     "trim dB (north east south west): walls {:+.1f} {:+.1f} {:+.1f} {:+.1f}; obstacles {:+.1f} {:+.1f} {:+.1f} {:+.1f}\n",
                     g_enabled, g_range, g_gain, g_dist[0], g_dist[1], g_dist[2], g_dist[3],
                     g_trim_db[0][0], g_trim_db[0][1], g_trim_db[0][2], g_trim_db[0][3], g_trim_db[1][0], g_trim_db[1][1], g_trim_db[1][2], g_trim_db[1][3]);
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
                                 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b,   // 1..9 0 quickbar slots
                                 0x15,                                                         // Y quickbar switch (the game's own; we announce the page in quickbar_tick)
                                 0x39, 0x12, 0x13, 0x16, 0x01,                                 // Space evade, E energy, R health, U interact, Escape menu
                                 0x38, 0x76};                                                  // Alt / Right Alt: show items (held). F2..F7 (the game's pet selection) and Backspace (pet display) are OURS (screens/pets.cpp)
    for (int k : direct) if (k == code) return true;
    return false;
  }
  bool allows_typeahead() const override { return false; }
  bool start_unfocused() const override { return true; }
  std::vector<InputCategory> input_categories() const override { return {InputCategory::InGame, InputCategory::Lifted}; }
  void build(GraphBuilder&) override {}
  // A mouse-button key (J/Enter = left, I = right) that was ALREADY held the moment we regained focus is a
  // carry-over from the screen that just closed (e.g. Enter activated a map-marker row, which closed the map
  // and re-exposed us with Enter still physically down) -- not a fresh world click. Latch it and ignore it
  // until it is released once. Protects every close-to-world screen (map, pause menu, conversation), not just
  // the map. Fresh holds (incl. hold-to-attack) are unaffected.
  void on_focus() override {
    Screen::on_focus();
    const KeySource& ks = hooks::key_source();
    suppress_left_ = ks.held(0x24) || ks.held(0x1c);   // J or Enter
    suppress_right_ = ks.held(0x17);                    // I
  }
  void on_update() override {
    world::pin_camera();
    world::tick();
    combat::tick();
    casts::tick();
    rooms::tick();
    quickbar_tick();
    weapon_swap_tick();
    pets_tick();
    world::reping_tick();   // re-sound the review ping when the target's route (straight/path/unreachable) changes
    world::show_all_tick();
    sonar::tick();
    walltones::tick();
    // The mouse buttons as keys, with real hold semantics: J (or Enter) = left, I = right, for as long as the
    // key is down and no modifier is held (Ctrl+J/I are lifted game keys).
    constexpr int kJ = 0x24, kI = 0x17, kEnter = 0x1c;
    const KeySource& ks = hooks::key_source();
    bool mods = ks.ctrl() || ks.shift() || ks.alt();
    bool left = !mods && (ks.held(kJ) || ks.held(kEnter));
    bool right = !mods && ks.held(kI);
    if (suppress_left_) { if (!left) suppress_left_ = false; else left = false; }     // clear on release, else swallow
    if (suppress_right_) { if (!right) suppress_right_ = false; else right = false; }
    world::mouse_key(1, left);
    world::mouse_key(2, right);
  }
  void on_unfocus() override { walltones::silence(); world::mouse_key(1, false); world::mouse_key(2, false); }
  void on_pop() override { walltones::silence(); world::mouse_key(1, false); world::mouse_key(2, false); rooms::reset(); sonar::reset(); quickbar_reset(); pets_reset(); }

 private:
  bool suppress_left_ = false;   // a left-mouse key held over from the screen that just closed
  bool suppress_right_ = false;
};

std::unique_ptr<Screen> make_in_game() { return std::make_unique<InGameScreen>(); }
}  // namespace gd::screens
