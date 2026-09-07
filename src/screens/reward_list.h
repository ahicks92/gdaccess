#pragma once
// "Here is what you got": the shape shared by the game's quest reward window (screens/modals.cpp) and the mod's
// own notice for results the game shows only as icons behind a sound -- the Inventor's dismantle, whose scrap
// count and bonus component are rolled when the button is pressed, so a spoken line lost under the effect
// leaves the player not knowing what came out. Header lines, one read-only row per thing handed out, then
// whatever closes it (the game's Close button for the quest window, a mod row for the notice).
#include <memory>
#include <string>
#include <vector>
#include "core/screen.h"

namespace gd::core { class GraphBuilder; }

namespace gd::screens {
// Adds the header lines (empties skipped) and one row per reward under `prefix` (ids prefix.lineN / prefix.rowN).
void add_reward_lines(gd::core::GraphBuilder& b, const std::string& prefix, const std::vector<std::string>& header,
                      const std::vector<std::string>& rewards);

// Open the mod-owned reward notice: a layered overlay above whatever screen is showing (like the list picker),
// titled `title` (spoken on entry), listing `rewards`, with a Close row. Enter on Close / Escape closes it and the
// launching screen is current again with its focus intact.
void open_reward_notice(std::string title, std::vector<std::string> rewards);
std::unique_ptr<gd::core::Screen> make_reward_notice();
}  // namespace gd::screens
