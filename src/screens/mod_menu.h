#pragma once
// The mod's own menu (F1 anywhere, 2026-09-01): the sound glossary, and in the world the announcement settings.
#include <memory>
namespace gd::core { class Screen; }
namespace gd::screens {
void open_mod_menu();
std::unique_ptr<gd::core::Screen> make_mod_menu();
}
