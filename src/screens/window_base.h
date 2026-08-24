#pragma once
// Shared shape of the in-world window screens (codex, factions, inventory, skills): the screen is showing
// while InGameUI's window object says IsVisible(); Escape closes it through the window's own Show(false);
// a tab row across the top (Left/Right, Enter; Ctrl+Tab / Ctrl+Shift+Tab from anywhere) and one vertical
// column below it. Content comes from src/gameapi.h snapshots refreshed every few frames and after every
// action (the graph is immediate-mode; the snapshot keeps the per-frame rebuild cheap).
#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "core/graph_builder.h"
#include "core/message_builder.h"
#include "core/screen.h"
#include "core/strings.h"
#include "exe_ui.h"
#include "gameapi.h"
#include "hooks.h"
#include "screens/controls.h"
#include "speech.h"
#include "world.h"

namespace gd::screens {

inline const gd::core::ControlType kLineType{"text", {"label", "value"}, [] { return std::vector<gd::core::NodeAnnouncement>{}; }};
inline const gd::core::ControlType kItemType{"item", {"label", "value", "enabled", "position"}, [] { return std::vector<gd::core::NodeAnnouncement>{}; }};
inline const gd::core::ControlType kGroupType{"group", {"label", "value", "position"}, [] { return std::vector<gd::core::NodeAnnouncement>{}; }};

// A read-only line.
inline gd::core::NodeVtablePtr line_item(std::string text) {
  auto v = std::make_shared<gd::core::NodeVtable>();
  v->control_type = &kLineType;
  v->announcements = {gd::core::NodeAnnouncement([text] { return text; }, false, gd::core::announcement_kinds::kLabel)};
  return v;
}
// An actionable row: label, optional live value, Enter / Backspace / Space handlers.
inline gd::core::NodeVtablePtr row_item(std::string label, std::function<std::string()> value = {}, std::function<void()> activate = {},
                                        std::function<void()> tooltip = {}, std::function<void()> secondary = {}, std::function<void()> tooltip_detail = {}) {
  auto v = std::make_shared<gd::core::NodeVtable>();
  v->control_type = &kItemType;
  v->announcements = {gd::core::NodeAnnouncement([label] { return label; }, false, gd::core::announcement_kinds::kLabel)};
  if (value) v->announcements.push_back(gd::core::NodeAnnouncement(value, true, gd::core::announcement_kinds::kValue));
  v->on_activate = std::move(activate);
  v->on_tooltip = std::move(tooltip);
  v->on_tooltip_detail = std::move(tooltip_detail);
  v->on_secondary = std::move(secondary);
  return v;
}
// Speak a multi-line game text (a tooltip) as one interrupting utterance; "no tooltip" when empty.
inline void speak_lines(const std::vector<std::string>& lines) {
  if (lines.empty()) { speech::speak(gd::strings::kNoTooltip, true); return; }
  gd::core::MessageBuilder m;
  for (const std::string& l : lines) m.list_item().fragment(l);
  speech::speak(m.build(), true);
}
// Space / Ctrl+Space on an item: the game's tooltip, short and with its Ctrl-held details. The short form ends
// with our hint only when the detailed form really says more (starter gear: identical, no hint).
inline std::function<void()> item_tip(unsigned id, bool details) {
  return [id, details] {
    void* p = gd::gameapi::object_by_id(id);
    if (!p) { speech::speak(gd::strings::kNoTooltip, true); return; }
    // A lore note's tooltip is the note itself: the FULL text (the game's tooltip truncates it) and how to
    // file it (decided with the user 2026-08-23).
    if (gd::world::object_is_note(p)) {
      std::vector<std::string> text = gd::gameapi::note_full_text(p);
      if (!text.empty()) { text.push_back(std::string(gd::strings::kNoteUseHint)); speak_lines(text); return; }
    }
    std::vector<std::string> lines = gd::gameapi::item_tooltip(p, false, details);
    if (!details && !lines.empty() && gd::gameapi::item_tooltip(p, false, true) != lines) lines.push_back(std::string(gd::strings::kDetailsHint));
    speak_lines(lines);
  };
}

// A snapshot refreshed every `ttl` frames or on demand.
template <class T> struct Snapshot {
  T value{};
  uint64_t frame = ~0ull;
  bool stale = true;
  const T& get(std::function<T()> load, uint64_t ttl = 20) {
    uint64_t f = gd::hooks::frame();
    if (stale || frame == ~0ull || f - frame > ttl) { value = load(); frame = f; stale = false; }
    return value;
  }
  void invalidate() { stale = true; }
};

class WindowScreen : public gd::core::Screen {
 public:
  WindowScreen(std::string key, std::string name, unsigned window_off, int layer) : key_(std::move(key)), name_(std::move(name)), off_(window_off), layer_(layer) {}
  std::string_view key() const override { return key_; }
  std::string screen_name() const override { return name_; }
  int layer() const override { return layer_; }
  bool is_active() override { return gd::exe_ui::available() && window().visible(); }
  std::vector<gd::core::InputCategory> input_categories() const override { return {gd::core::InputCategory::UI, gd::core::InputCategory::Windows}; }
  bool allows_typeahead() const override { return true; }
  std::vector<gd::core::ScreenAction> actions() override {
    return {{std::string(gd::core::action_ids::Back), [this] { close(); }}};
  }
  gd::exe_ui::WindowB window() const { return gd::exe_ui::ingame_window(off_); }
  virtual void close() { gd::exe_ui::WindowB w = window(); if (w) w.show(false); }

  // ---- tabs ----
  // The screen declares its tab labels per render; the selected index is ours (or the game's when the
  // subclass maps it). Ctrl+Tab cycles; Enter on a tab selects it. A change speaks "label, tab".
  int tab() const { return tab_; }
  bool switch_tab(int dir) override {
    int n = (int)tab_labels_.size();
    if (n == 0) return false;
    select_tab(((tab_ + dir) % n + n) % n, true);
    return true;
  }
  virtual void on_tab_changed(int /*index*/) {}
  void select_tab(int index, bool speak) {
    if (index < 0 || index >= (int)tab_labels_.size()) return;
    tab_ = index;
    tab_key_ = tab_labels_[(size_t)index];   // remember WHICH tab, not its position
    on_tab_changed(index);
    if (speak) { gd::core::MessageBuilder m; gd::strings::push_control(m, tab_labels_[(size_t)index], gd::strings::kTab, false, false); speech::speak(m.build(), true); }
  }
  void add_tabs(gd::core::GraphBuilder& b, std::vector<std::string> labels) {
    tab_labels_ = std::move(labels);
    // Re-resolve the selection by its label: when the tab SET changes membership (a merchant's empty
    // category, or buying the last item in a category so its tab vanishes -- 2026-08-23) a bare index would
    // silently point at a different tab. Fall back to clamping when the remembered tab is gone.
    if (!tab_key_.empty()) {
      auto it = std::find(tab_labels_.begin(), tab_labels_.end(), tab_key_);
      if (it != tab_labels_.end()) tab_ = (int)(it - tab_labels_.begin());
    }
    if (tab_ < 0 || tab_ >= (int)tab_labels_.size()) tab_ = 0;
    if (tab_key_.empty() && !tab_labels_.empty()) tab_key_ = tab_labels_[(size_t)tab_];
    b.begin_stop("tabs");
    b.start_row("tabs");
    for (size_t i = 0; i < tab_labels_.size(); ++i) {
      std::string label = tab_labels_[i];
      bool selected = (int)i == tab_;
      auto v = std::make_shared<gd::core::NodeVtable>();
      v->control_type = &kTabType;
      v->announcements = {gd::core::NodeAnnouncement([label] { return label; }, false, gd::core::announcement_kinds::kLabel),
                          gd::core::NodeAnnouncement([selected] { return selected ? std::string(gd::strings::kSelected) : std::string(); }, true, gd::core::announcement_kinds::kSelected)};
      v->on_activate = [this, i] { select_tab((int)i, false); };
      b.add_item(gd::core::ControlId::structural(key_ + ".tab" + std::to_string(i)), v);
    }
    b.end_row();
    b.begin_stop("page");
  }

 protected:
  std::string key_, name_;
  unsigned off_;
  int layer_;
  int tab_ = 0;
  std::string tab_key_;      // the selected tab's label; the index is re-resolved from it each render
  std::vector<std::string> tab_labels_;
};

}  // namespace gd::screens
