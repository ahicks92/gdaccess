#include "devserver.h"
#include "hooks.h"
#include "app.h"
#include "log.h"
#include "speech.h"
#include "textcap.h"
#include "world.h"
#include "exe_ui.h"
#include "audio.h"
#include "audio_mute.h"
#include "combat.h"
#include "rooms.h"
#include "voice.h"
#include "screens/in_game.h"
#include "screens/inventory.h"
#include "screens/quickbar.h"
#include "gameapi.h"
#include <cmath>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <thread>

namespace gd::dev {
static std::atomic<bool> g_run{false};
static SOCKET g_listen = INVALID_SOCKET;
static std::thread g_thread;

static std::string url_decode(std::string_view s) {
  std::string o;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '+') o += ' ';
    else if (s[i] == '%' && i + 2 < s.size()) { o += (char)strtol(std::string(s.substr(i + 1, 2)).c_str(), nullptr, 16); i += 2; }
    else o += s[i];
  }
  return o;
}
static std::map<std::string, std::string> parse_query(std::string_view q) {
  std::map<std::string, std::string> m;
  size_t p = 0;
  while (p <= q.size()) {
    size_t e = q.find('&', p); if (e == std::string_view::npos) e = q.size();
    std::string_view kv = q.substr(p, e - p);
    size_t eq = kv.find('=');
    if (!kv.empty()) m[url_decode(kv.substr(0, eq))] = eq == std::string_view::npos ? "1" : url_decode(kv.substr(eq + 1));
    p = e + 1;
  }
  return m;
}
static int parse_int(const std::string& s, int def = 0) {
  if (s.empty()) return def;
  return (int)strtol(s.c_str(), nullptr, s.rfind("0x", 0) == 0 ? 16 : 10);
}
static bool truthy(const std::string& s) { return s == "1" || s == "true" || s == "on" || s == "yes"; }

// Key names -> the game's Button enum (DIK scancodes for plain keys; extended keys measured from the game).
static int key_code(const std::string& name) {
  static const std::map<std::string, int> k = {
    {"escape", 0x01}, {"esc", 0x01}, {"1", 0x02}, {"2", 0x03}, {"3", 0x04}, {"4", 0x05}, {"5", 0x06}, {"6", 0x07}, {"7", 0x08}, {"8", 0x09}, {"9", 0x0a}, {"0", 0x0b},
    {"minus", 0x0c}, {"equals", 0x0d}, {"backspace", 0x0e}, {"tab", 0x0f},
    {"q", 0x10}, {"w", 0x11}, {"e", 0x12}, {"r", 0x13}, {"t", 0x14}, {"y", 0x15}, {"u", 0x16}, {"i", 0x17}, {"o", 0x18}, {"p", 0x19},
    {"enter", 0x1c}, {"return", 0x1c}, {"ctrl", 0x1d}, {"lctrl", 0x1d}, {"rctrl", 0x6b}, {"insert", 0x80}, {"delete", 0x81},
    {"a", 0x1e}, {"s", 0x1f}, {"d", 0x20}, {"f", 0x21}, {"g", 0x22}, {"h", 0x23}, {"j", 0x24}, {"k", 0x25}, {"l", 0x26},
    {"lshift", 0x2a}, {"z", 0x2c}, {"x", 0x2d}, {"c", 0x2e}, {"v", 0x2f}, {"b", 0x30}, {"n", 0x31}, {"m", 0x32},
    {"rshift", 0x36}, {"lalt", 0x38}, {"alt", 0x38}, {"ralt", 0x76}, {"space", 0x39}, {"capslock", 0x3a},
    {"f1", 0x3b}, {"f2", 0x3c}, {"f3", 0x3d}, {"f4", 0x3e}, {"f5", 0x3f}, {"f6", 0x40}, {"f7", 0x41}, {"f8", 0x42}, {"f9", 0x43}, {"f10", 0x44},
    {"f11", 0x57}, {"f12", 0x58},
    {"home", 0x78}, {"up", 0x79}, {"pageup", 0x7a}, {"left", 0x7b}, {"right", 0x7c}, {"end", 0x7d}, {"down", 0x7e}, {"pagedown", 0x7f},  // from the game's own key names
    {"lbracket", 0x1a}, {"rbracket", 0x1b}, {"semicolon", 0x27}, {"apostrophe", 0x28}, {"grave", 0x29}, {"backslash", 0x2b},
    {"comma", 0x33}, {"period", 0x34}, {"slash", 0x35}, {"numpadplus", 0x4e}, {"numpadminus", 0x4a},
  };
  auto it = k.find(name);
  return it == k.end() ? -1 : it->second;
}

static std::string handle(const std::string& path, const std::map<std::string, std::string>& q, const std::string& body, int& status) {
  status = 200;
  if (path == "/health")
    return std::format("ok pid={} frame={} backend={} speech_muted={} game_keys_muted={} keyboard={}\n", GetCurrentProcessId(), hooks::frame(),
                       speech::backend_name(), speech::muted(), hooks::game_keys_muted(), app::owns_keyboard() ? "mod" : "game") + hooks::counters() + "\n";
  if (path == "/voices") return voice::status() + "rolloff: " + world::voice_rolloff() + "\n";  // the positional voices: state, bound voices, counters, the installed list
  if (path == "/voice") {  // /voice?since=N the spoken-line ring; ?say=..&voice=mark|zira&pan=&gain=&replace=1 a test line; knobs enable= vol=
    if (q.count("enable")) voice::set_enabled(truthy(q.at("enable")));
    if (q.count("vol")) voice::set_gain((float)atof(q.at("vol").c_str()));
    if (q.count("coalesce")) combat::set_coalesce(truthy(q.at("coalesce")));
    if (q.count("window")) combat::set_window(atof(q.at("window").c_str()));
    if (q.count("cap")) combat::set_cap(parse_int(q.at("cap"), 4));
    if (q.count("near") || q.count("far") || q.count("floor"))
      world::set_voice_rolloff(q.count("near") ? (float)atof(q.at("near").c_str()) : -1.0f, q.count("far") ? (float)atof(q.at("far").c_str()) : -1.0f, q.count("floor") ? (float)atof(q.at("floor").c_str()) : -1.0f);
    if (q.count("max")) voice::set_max_concurrent(q.count("voice") && q.at("voice") == "zira" ? voice::Which::Zira : voice::Which::Mark, parse_int(q.at("max"), 4));
    if (q.count("say")) {
      voice::Say s;
      s.voice = q.count("voice") && q.at("voice") == "zira" ? voice::Which::Zira : voice::Which::Mark;
      s.text = q.at("say");
      s.pan = q.count("pan") ? (float)atof(q.at("pan").c_str()) : 0.0f;
      s.gain = q.count("gain") ? (float)atof(q.at("gain").c_str()) : 1.0f;
      s.policy = q.count("replace") && truthy(q.at("replace")) ? voice::Policy::Replace : voice::Policy::Overlap;
      s.group = s.voice == voice::Which::Zira ? voice::kGroupSelf : voice::kGroupEnemy;
      voice::say(s);
      return "queued\n";
    }
  }
  if (path == "/combat") {  // /combat?raw=N arms a hex dump of the next N 0x1b events into /log
    if (q.count("raw")) combat::arm_raw_log(parse_int(q.at("raw"), 10));
    return combat::status();
  }
  if (path == "/speech" || path == "/log" || path == "/voice") {
    auto& ring = path == "/speech" ? speech::history() : path == "/voice" ? voice::history() : log::ring();
    uint64_t since = q.count("since") ? strtoull(q.at("since").c_str(), nullptr, 10) : 0;
    auto [lines, cur] = ring.since(since);
    std::string out = std::format("cursor: {}\n", cur);
    for (auto& [i, l] : lines) out += std::format("{}: {}\n", i, l);
    return out;
  }
  if (path == "/text") {
    std::string out;
    for (auto& it : textcap::snapshot())
      out += std::format("{}\t{}\t{}\t{}\t{:08x}\t{}\t{}\n", it.x, it.y, it.xalign, it.yalign, it.rgba, it.variant, textcap::speakable(it.text));
    return out.empty() ? "(no text)\n" : out;
  }
  if (path == "/say") { std::string t = q.count("text") ? q.at("text") : body; speech::speak(t, truthy(q.count("interrupt") ? q.at("interrupt") : "1")); return "ok\n"; }
  if (path == "/mute") {
    bool on = truthy(q.count("on") ? q.at("on") : "1");
    speech::set_muted(on);
    if (!on) SetEnvironmentVariableW(L"GDACCESS_MUTE", nullptr);  // or the next hot reload re-mutes (dllmain reads it)
    return std::format("speech_muted={}\n", speech::muted());
  }
  if (path == "/audiomute") {  // the process's WASAPI session (game sounds AND our tones): /audiomute?on=0 to hear it
    bool on = truthy(q.count("on") ? q.at("on") : "1");
    if (!on) SetEnvironmentVariableW(L"GDACCESS_MUTE", nullptr);
    return std::format("audio_muted={} ok={}\n", on, audio::mute_process(on));
  }
  if (path == "/gamekeys") { hooks::set_game_keys_muted(truthy(q.count("on") ? q.at("on") : "1")); return std::format("game_keys_muted={}\n", hooks::game_keys_muted()); }
  if (path == "/key") {
    int code = q.count("name") ? key_code(q.at("name")) : parse_int(q.count("code") ? q.at("code") : "", -1);
    if (code < 0) { status = 400; return "unknown key\n"; }
    char16_t ch = 0;
    if (q.count("ch") && !q.at("ch").empty()) { std::string c = q.at("ch"); wchar_t w[4]; MultiByteToWideChar(CP_UTF8, 0, c.c_str(), -1, w, 4); ch = (char16_t)w[0]; }
    bool shift = q.count("shift") && truthy(q.at("shift")), ctrl = q.count("ctrl") && truthy(q.at("ctrl")), alt = q.count("alt") && truthy(q.at("alt"));
    hooks::push_key(code, shift, ctrl, alt, ch);
    return std::format("queued key {:#x}\n", code);
  }
  if (path == "/keys") {
    std::string t = q.count("text") ? q.at("text") : body;
    int n = 0;
    for (char c : t) {
      std::string name(1, (char)tolower((unsigned char)c));
      if (c == ' ') name = "space";
      int code = key_code(name);
      if (code < 0) continue;
      hooks::push_key(code, isupper((unsigned char)c) != 0, false, false, (char16_t)c);
      ++n;
    }
    return std::format("queued {} keys\n", n);
  }
  if (path == "/cursor") {
    if (q.count("clear")) { hooks::set_cursor_override(false, 0, 0); return "cursor override cleared\n"; }
    hooks::set_cursor_override(true, (float)atof(q.at("x").c_str()), (float)atof(q.at("y").c_str()));
    return "cursor override set\n";
  }
  if (path == "/buttons") return hooks::button_query_stats();
  if (path == "/gui") return app::gui_dump();
  if (path == "/actions") return app::action_keys();
  if (path == "/action") { std::string k = q.count("key") ? q.at("key") : ""; if (!app::fire_action(k)) { status = 400; return "unknown action; see /actions\n"; } return "fired\n"; }
  if (path == "/keynames") return hooks::button_names(q.count("max") ? parse_int(q.at("max"), 0x120) : 0x120);
  if (path == "/fakeactive") { hooks::set_fake_active(truthy(q.count("on") ? q.at("on") : "1")); return "ok\n"; }
  if (path == "/click") {
    if (!q.count("x") || !q.count("y")) { status = 400; return "need x and y\n"; }
    int button = q.count("button") ? (q.at("button") == "right" ? 2 : q.at("button") == "middle" ? 3 : parse_int(q.at("button"), 1)) : 1;
    hooks::click((float)atof(q.at("x").c_str()), (float)atof(q.at("y").c_str()), button);
    return "queued click\n";
  }
  if (path == "/player") return world::debug_dump();
  if (path == "/classinfo") return world::classinfo_dump();
  if (path == "/scan") {  // /scan?group=0..3&max=40 -- the review cursor's list for a group (enemies, people, bystanders, objects)
    int g = q.count("group") ? parse_int(q.at("group"), 3) : 3;
    float r = q.count("max") ? (float)atof(q.at("max").c_str()) : 40.0f;
    std::string out;
    for (const world::ScanItem& it : world::scan((world::ScanGroup)g, r)) out += std::format("{:6.1f} id={} {} '{}' {}\n", it.dist, it.id, it.cls, it.label, it.record);
    return out.empty() ? "nothing\n" : out;
  }
  if (path == "/keydown" || path == "/keyup") {  // hold a key across frames: /keydown?name=w ... /keyup?name=w
    int code = q.count("name") ? key_code(q.at("name")) : parse_int(q.count("code") ? q.at("code") : "", -1);
    if (code < 0) { status = 400; return "unknown key\n"; }
    hooks::push_key_event({code, path == "/keyup", false, false, false, 0});
    return std::format("queued {} {:#x}\n", path == "/keyup" ? "up" : "down", code);
  }
  if (path == "/walltones") {
    if (q.count("on")) screens::walltones::set_enabled(truthy(q.at("on")));
    if (q.count("range")) screens::walltones::set_range((float)atof(q.at("range").c_str()));  // world units
    if (q.count("vol")) screens::walltones::set_gain((float)atof(q.at("vol").c_str()));        // 0..1
    return screens::walltones::status();
  }
  if (path == "/where") { screens::speak_where(); return "ok\n"; }
  if (path == "/blocks") return world::blocks_dump();
  if (path == "/room") {      // the rooms feature: current place + exits; ?dwell=ms ?untitled=0|1 ?say=1 (re-announce) ?reload=1
    if (q.count("dwell")) rooms::set_dwell_ms(parse_int(q.at("dwell"), 400));
    if (q.count("settle")) rooms::set_settle_ms(parse_int(q.at("settle"), 1000));
    if (q.count("untitled")) rooms::set_say_untitled(truthy(q.at("untitled")));
    if (q.count("reload")) rooms::reload();
    if (q.count("say")) rooms::announce_now();
    return rooms::status();
  }
  if (path == "/regions") return world::regions_dump(parse_int(q.count("max") ? q.at("max") : "40", 40));   // engine Regions (chunks): name, offset, loaded, portals; ?max=
  if (path == "/portals") return world::portals_dump();   // the player's chunk's portals
  if (path == "/teleport") {  // /teleport?x=&z= -- the player, floored (authoring; docs/rooms.md M4)
    if (!q.count("x") || !q.count("z")) { status = 400; return "need x and z\n"; }
    return world::teleport((float)atof(q.at("x").c_str()), (float)atof(q.at("z").c_str()), q.count("check") > 0);   // &check=1: report only
  }
  if (path == "/project" && q.count("pts")) {  // /project?pts=x,z;x,z;... -- ground points to screen (or POST the list)
    std::vector<world::Vec3> pts;
    std::string s = q.at("pts");
    size_t i = 0;
    while (i < s.size()) {
      size_t e = s.find(';', i); if (e == std::string::npos) e = s.size();
      std::string pair = s.substr(i, e - i); size_t c = pair.find(',');
      if (c != std::string::npos) pts.push_back({(float)atof(pair.substr(0, c).c_str()), 0.f, (float)atof(pair.substr(c + 1).c_str())});
      i = e + 1;
    }
    return world::project_points(pts);
  }
  if (path == "/fog") {       // /fog?x=&z=&radius=N -- reveal around a point (the dev character's map)
    if (!q.count("x") || !q.count("z")) { status = 400; return "need x and z\n"; }
    return world::fog_reveal((float)atof(q.at("x").c_str()), (float)atof(q.at("z").c_str()), parse_int(q.count("radius") ? q.at("radius") : "20", 20));
  }
  if (path == "/navprobe") {  // /navprobe?x0=&z0=&x1=&z1=&step=0.5 -- IsPointOnPathMesh over a grid, '#' walkable, rows = z ascending
    auto f = [&](const char* k, float d) { return q.count(k) ? (float)atof(q.at(k).c_str()) : d; };
    return world::navprobe(f("x0", 0), f("z0", 0), f("x1", 0), f("z1", 0), f("step", 0.5f));
  }
  if (path == "/conv") return world::conversation_dump();
  if (path == "/ui") return exe_ui::ui_dump();            // the exe's menu widget tree (framework A)
  if (path == "/ui/activate") {                           // /ui/activate?ptr=0x... presses a framework A button through its listeners
    uintptr_t p = q.count("ptr") ? (uintptr_t)strtoull(q.at("ptr").c_str(), nullptr, 0) : 0;
    return exe_ui::activate_ptr(p) ? "activated\n" : "not a button in the current tree (see /ui)\n";
  }
  // ---- the in-world windows' model (src/gameapi.h) ----
  if (path == "/quests") { if (q.count("track")) return gameapi::set_quest_tracked((void*)strtoull(q.at("track").c_str(), nullptr, 0), !q.count("off")) ? "ok\n" : "failed\n"; return gameapi::dump_quests(parse_int(q.count("filter") ? q.at("filter") : "0", 0)); }
  if (path == "/objectives") { std::string out; for (const std::string& l : gameapi::objectives()) out += l + "\n"; return out.empty() ? "no objectives\n" : out; }
  if (path == "/factions") return gameapi::dump_factions();
  if (path == "/hotbar") {   // ?assign=<slot index>&skill=<id> | ?primary=<id> | ?secondary=<id> | ?activate=<index> | ?tip=<index> | ?base=&stride= (quickbar layout knob)
    if (q.count("base")) screens::set_quickbar_base((unsigned)parse_int(q.at("base"), 0), (unsigned)parse_int(q.count("stride") ? q.at("stride") : "10", 10));
    if (q.count("assign")) return gameapi::assign_skill_to_slot((unsigned)parse_int(q.at("assign"), 0), (unsigned)parse_int(q.count("skill") ? q.at("skill") : "0", 0)) ? "ok\n" : "failed\n";
    if (q.count("primary")) return gameapi::set_primary_skill((unsigned)parse_int(q.at("primary"), 0)) ? "ok\n" : "failed\n";
    if (q.count("secondary")) return gameapi::set_secondary_skill((unsigned)parse_int(q.at("secondary"), 0)) ? "ok\n" : "failed\n";
    if (q.count("activate")) return gameapi::activate_hotslot((unsigned)parse_int(q.at("activate"), 0)) ? "ok\n" : "failed\n";
    if (q.count("tip")) { std::string out; for (const std::string& l : gameapi::hotslot_tooltip((unsigned)parse_int(q.at("tip"), 0))) out += l + "\n"; return out.empty() ? "no text\n" : out; }
    return gameapi::dump_hotslots();
  }
  if (path == "/lore") { if (q.count("read")) { std::string out; for (const std::string& l : gameapi::note_text(gameapi::object_by_id((unsigned)parse_int(q.at("read"), 0)))) out += l + "\n"; return out.empty() ? "no text\n" : out; } return gameapi::dump_lore(); }
  if (path == "/inv") {      // ?tip=<id>[&simple=1] | ?use=<id>[&source=N] | ?drop=<id> | ?unequip=<loc> | ?equip=<id>&loc=<loc> | ?bag=<n> | ?source=N (the UseItem ItemSource knob)
    if (q.count("source")) screens::set_bag_item_source(parse_int(q.at("source"), 0));
    if (q.count("tip")) { std::string out; for (const std::string& l : gameapi::item_tooltip(gameapi::object_by_id((unsigned)parse_int(q.at("tip"), 0)), q.count("simple"), q.count("details"))) out += l + "\n"; return out.empty() ? "no text\n" : out; }
    if (q.count("use")) return gameapi::use_item((unsigned)parse_int(q.at("use"), 0), screens::bag_item_source()) ? "ok\n" : "failed\n";
    if (q.count("drop")) return gameapi::drop_item((unsigned)parse_int(q.at("drop"), 0)) ? "ok\n" : "failed\n";
    if (q.count("unequip")) return gameapi::unequip(parse_int(q.at("unequip"), 0)) ? "ok\n" : "failed\n";
    if (q.count("equip")) return gameapi::equip((unsigned)parse_int(q.at("equip"), 0), parse_int(q.count("loc") ? q.at("loc") : "0", 0)) ? "ok\n" : "failed\n";
    if (q.count("bag")) return gameapi::select_bag(parse_int(q.at("bag"), 0)) ? "ok\n" : "failed\n";
    return std::format("item source knob {}\n", screens::bag_item_source()) + gameapi::dump_bags() + gameapi::dump_equipment();
  }
  if (path == "/skills") {   // ?tip=<id> | ?learn=<id> | ?refund=<id> | ?attr=1..3 (spend an attribute point) | ?pane=<mastery enum|80>&tab=0|1
    if (q.count("attr")) return gameapi::spend_attribute_point(parse_int(q.at("attr"), 0)) ? "ok\n" : "failed\n";
    if (q.count("pane")) return exe_ui::skills_set_pane(parse_int(q.count("tab") ? q.at("tab") : "0", 0), parse_int(q.at("pane"), 0)) ? "ok\n" : "failed\n";
    if (q.count("pickup")) return gameapi::pickup_item((unsigned)parse_int(q.at("pickup"), 0)) ? "ok\n" : "failed\n";
    if (q.count("tip")) { std::string out; for (const std::string& l : gameapi::skill_tooltip(gameapi::object_by_id((unsigned)parse_int(q.at("tip"), 0)))) out += l + "\n"; return out.empty() ? "no text\n" : out; }
    if (q.count("learn")) return gameapi::learn_skill(gameapi::object_by_id((unsigned)parse_int(q.at("learn"), 0))) ? "ok\n" : "failed\n";
    if (q.count("refund")) return gameapi::refund_skill(gameapi::object_by_id((unsigned)parse_int(q.at("refund"), 0))) ? "ok\n" : "failed\n";
    return gameapi::dump_skills();
  }
  if (path == "/sheet") return gameapi::dump_sheet();
  if (path == "/lua") { std::string code = q.count("code") ? q.at("code") : body; if (code.empty()) return "?code= or POST body\n"; return gameapi::lua_run(code) ? "ok\n" : "failed (see /log)\n"; }   // dev: a chunk in the game's Lua state
  if (path == "/cheat") return q.count("xp") ? (gameapi::dev_add_experience((unsigned)parse_int(q.at("xp"), 0)) ? "ok\n" : "failed\n") : std::string("?xp=N\n");
  if (path == "/obj") return q.count("id") ? gameapi::dump_object((unsigned)parse_int(q.at("id"), 0)) : gameapi::dump_objects_stats();
  if (path == "/loc2") return q.count("tag") ? gameapi::localize(q.at("tag")) + "\n" : std::string("?tag=\n");
  if (path == "/ingame") { if (q.count("action")) return exe_ui::ingame_key_action(parse_int(q.at("action"), 0)) ? "ok\n" : "failed\n"; return exe_ui::ingame_dump(); }    // InGameUI's windows and the prompt box (framework B)
  if (path == "/peek") {                                 // /peek?ptr=0x...&n=256 -- hex dump (dev; SEH-guarded)
    uintptr_t p = q.count("ptr") ? (uintptr_t)strtoull(q.at("ptr").c_str(), nullptr, 0) : 0;
    return exe_ui::peek(p, q.count("n") ? parse_int(q.at("n"), 256) : 256);
  }
  if (path == "/convwin") {                              // the game's conversation window: speaker/speech elements, rows with their steps
    exe_ui::ConvWindow w = exe_ui::conv_window();
    if (!w) return "no conversation window\n";
    std::string out = std::format("window {} open={} rect=({:.0f},{:.0f} {:.0f}x{:.0f}) speaker='{}' page='{}'\nspeech='{}'\n", w.p, (int)w.open(), w.rect().x, w.rect().y, w.rect().w, w.rect().h, w.speaker(), w.page_text(), w.speech());
    for (exe_ui::ConvRow r : w.rows()) out += std::format("  row {} step={} rect=({:.0f},{:.0f} {:.0f}x{:.0f}) '{}'\n", r.p, r.step(), r.rect().x, r.rect().y, r.rect().w, r.rect().h, r.text());
    out += exe_ui::conv_elements_dump();
    return out;
  }
  if (path == "/tips") {                                 // the game's tip manager: live tips with kind/state/lines; /tips?dismiss=1 closes tutorial tips
    std::string out;
    for (exe_ui::Tip t : exe_ui::tips()) {
      out += std::format("tip {} kind={} state={} page={}\n", t.p, t.kind(), t.state(), t.page());
      for (const std::string& l : t.lines()) out += "  | " + l + "\n";
      if (q.count("dismiss") && t.kind() == 1) t.dismiss();
    }
    return out.empty() ? "no tips\n" : out;
  }
  if (path == "/loc") return hooks::localize(q.count("tag") ? q.at("tag").c_str() : "") + "\n";  // /loc?tag=tagVideoTitle
  if (path == "/dialog") {                                // /dialog?answer=yes|no|okay answers the game's message box through DialogManager
    if (q.count("answer")) { std::string a = q.at("answer"); return exe_ui::answer_dialog(a == "yes" || a == "okay") ? "answered\n" : "no dialog open\n"; }
    return exe_ui::dialog_dump();
  }
  if (path == "/entities") return world::entities_dump(q.count("max") ? (float)atof(q.at("max").c_str()) : 40.0f);
  if (path == "/lock") {  // /lock?id=N parks the cursor over the entity each frame; /lock?off=1 releases; no args: report
    if (q.count("off")) world::unlock_target();
    else if (q.count("id")) { if (!world::lock_target((unsigned)strtoul(q.at("id").c_str(), nullptr, 10))) return "entity not found near the player\n"; }
    return std::format("locked={}\n", world::locked_target()) + world::target_dump();
  }
  if (path == "/project") return world::project_dump(q.count("id") ? (unsigned)strtoul(q.at("id").c_str(), nullptr, 10) : 0);
  if (path == "/target") {  // /target?id=N sets the combat enemy; /target?clear=1; no args: report
    if (q.count("clear")) world::clear_target();
    else if (q.count("id")) { if (!world::set_target((unsigned)strtoul(q.at("id").c_str(), nullptr, 10))) return "no controller\n"; }
    return world::target_dump();
  }
  if (path == "/nav") {  // free distance along a direction: /nav?dx=1&dz=0&max=15&step=0.5
    float dx = q.count("dx") ? (float)atof(q.at("dx").c_str()) : 1, dz = q.count("dz") ? (float)atof(q.at("dz").c_str()) : 0;
    float len = std::sqrt(dx * dx + dz * dz); if (len > 0) { dx /= len; dz /= len; }
    float mx = q.count("max") ? (float)atof(q.at("max").c_str()) : 15.0f, st = q.count("step") ? (float)atof(q.at("step").c_str()) : 0.5f;
    return std::format("free={:.2f}\n", world::free_distance(dx, dz, mx, st));
  }
  if (path == "/tone") {  // /tone?freq=440&ms=300&vol=0.5&pan=0  (ms=0: continuous tone id=99 until /tone?stop=1)
    if (q.count("stop")) { audio::stop_tone(99); return "stopped\n"; }
    float f = q.count("freq") ? (float)atof(q.at("freq").c_str()) : 440, v = q.count("vol") ? (float)atof(q.at("vol").c_str()) : 0.5f, pn = q.count("pan") ? (float)atof(q.at("pan").c_str()) : 0;
    int ms = q.count("ms") ? parse_int(q.at("ms"), 300) : 300;
    if (ms > 0) audio::beep(f, ms, v, pn); else audio::set_tone(99, f, v, pn);
    return std::format("audio_ready={}\n", audio::ready());
  }
  if (path == "/mouse") {
    hooks::SynthMouse m{};
    m.type = parse_int(q.count("type") ? q.at("type") : "9", 9);
    m.x = q.count("x") ? (float)atof(q.at("x").c_str()) : 0; m.y = q.count("y") ? (float)atof(q.at("y").c_str()) : 0;
    hooks::push_mouse_event(m);
    return "queued mouse event\n";
  }
  status = 404;
  return "unknown route\n";
}

struct JobCtx { const std::string* path; const std::map<std::string, std::string>* q; const std::string* body; int* status; std::string* out; };
static void run_job(JobCtx* c) { *c->out = handle(*c->path, *c->q, *c->body, *c->status); }
static int seh_filter(EXCEPTION_POINTERS* ep, void** addr, DWORD* code) {
  *addr = ep->ExceptionRecord->ExceptionAddress;
  *code = ep->ExceptionRecord->ExceptionCode;
  return EXCEPTION_EXECUTE_HANDLER;
}
static DWORD run_job_seh(JobCtx* c, void** addr) {  // no C++ objects with destructors in here (SEH rule)
  DWORD code = 0;
  __try { run_job(c); } __except (seh_filter(GetExceptionInformation(), addr, &code)) {}
  return code;
}
static std::string module_offset(void* addr) {
  HMODULE m = nullptr;
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)addr, &m);
  if (!m) return std::format("{}", addr);
  char path[MAX_PATH]; GetModuleFileNameA(m, path, MAX_PATH);
  const char* base = strrchr(path, '\\'); base = base ? base + 1 : path;
  return std::format("{}+{:#x}", base, (uintptr_t)addr - (uintptr_t)m);
}

static void serve(SOCKET c) {
  std::string req;
  char buf[4096];
  // read headers (and whatever body arrived with them)
  while (req.find("\r\n\r\n") == std::string::npos) {
    int n = recv(c, buf, sizeof buf, 0);
    if (n <= 0) return;
    req.append(buf, n);
    if (req.size() > 1 << 20) return;
  }
  size_t hdr_end = req.find("\r\n\r\n");
  std::string head = req.substr(0, hdr_end), body = req.substr(hdr_end + 4);
  size_t cl = head.find("Content-Length:");
  if (cl != std::string::npos) {
    size_t want = strtoul(head.c_str() + cl + 15, nullptr, 10);
    while (body.size() < want) { int n = recv(c, buf, sizeof buf, 0); if (n <= 0) break; body.append(buf, n); }
  }
  size_t sp1 = head.find(' '), sp2 = head.find(' ', sp1 + 1);
  std::string target = head.substr(sp1 + 1, sp2 - sp1 - 1);
  size_t qm = target.find('?');
  std::string path = target.substr(0, qm);
  auto query = parse_query(qm == std::string::npos ? "" : target.substr(qm + 1));
  // Routes poke at game objects with guessed layouts; a fault inside one must not take the game down.
  // The SEH frame lives in a function without C++ objects (run_job_seh); the report names the module.
  // The job owns everything it touches (shared with this frame): a route that outlives the 8 s timeout
  // still runs to completion later on the game thread and must not write into a dead stack frame.
  struct Owned { std::string path, body; std::map<std::string, std::string> query; int status = 500; std::string out; };
  auto owned = std::make_shared<Owned>();
  owned->path = path; owned->body = body; owned->query = query;
  bool ran = hooks::run_on_game_thread([owned] {
    JobCtx ctx{&owned->path, &owned->query, &owned->body, &owned->status, &owned->out};
    void* addr = nullptr;
    DWORD code = run_job_seh(&ctx, &addr);
    if (code) {
      owned->status = 500;
      owned->out = std::format("route faulted: exception {:#x} at {}\n", code, module_offset(addr));
      log::write(owned->out);
    }
  }, 8000);
  int status = 504;
  std::string out = "game thread did not run the job within 8 s (still queued/running; not ticking? unfocused with inactiveUpdateRate=0?)\n";
  if (ran) { status = owned->status; out = owned->out; }   // not touched otherwise: the job may still be writing it
  std::string resp = std::format("HTTP/1.1 {} {}\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: {}\r\nConnection: close\r\n\r\n",
                                 status, status == 200 ? "OK" : "ERR", out.size()) + out;
  send(c, resp.data(), (int)resp.size(), 0);
}

static void loop(int port) {
  WSADATA w; WSAStartup(MAKEWORD(2, 2), &w);
  g_listen = socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons((u_short)port); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  BOOL yes = TRUE; setsockopt(g_listen, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof yes);
  if (bind(g_listen, (sockaddr*)&a, sizeof a) != 0 || listen(g_listen, 8) != 0) { log::writef("devserver: bind/listen on {} failed: {}", port, WSAGetLastError()); return; }
  log::writef("devserver: listening on 127.0.0.1:{}", port);
  while (g_run) {
    SOCKET c = accept(g_listen, nullptr, nullptr);
    if (c == INVALID_SOCKET) break;
    DWORD to = 10000; setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (const char*)&to, sizeof to);
    serve(c);
    closesocket(c);
  }
}

void start(int port) { g_run = true; g_thread = std::thread(loop, port); }
void stop() {
  g_run = false;
  if (g_listen != INVALID_SOCKET) { closesocket(g_listen); g_listen = INVALID_SOCKET; }
  if (g_thread.joinable()) g_thread.join();
}
}  // namespace gd::dev
