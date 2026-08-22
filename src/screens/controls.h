#pragma once
// Shared building blocks for the game's screens: the control types (role word + speak order) and the
// "activate = synthesized click at the drawn label" helpers. The label is known to exist on the screen we
// modelled; only its pixel position is read from the game at activation time (it depends on resolution).
#include <memory>
#include <string>
#include "core/graph_types.h"
#include "core/strings.h"
#include "hooks.h"
#include "log.h"
#include "textcap.h"

namespace gd::screens {

inline const gd::core::ControlType kButtonType{"button", {"label", "role", "value", "selected", "enabled", "tooltip", "position"},
    [] { return std::vector<gd::core::NodeAnnouncement>{gd::core::NodeAnnouncement([] { return std::string(gd::strings::kButton); }, false, gd::core::announcement_kinds::kRole)}; }};
inline const gd::core::ControlType kRadioType{"radio", {"label", "role", "selected", "enabled", "position"},
    [] { return std::vector<gd::core::NodeAnnouncement>{gd::core::NodeAnnouncement([] { return std::string(gd::strings::kRadio); }, false, gd::core::announcement_kinds::kRole)}; }};
inline const gd::core::ControlType kToggleType{"toggle", {"label", "role", "value"},
    [] { return std::vector<gd::core::NodeAnnouncement>{gd::core::NodeAnnouncement([] { return std::string(gd::strings::kToggle); }, false, gd::core::announcement_kinds::kRole)}; }};
inline const gd::core::ControlType kEditType{"edit", {"label", "role", "value"},
    [] { return std::vector<gd::core::NodeAnnouncement>{gd::core::NodeAnnouncement([] { return std::string(gd::strings::kTextField); }, false, gd::core::announcement_kinds::kRole)}; }};

// Click at a drawn label, optionally offset from it (a text field's box sits below its label). `last` takes
// the lowest match on screen when the same label is drawn more than once (a dialog over the main menu).
inline void click_label(std::string_view screen, const std::string& label, int dx = 0, int dy = 0, bool last = false) {
  textcap::Item it;
  if (textcap::find_item(label, it, last)) hooks::click((float)(it.x + dx), (float)(it.y + dy));
  else log::writef("{}: label '{}' not on screen at activation", screen, label);
}

// A plain button whose activation is a click on its label.
inline gd::core::NodeVtablePtr click_button(std::string_view screen, std::string label) {
  auto v = std::make_shared<gd::core::NodeVtable>();
  v->control_type = &kButtonType;
  v->announcements = {gd::core::NodeAnnouncement([label] { return label; }, false, gd::core::announcement_kinds::kLabel)};
  v->on_activate = [screen, label] { click_label(screen, label); };
  return v;
}
}  // namespace gd::screens
