#include "devserver.h"
#include "hooks.h"
#include "app.h"
#include "log.h"
#include "speech.h"
#include "textcap.h"
#include "world.h"
#include "audio.h"
#include "audio_mute.h"
#include "screens/in_game.h"
#include <cmath>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <atomic>
#include <map>
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
  };
  auto it = k.find(name);
  return it == k.end() ? -1 : it->second;
}

static std::string handle(const std::string& path, const std::map<std::string, std::string>& q, const std::string& body, int& status) {
  status = 200;
  if (path == "/health")
    return std::format("ok pid={} frame={} backend={} speech_muted={} game_keys_muted={} keyboard={}\n", GetCurrentProcessId(), hooks::frame(),
                       speech::backend_name(), speech::muted(), hooks::game_keys_muted(), app::owns_keyboard() ? "mod" : "game") + hooks::counters() + "\n";
  if (path == "/speech" || path == "/log") {
    auto& ring = path == "/speech" ? speech::history() : log::ring();
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
  if (path == "/conv") return world::conversation_dump();
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
  int status = 500;
  std::string out;
  // Routes poke at game objects with guessed layouts; a fault inside one must not take the game down.
  // The SEH frame lives in a function without C++ objects (run_job_seh); the report names the module.
  JobCtx ctx{&path, &query, &body, &status, &out};
  bool ran = hooks::run_on_game_thread([&] {
    void* addr = nullptr;
    DWORD code = run_job_seh(&ctx, &addr);
    if (code) {
      status = 500;
      out = std::format("route faulted: exception {:#x} at {}\n", code, module_offset(addr));
      log::write(out);
    }
  }, 8000);
  if (!ran) { status = 504; out = "game thread did not run the job (not ticking? unfocused with inactiveUpdateRate=0?)\n"; }
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
