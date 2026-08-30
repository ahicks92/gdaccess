#pragma once
// The blacksmith's crafting window ("Forgemaster"): a shop whose price is the recipe. docs/crafting.md.
#include <memory>
#include "core/screen.h"

namespace gd::screens {
std::unique_ptr<gd::core::Screen> make_crafting();
}
