#include "textcap.h"
#include "log.h"
#include "speech.h"
#include "core/message_builder.h"
#include <algorithm>
#include <mutex>
#include <set>

namespace gd::textcap {
static std::mutex g_mu;
static std::vector<Item> g_cur, g_last;
static std::set<std::u16string> g_last_texts;
static uint64_t g_frame;
static bool g_announce = false;

std::string speakable(std::u16string_view raw) {
  std::u16string out;
  out.reserve(raw.size());
  for (size_t i = 0; i < raw.size(); ++i) {
    if (raw[i] == u'^' && i + 1 < raw.size()) {  // ^h, ^-, ^w ... = color/style codes; ^n = newline
      if (raw[i + 1] == u'n') out.push_back(u' ');
      ++i; continue;
    }
    if (raw[i] == u'{') {  // short {...} markup tokens ("{}Training Dummy"); a real brace has no close within 8 chars
      size_t close = raw.find(u'}', i);
      if (close != std::u16string_view::npos && close - i <= 8) { i = close; continue; }
    }
    out.push_back(raw[i]);
  }
  return speakable(std::string_view(log::utf8(out)));
}
std::string speakable(std::string_view utf8) {
  std::string s;
  s.reserve(utf8.size());
  for (size_t i = 0; i < utf8.size(); ++i) {
    if (utf8[i] == '^' && i + 1 < utf8.size()) { if (utf8[i + 1] == 'n') s.push_back(' '); ++i; continue; }
    if (utf8[i] == '{') {
      size_t close = utf8.find('}', i);
      if (close != std::string_view::npos && close - i <= 8) { i = close; continue; }
    }
    s.push_back(utf8[i]);
  }
  // collapse whitespace runs and trim
  std::string t;
  for (char c : s) {
    bool ws = c == ' ' || c == '\t' || c == '\n' || c == '\r';
    if (ws && (t.empty() || t.back() == ' ')) continue;
    t.push_back(ws ? ' ' : c);
  }
  while (!t.empty() && t.back() == ' ') t.pop_back();
  return t;
}

void on_text(Item&& item) {
  if (item.text.empty()) return;
  std::lock_guard lk(g_mu);
  // The Rect overloads call the (x,y) overloads internally, and some screens draw a line twice
  // (shadow pass): drop exact duplicates of the previous capture.
  if (!g_cur.empty()) {
    const Item& p = g_cur.back();
    if (p.x == item.x && p.y == item.y && p.text == item.text) return;
  }
  if (g_cur.size() < 4096) g_cur.push_back(std::move(item));
}

static std::string describe(const Item& it) {
  return std::format("\"{}\" @({},{}) align={}/{} rgba={:08x} [{}]", log::utf8(it.text), it.x, it.y, it.xalign, it.yalign, it.rgba, it.variant);
}

void on_frame_end() {
  std::vector<Item> cur;
  {
    std::lock_guard lk(g_mu);
    ++g_frame;
    cur.swap(g_cur);
    std::stable_sort(cur.begin(), cur.end(), [](const Item& a, const Item& b) { return a.y != b.y ? a.y < b.y : a.x < b.x; });
    g_last = cur;
  }
  std::set<std::u16string> texts;
  for (auto& it : cur) texts.insert(it.text);
  std::set<std::u16string> added;
  std::vector<std::string> removed;
  for (auto& t : texts) if (!g_last_texts.count(t)) added.insert(t);
  for (auto& t : g_last_texts) if (!texts.count(t)) removed.push_back(log::utf8(t));
  if (!added.empty() || !removed.empty()) {
    log::writef("frame {}: {} texts, +{} -{}", g_frame, cur.size(), added.size(), removed.size());
    int n = 0;
    std::set<std::u16string> logged;
    for (auto& it : cur) if (added.count(it.text) && logged.insert(it.text).second && n++ < 80) log::writef("  + {}", describe(it));
    n = 0;
    for (auto& r : removed) if (n++ < 40) log::writef("  - \"{}\"", r);
    if (g_announce && !added.empty()) {
      gd::core::MessageBuilder m;
      std::set<std::u16string> spoken;
      for (auto& it : cur) if (added.count(it.text) && spoken.insert(it.text).second) m.list_item().fragment(speakable(it.text));
      speech::speak(m.build(), false);
    }
  }
  g_last_texts.swap(texts);
}

std::vector<Item> snapshot() { std::lock_guard lk(g_mu); return g_last; }
bool find_text(std::string_view t, int& x, int& y) {
  std::lock_guard lk(g_mu);
  for (auto& it : g_last) if (speakable(it.text) == t) { x = it.x; y = it.y; return true; }
  return false;
}
bool has_text(std::string_view t) { int x, y; return find_text(t, x, y); }
bool find_item(std::string_view t, Item& out, bool last) {
  std::lock_guard lk(g_mu);
  bool found = false;
  for (auto& it : g_last) {
    if (speakable(it.text) != t) continue;
    out = it; found = true;
    if (!last) break;
  }
  return found;
}
void set_announce_changes(bool on) { g_announce = on; }
bool announce_changes() { return g_announce; }
uint64_t frame() { return g_frame; }
}  // namespace gd::textcap
