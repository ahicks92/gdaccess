#pragma once
// Mod options (F1 -> mod options, 2026-09-18): the mod's own switches that are neither sounds nor announcements.
// One stop; each row is a toggle persisted in settings.txt. Currently: the dev server (src/devserver.h).
#include <memory>
namespace gd::core { class Screen; }
namespace gd::screens {
void open_mod_options();
std::unique_ptr<gd::core::Screen> make_mod_options();
}
