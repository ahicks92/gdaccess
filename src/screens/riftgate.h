#pragma once
#include <memory>
#include "core/screen.h"

namespace gd::screens {
// The riftgate travel map (after using a rift, or the Personal Riftgate key): the discovered riftgates as a
// list; Enter travels, Escape closes. docs/exe-ui-layout.md "Riftgate travel".
std::unique_ptr<gd::core::Screen> make_riftgate();
}  // namespace gd::screens
