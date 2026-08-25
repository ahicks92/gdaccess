#include "notify.h"
#include <windows.h>
#include <atomic>
#include <deque>
#include <format>
#include <mutex>
#include <string>
#include <vector>
#include "app.h"
#include "gd_names.h"
#include "hooks.h"
#include "log.h"
#include "msvc_string.h"
#include "speech.h"

namespace gd::notify {
namespace {
using namespace gd::names;

std::vector<gd::hooks::Hook> g_hooks;
std::atomic<uint64_t> g_banners{0}, g_popups{0}, g_deduped{0};
std::mutex g_recent_mu;
std::deque<std::string> g_recent;      // last spoken lines, for /notify
std::string g_last_text;               // dedupe: the immediately-preceding line
double g_last_t = 0;
constexpr double kDedupeWindow = 1.5;  // seconds; the game re-sets the popup while it is showing

// Speak a banner/popup line once (game thread): non-interrupting, deduped against the last identical line.
void announce(std::string text) {
  if (text.empty()) return;
  double now = app::now();
  if (text == g_last_text && now - g_last_t < kDedupeWindow) { ++g_deduped; g_last_t = now; return; }
  g_last_text = text; g_last_t = now;
  { std::lock_guard lk(g_recent_mu); g_recent.push_back(text); while (g_recent.size() > 20) g_recent.pop_front(); }
  speech::speak(text, false);   // screen reader, do not cut what it is already saying
}

bool bad(const void* p, size_t n) { return IsBadReadPtr(p, n) != 0; }
// POD reads (SEH): copy a MsvcStringW's UTF-16 into a fixed buffer (no C++ objects in the __try body).
bool read_u16(const void* mstr, char16_t* out, size_t cap) {
  __try {
    const MsvcStringW* s = (const MsvcStringW*)mstr;
    size_t n = s->size < cap - 1 ? s->size : cap - 1;
    const char16_t* d = s->data();
    if (n && bad(d, n * 2)) return false;
    memcpy(out, d, n * 2); out[n] = 0;
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool read_charstr(const void* mstr, char* out, size_t cap) {
  __try {
    const MsvcStringA* s = (const MsvcStringA*)mstr;
    size_t n = s->size < cap - 1 ? s->size : cap - 1;
    const char* d = s->data();
    if (n && bad(d, n)) return false;
    memcpy(out, d, n); out[n] = 0;
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// Read up to `maxlines` u16 strings out of a mem::vector<basic_string<u16>> (stride 0x20). Returns the count.
int read_u16_vec(const void* vec, char16_t lines[][256], int maxlines) {
  int count = 0;
  __try {
    const char* const* v = (const char* const*)vec;   // {begin, end, cap}
    const char* begin = v[0]; const char* end = v[1];
    if (!begin || end < begin) return 0;
    size_t n = (size_t)(end - begin) / 0x20;
    for (size_t i = 0; i < n && count < maxlines; ++i) {
      const MsvcStringW* s = (const MsvcStringW*)(begin + i * 0x20);
      size_t m = s->size < 255 ? s->size : 255;
      const char16_t* d = s->data();
      if (m && bad(d, m * 2)) { lines[count][0] = 0; }
      else { memcpy(lines[count], d, m * 2); lines[count][m] = 0; }
      ++count;
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {}
  return count;
}

// GameEngine::AddUINotification(Type, u16string const&, bool) -- one localized banner line.
typedef void (*AddUINotifStr_t)(void*, int, const void*, bool);
static AddUINotifStr_t AddUINotifStr_hook_orig;
static void AddUINotifStr_hook(void* self, int type, const void* str, bool b) {
  AddUINotifStr_hook_orig(self, type, str, b);
  ++g_banners;
  char16_t buf[256];
  if (str && read_u16(str, buf, 256) && buf[0]) announce(log::utf8(buf));
}
// GameEngine::AddUINotification(Type, mem::vector<u16string> const&, bool) -- a multi-line banner.
typedef void (*AddUINotifVec_t)(void*, int, const void*, bool);
static AddUINotifVec_t AddUINotifVec_hook_orig;
static void AddUINotifVec_hook(void* self, int type, const void* vec, bool b) {
  AddUINotifVec_hook_orig(self, type, vec, b);
  ++g_banners;
  if (!vec) return;
  char16_t lines[8][256];
  int n = read_u16_vec(vec, lines, 8);
  for (int i = 0; i < n; ++i) if (lines[i][0]) announce(log::utf8(lines[i]));
}
// ControllerPlayer::SetUserText(std::string const& tag, int ms) -- the red action-failed popup; localize the tag.
typedef void (*SetUserText_t)(void*, const void*, int);
static SetUserText_t SetUserText_hook_orig;
static void SetUserText_hook(void* self, const void* tag, int ms) {
  SetUserText_hook_orig(self, tag, ms);
  ++g_popups;
  char t[128];
  if (tag && read_charstr(tag, t, 128) && t[0]) announce(gd::hooks::localize(t));
}
}  // namespace

bool install() {
  g_hooks = {GD_HOOK(GameEngine_AddUINotification_Str, AddUINotifStr_hook),
             GD_HOOK(GameEngine_AddUINotification_Vec, AddUINotifVec_hook),
             GD_HOOK(ControllerPlayer_SetUserText, SetUserText_hook)};
  return gd::hooks::attach_hooks(g_hooks) == 0;
}
void remove() { gd::hooks::detach_hooks(g_hooks); }

std::string status() {
  std::string out = std::format("banners={} popups={} deduped={}\n", g_banners.load(), g_popups.load(), g_deduped.load());
  std::lock_guard lk(g_recent_mu);
  for (const std::string& l : g_recent) out += "  " + l + "\n";
  return out;
}
}  // namespace gd::notify
