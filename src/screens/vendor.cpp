#include "screens/vendor.h"
#include <format>
#include <optional>
#include "app.h"
#include "core/navigator.h"
#include "gameapi.h"
#include "screens/count_prompt.h"
#include "screens/window_base.h"

namespace gd::screens {
using namespace gd::core;

namespace {
void add_item_rows(GraphBuilder& b, const std::string& prefix, const std::vector<gameapi::BagItem>& items, std::function<std::string(const gameapi::BagItem&)> value,
                   std::function<void(unsigned)> activate) {
  if (items.empty()) { b.add_item(ControlId::structural(prefix + ".empty"), line_item(std::string(strings::kEmpty))); return; }
  for (const gameapi::BagItem& it : items) {
    MessageBuilder m; strings::push_stack(m, it.name.empty() ? std::format("item {}", it.id) : it.name, it.stack);
    unsigned id = it.id;
    std::string extra = value ? value(it) : std::string();
    auto v = row_item(m.build(), extra.empty() ? std::function<std::string()>{} : [extra] { return extra; }, [activate, id] { activate(id); }, item_tip(id, false), {}, item_tip(id, true));
    v->on_compare = item_compare(id);   // Backslash: the equipped item in the slot this one fits
    b.add_item(ControlId::structural(std::format("{}.{}", prefix, id)), v);
  }
}
}  // namespace

class VendorScreen : public WindowScreen {
 public:
  VendorScreen() : WindowScreen("vendor", std::string(strings::kVendor), exe_ui::ingame::kMarket, 14) {}
  bool is_active() override { return exe_ui::available() && (exe_ui::ingame_window(exe_ui::ingame::kMarket).visible() || exe_ui::ingame_window(exe_ui::ingame::kFactionVendor).visible()); }
  exe_ui::WindowB active_window() const { exe_ui::WindowB w = exe_ui::ingame_window(exe_ui::ingame::kMarket); return w.visible() ? w : exe_ui::ingame_window(exe_ui::ingame::kFactionVendor); }
  void close() override { exe_ui::WindowB w = active_window(); if (w) w.show(false); }
  void on_focus() override { invalidate(); WindowScreen::on_focus(); }
  void on_tab_changed(int) override { invalidate(); }

  // Ctrl+Enter on a Sell row: "sell how many of N", then the game's split + sale of the piece.
  void sell_partial() {
    GraphNavigator* nav = app::navigator();
    std::optional<ControlId> fid = nav ? nav->focused_id() : std::nullopt;
    if (!fid || !fid->structural_key().is_string()) { speech::speak(strings::kNotAStack, true); return; }
    const std::string& k = fid->structural_key().text();
    if (k.rfind("vendor.sell.", 0) != 0) { speech::speak(strings::kNotAStack, true); return; }
    unsigned id = (unsigned)strtoul(k.c_str() + 12, nullptr, 10);
    const gameapi::BagItem* found = nullptr;
    for (const gameapi::Bag& bag : bags_.value) for (const gameapi::BagItem& it : bag.items) if (it.id == id) found = &it;
    if (!found || found->stack < 2) { speech::speak(strings::kNotAStack, true); return; }
    unsigned market = exe_ui::vendor_market_id(active_window());
    unsigned stack = found->stack;
    open_count_prompt(std::format("{} {}", strings::kSellHowMany, stack), stack, [this, market, id](unsigned n) {
      if (n >= stack_of(id)) { speech::speak(gameapi::sell(market, id) ? std::string(strings::kSold) : std::string(strings::kCannot), true); invalidate(); return; }
      unsigned clone = gameapi::split_stack(id, n);
      if (!clone) { speech::speak(strings::kCannot, true); return; }
      pending_clone_ = clone; pending_market_ = market; pending_ticks_ = 3;   // the sale runs after the character-side add
      invalidate();
    });
  }
  void on_update() override {
    if (pending_clone_ && --pending_ticks_ == 0) {
      unsigned clone = pending_clone_; pending_clone_ = 0;
      bool ok = gameapi::sell_split(pending_market_, clone);
      if (!ok) gameapi::unsplit_stack(clone);
      speech::speak(ok ? std::string(strings::kSold) : std::string(strings::kCannot), true);
      invalidate();
    }
  }
  unsigned stack_of(unsigned id) {
    for (const gameapi::Bag& bag : bags_.value) for (const gameapi::BagItem& it : bag.items) if (it.id == id) return it.stack;
    return 0;
  }

  void build(GraphBuilder& b) override {
    unsigned market = exe_ui::vendor_market_id(active_window());
    const std::vector<gameapi::MarketTab>& stock = stock_.get([market] { return gameapi::market_stock(market); }, 60);
    std::vector<std::string> labels;
    // market_stock names each tab from the game's own vendor-tab tag (Melee Weapons, Armor, ...); "Buy N" is
    // only a fallback if a tag ever fails to localize.
    for (size_t i = 0; i < stock.size(); ++i) labels.push_back(stock[i].name.empty() ? std::format("{} {}", strings::kBuy, i + 1) : stock[i].name);
    labels.push_back(std::string(strings::kSell));
    add_tabs(b, labels);
    int t = tab();
    { MessageBuilder m; strings::push_stat(m, strings::kIronBits, std::format("{}", gameapi::money())); b.add_item(ControlId::structural("vendor.money"), line_item(m.build())); }
    if (t < (int)stock.size()) {
      const gameapi::MarketTab& mt = stock[(size_t)t];
      add_item_rows(b, std::format("vendor.buy{}", mt.type), mt.items,
                    [market](const gameapi::BagItem& it) { return gameapi::market_price_text(market, it.id, true); },
                    [this, market](unsigned id) { speech::speak(gameapi::buy(market, id) ? std::string(strings::kBought) : std::string(strings::kCannot), true); invalidate(); });
    } else {
      const std::vector<gameapi::Bag>& bags = bags_.get([] { return gameapi::bags(); }, 30);
      std::vector<gameapi::BagItem> all;
      for (const gameapi::Bag& bag : bags) all.insert(all.end(), bag.items.begin(), bag.items.end());
      add_item_rows(b, "vendor.sell", all,
                    [market](const gameapi::BagItem& it) { return gameapi::market_price_text(market, it.id, false); },
                    [this, market](unsigned id) { speech::speak(gameapi::sell(market, id) ? std::string(strings::kSold) : std::string(strings::kCannot), true); invalidate(); });
    }
  }
 private:
  void invalidate() { stock_.invalidate(); bags_.invalidate(); }
  Snapshot<std::vector<gameapi::MarketTab>> stock_;
  Snapshot<std::vector<gameapi::Bag>> bags_;
  unsigned pending_clone_ = 0, pending_market_ = 0; int pending_ticks_ = 0;
};

class StashScreen : public WindowScreen {
 public:
  StashScreen() : WindowScreen("stash", std::string(strings::kStash), exe_ui::ingame::kCaravan, 14) {}
  void on_focus() override { sacks_.invalidate(); WindowScreen::on_focus(); }
  void on_tab_changed(int) override { sacks_.invalidate(); }
  void build(GraphBuilder& b) override {
    const std::vector<gameapi::Bag>& sacks = sacks_.get([] { return gameapi::stash_sacks(); }, 30);
    std::vector<std::string> labels;
    for (const gameapi::Bag& s : sacks) labels.push_back(s.name.empty() ? std::format("{} {}", s.index < 100 ? strings::kStash : strings::kTransfer, (s.index % 100) + 1) : s.name);
    if (labels.empty()) labels.push_back(std::string(strings::kStash));
    add_tabs(b, labels);
    int t = tab();
    if (t >= (int)sacks.size()) { b.add_item(ControlId::structural("stash.empty"), line_item(std::string(strings::kEmpty))); return; }
    const gameapi::Bag& s = sacks[(size_t)t];
    add_item_rows(b, std::format("stash.{}", s.index), s.items, {},
                  [this, s](unsigned id) { speech::speak(gameapi::stash_to_bag(s.index, id) ? std::string(strings::kMoved) : std::string(strings::kCannot), true); sacks_.invalidate(); });
  }
 private:
  Snapshot<std::vector<gameapi::Bag>> sacks_;
};

std::unique_ptr<Screen> make_vendor() { return std::make_unique<VendorScreen>(); }
void sell_partial_focused() {
  VendorScreen* v = dynamic_cast<VendorScreen*>(app::screens().current());
  if (v) v->sell_partial();   // a Windows-category chord: silent in the other windows
}
std::unique_ptr<Screen> make_stash() { return std::make_unique<StashScreen>(); }
}  // namespace gd::screens
