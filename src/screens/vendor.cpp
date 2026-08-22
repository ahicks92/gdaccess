#include "screens/vendor.h"
#include <format>
#include "gameapi.h"
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
    b.add_item(ControlId::structural(std::format("{}.{}", prefix, id)),
               row_item(m.build(), extra.empty() ? std::function<std::string()>{} : [extra] { return extra; }, [activate, id] { activate(id); }, item_tip(id, false), {}, item_tip(id, true)));
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

  void build(GraphBuilder& b) override {
    unsigned market = exe_ui::vendor_market_id(active_window());
    const std::vector<gameapi::MarketTab>& stock = stock_.get([market] { return gameapi::market_stock(market); }, 60);
    std::vector<std::string> labels;
    for (const gameapi::MarketTab& t : stock) labels.push_back(t.name.empty() ? std::format("{} {}", strings::kBuy, t.type + 1) : t.name);
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
std::unique_ptr<Screen> make_stash() { return std::make_unique<StashScreen>(); }
}  // namespace gd::screens
