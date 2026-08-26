#pragma once
#include <memory>
#include "core/screen.h"

namespace gd::screens {
// A merchant's window (vendor / faction vendor): tabs Buy (the stock, one row per item with its price; Enter
// buys, Space = the game's tooltip) and Sell (the bag; Enter sells at the game's price). Model: src/gameapi.h
// market calls (GetMarketInventorySack, PlayerPurchaseRequest, PlayerSaleRequest).
std::unique_ptr<gd::core::Screen> make_vendor();
// Ctrl+Enter in the Sell tab: prompt for a count and sell that many of the focused stack (nothing outside it).
void sell_partial_focused();
// The caravan (stash / transfer) window: tabs per stash sack and per transfer sack, rows = items (Enter moves
// an item to the bag); read-only beyond that in this first pass.
std::unique_ptr<gd::core::Screen> make_stash();
}  // namespace gd::screens
