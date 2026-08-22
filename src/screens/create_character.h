#pragma once
#include <memory>
#include "core/screen.h"

namespace gd::screens {
// The "Create Character" dialog (name, sex, hardcore, Next/Cancel), modelled explicitly. Layer 20 (a modal
// over the main menu). We own the choices: sex and hardcore are tracked as our state and the game is driven
// to match by clicking; the name is typed into the game's own field.
std::unique_ptr<gd::core::Screen> make_create_character();
}  // namespace gd::screens
