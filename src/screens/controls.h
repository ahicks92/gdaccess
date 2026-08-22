#pragma once
// Shared building blocks for the game's screens: the control types (role word + speak order) and the
// widget-backed button item (label, enabled state and activation all from the exe's own button object).
#include <memory>
#include <string>
#include "core/graph_types.h"
#include "core/strings.h"
#include "exe_ui.h"
#include "hooks.h"
#include "log.h"
#include "speech.h"
#include "textcap.h"

namespace gd::screens {

inline const gd::core::ControlType kButtonType{"button", {"label", "role", "value", "selected", "enabled", "tooltip", "position"},
    [] { return std::vector<gd::core::NodeAnnouncement>{gd::core::NodeAnnouncement([] { return std::string(gd::strings::kButton); }, false, gd::core::announcement_kinds::kRole)}; }};
inline const gd::core::ControlType kRadioType{"radio", {"label", "role", "selected", "enabled", "position"},
    [] { return std::vector<gd::core::NodeAnnouncement>{gd::core::NodeAnnouncement([] { return std::string(gd::strings::kRadio); }, false, gd::core::announcement_kinds::kRole)}; }};
inline const gd::core::ControlType kToggleType{"toggle", {"label", "role", "value"},
    [] { return std::vector<gd::core::NodeAnnouncement>{gd::core::NodeAnnouncement([] { return std::string(gd::strings::kToggle); }, false, gd::core::announcement_kinds::kRole)}; }};
inline const gd::core::ControlType kSliderType{"slider", {"label", "role", "value"},
    [] { return std::vector<gd::core::NodeAnnouncement>{gd::core::NodeAnnouncement([] { return std::string(gd::strings::kSlider); }, false, gd::core::announcement_kinds::kRole)}; }};
inline const gd::core::ControlType kComboType{"combo", {"label", "role", "value"},
    [] { return std::vector<gd::core::NodeAnnouncement>{gd::core::NodeAnnouncement([] { return std::string(gd::strings::kComboBox); }, false, gd::core::announcement_kinds::kRole)}; }};
inline const gd::core::ControlType kTabType{"tab", {"label", "role", "selected", "position"},
    [] { return std::vector<gd::core::NodeAnnouncement>{gd::core::NodeAnnouncement([] { return std::string(gd::strings::kTab); }, false, gd::core::announcement_kinds::kRole)}; }};
inline const gd::core::ControlType kEditType{"edit", {"label", "role", "value"},
    [] { return std::vector<gd::core::NodeAnnouncement>{gd::core::NodeAnnouncement([] { return std::string(gd::strings::kTextField); }, false, gd::core::announcement_kinds::kRole)}; }};

// A button of the exe's menu tree as a graph item: the label is the game's own caption (or `label` when the
// button has none, e.g. the main menu's icon buttons), "disabled" from the widget's enabled byte, activation
// through the button's listeners. The handle is per render, never stored (the graph is immediate-mode).
inline gd::core::NodeVtablePtr widget_button(gd::exe_ui::WidgetA w, std::string label = {}) {
  auto v = std::make_shared<gd::core::NodeVtable>();
  v->control_type = &kButtonType;
  if (label.empty()) label = w.caption();
  bool enabled = w.enabled();
  v->announcements = {gd::core::NodeAnnouncement([label] { return label; }, false, gd::core::announcement_kinds::kLabel),
                      gd::core::NodeAnnouncement([enabled] { return enabled ? std::string() : std::string(gd::strings::kDisabled); }, true, gd::core::announcement_kinds::kEnabled)};
  v->on_activate = [w, enabled] {
    if (!enabled) { speech::speak(gd::strings::kDisabled, true); return; }
    w.activate();
  };
  return v;
}

}  // namespace gd::screens
