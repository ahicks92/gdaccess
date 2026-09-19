#pragma once
// The mod's own menu (F1 anywhere, 2026-09-01): the sound glossary, in the world the announcement and cue settings,
// and mod options (the dev server toggle).
#include <memory>
namespace gd::core { class Screen; }
namespace gd::screens {
void open_mod_menu();
std::unique_ptr<gd::core::Screen> make_mod_menu();
}
