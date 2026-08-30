// The game's Loot Filter window (InGameUI+0xab410; docs/loot-filter.md, docs/re_lootfilter_exe.md) as four tab
// stops -- its columns Quality / Type / Damage / Character -- each a list of toggles in the window's own order.
// Truth is Player::GetLootFilter; Enter flips the bit through Player::SetLootFilter (in effect at once, saved with
// the character, exactly what the game's click does) and mirrors the drawn box; Space reads the box's own
// tooltip tag; the last row of a column resets that column to the factory defaults. Escape = the window's Show(false).
#include "screens/loot_filter.h"
#include <format>
#include "gameapi.h"
#include "screens/window_base.h"
#include "textcap.h"

namespace gd::screens {
using namespace gd::core;

class LootFilterScreen : public WindowScreen {
 public:
  LootFilterScreen() : WindowScreen("lootFilter", std::string(strings::kLootFilter), exe_ui::ingame::kLootFilter, 13) {}
  // Each column is its own Tab stop (decided with the user 2026-08-29; no tab strip): its header as a line, the
  // toggles in the window's order, then "set to defaults" for that column.
  void build(GraphBuilder& b) override {
    for (int col = 0; col < gameapi::kLootFilterColumns; ++col) {
      b.begin_stop(std::format("lootFilter.col{}", col));
      b.add_item(ControlId::structural(std::format("lootFilter.head{}", col)), line_item(game_text(gameapi::loot_filter_column_tag(col), gameapi::loot_filter_column_fallback(col))));
      for (const gameapi::LootFilterOption& o : gameapi::loot_filter_options()) if (o.column == col) add_toggle(b, o);
      auto v = std::make_shared<NodeVtable>();
      v->control_type = &kItemType;
      v->announcements = {NodeAnnouncement([] { return std::string(strings::kSetToDefaults); }, false, announcement_kinds::kLabel)};
      v->on_activate = [col] {
        gameapi::loot_filter_defaults(col);
        for (const gameapi::LootFilterOption& o : gameapi::loot_filter_options()) if (o.column == col) exe_ui::loot_filter_mirror(o.index, o.default_on);
        speech::speak(std::string(strings::kDefaults), true);
      };
      b.add_item(ControlId::structural(std::format("lootFilter.defaults{}", col)), v);
    }
  }

 private:
  static std::string game_text(const char* tag, const char* fallback) {
    std::string t = textcap::speakable(hooks::localize(tag));
    return t.empty() ? std::string(fallback) : t;
  }
  static void add_toggle(GraphBuilder& b, const gameapi::LootFilterOption& o) {
    std::string label = game_text(o.tag, o.fallback);
    int idx = o.index;
    std::string tip_tag = std::string(o.tag) + "Info";
    auto v = std::make_shared<NodeVtable>();
    v->control_type = &kToggleType;
    v->announcements = {NodeAnnouncement([label] { return label; }, false, announcement_kinds::kLabel),
                        NodeAnnouncement([idx] { return std::string(gameapi::loot_filter(idx) ? strings::kOn : strings::kOff); }, true, announcement_kinds::kValue)};
    v->state_text = [idx] { return std::string(gameapi::loot_filter(idx) ? strings::kOn : strings::kOff); };
    v->on_activate = [idx] {
      bool on = !gameapi::loot_filter(idx);
      if (gameapi::set_loot_filter(idx, on)) exe_ui::loot_filter_mirror(idx, on);
    };
    v->on_tooltip = [tip_tag] { std::string t = textcap::speakable(hooks::localize(tip_tag.c_str())); speech::speak(t.empty() ? std::string(strings::kNoTooltip) : t, true); };
    b.add_item(ControlId::structural(std::format("lootFilter.opt{}", idx)), v);
  }
};

std::unique_ptr<Screen> make_loot_filter() { return std::make_unique<LootFilterScreen>(); }
}  // namespace gd::screens
