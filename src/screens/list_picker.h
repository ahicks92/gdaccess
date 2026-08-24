#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "core/screen.h"

namespace gd::screens {

// One row in the shared picker: `id` is passed to on_pick (0 is reserved by callers for a "clear / empty /
// unequip" entry), `label` is the name, `value` an optional second fragment (stack, aim, ...).
struct PickerItem { unsigned id; std::string label; std::string value; };

// Open the shared list picker -- a layered overlay above whatever screen is showing. The title is spoken on
// entry; each row is navigable; activating a row runs on_pick(id) and closes the picker, which re-exposes the
// screen that opened it with its focus intact (so the launching slot stays selected). Escape cancels.
// Only one picker is open at a time (equip OR hotbar assign, never both).
// `tooltip`, if given, is invoked on Space (detail=false) / Ctrl+Space (detail=true) with the focused row's id
// -- the equip picker speaks the item's tooltip, the skill picker the skill's.
void open_picker(std::string title, std::vector<PickerItem> items, std::function<void(unsigned)> on_pick,
                 std::function<void(unsigned, bool)> tooltip = {});
bool picker_open();

std::unique_ptr<gd::core::Screen> make_list_picker();

}  // namespace gd::screens
