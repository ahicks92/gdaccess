#pragma once
#include <string>
#include <string_view>
#include "message_builder.h"

// The single home for mod-authored spoken strings and, more importantly, for the spoken CONVENTIONS:
// role words, how a position in a list reads, what "nothing here" sounds like. Game-provided text
// (item names, tooltips, menu labels) passes through verbatim; this is only the language we add
// around it. English only, by decision (2026-08-21). No inline literals in speech calls elsewhere.
namespace gd::strings {

// ---- roles (spoken after a control's label) ----
inline constexpr std::string_view kButton = "button";
inline constexpr std::string_view kToggle = "toggle";
inline constexpr std::string_view kSlider = "slider";
inline constexpr std::string_view kRadio = "radio button";
inline constexpr std::string_view kComboBox = "combo box";
inline constexpr std::string_view kTab = "tab";
inline constexpr std::string_view kTextField = "edit";
inline constexpr std::string_view kList = "list";
inline constexpr std::string_view kHeading = "heading";

// ---- states ----
inline constexpr std::string_view kSelected = "selected";
inline constexpr std::string_view kDisabled = "disabled";
inline constexpr std::string_view kOn = "on";
inline constexpr std::string_view kOff = "off";
inline constexpr std::string_view kNotSelected = "not selected";
inline constexpr std::string_view kEmpty = "empty";
inline constexpr std::string_view kEditing = "editing";  // a text field took the keyboard: type, Enter or Escape when done
inline constexpr std::string_view kNameNotTaken = "the game did not take the name";  // checkpoint after editing failed
inline constexpr std::string_view kSpace = "space";      // the typed-character echo for a space
inline constexpr std::string_view kExpanded = "expanded";
inline constexpr std::string_view kCollapsed = "collapsed";

// ---- feedback ----
inline constexpr std::string_view kNoTooltip = "no tooltip";
inline constexpr std::string_view kNothingToDrag = "nothing to drag";
inline constexpr std::string_view kNoDetails = "no details";
inline constexpr std::string_view kNoMatch = "no match for";
inline constexpr std::string_view kSearchCleared = "search cleared";
inline constexpr std::string_view kNothingThere = "nothing there";
inline constexpr std::string_view kNoTextOnScreen = "no text on screen";
inline constexpr std::string_view kUnsupportedScreen = "unsupported screen";
inline constexpr std::string_view kModLoaded = "G D Access loaded";
inline constexpr std::string_view kModLoadedNoSpeech = "G D Access loaded, no speech backend";

// ---- composed shapes: push_* helpers so call sites never concatenate ----
// "3 of 7"
gd::core::MessageBuilder& push_position(gd::core::MessageBuilder& m, int index1, int count);
// "<label>, <role>[, selected][, disabled]" as list items
gd::core::MessageBuilder& push_control(gd::core::MessageBuilder& m, std::string_view label, std::string_view role, bool selected, bool disabled);
// "<n> items" / "1 item"
gd::core::MessageBuilder& push_count(gd::core::MessageBuilder& m, int count, std::string_view singular, std::string_view plural);
// "x 59, z 97, life 250 of 250, region <name>" -- the in-game "where am I" readout.
gd::core::MessageBuilder& push_where(gd::core::MessageBuilder& m, float x, float z, std::string_view region, double life, float life_max);

// ---- in-game ----
// "Hangman Jarvis, 5 away, 2 o'clock, 1 of 3" -- a review-cursor landing (game label verbatim).
gd::core::MessageBuilder& push_scan_item(gd::core::MessageBuilder& m, std::string_view label, float distance, int clock_hour, int index1, int count);
// "no enemies nearby"
gd::core::MessageBuilder& push_nothing_nearby(gd::core::MessageBuilder& m, std::string_view group_plural);
inline constexpr std::string_view kEnemies = "enemies";
inline constexpr std::string_view kNeutrals = "people";
inline constexpr std::string_view kBystanders = "bystanders";
inline constexpr std::string_view kObjects = "objects";
inline constexpr std::string_view kNoTarget = "no target";
inline constexpr std::string_view kInGame = "in game";
inline constexpr std::string_view kMessage = "message";  // the game's generic message box
inline constexpr std::string_view kPauseMenu = "pause menu";
inline constexpr std::string_view kLoading = "loading";
inline constexpr std::string_view kTip = "tip";      // a tutorial tip, shown in our own overlay
inline constexpr std::string_view kConversation = "conversation";
inline constexpr std::string_view kClose = "close";
inline constexpr std::string_view kNotInWorld = "not in the world";

}  // namespace gd::strings
