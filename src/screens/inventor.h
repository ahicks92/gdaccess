#pragma once
// The Inventor's window: salvage a component / augment out of an item, or dismantle an item for parts. docs/inventor.md.
#include <memory>
#include "core/screen.h"

namespace gd::screens {
std::unique_ptr<gd::core::Screen> make_inventor();
}
