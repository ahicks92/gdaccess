#include "strings.h"
#include <format>

namespace gd::strings {
using gd::core::MessageBuilder;

MessageBuilder& push_position(MessageBuilder& m, int index1, int count) {
  m.list_item().fragment(std::format("{} of {}", index1, count));
  return m;
}

MessageBuilder& push_control(MessageBuilder& m, std::string_view label, std::string_view role, bool selected, bool disabled) {
  m.list_item().fragment(label);
  if (!role.empty()) m.list_item().fragment(role);
  if (selected) m.list_item().fragment(kSelected);
  if (disabled) m.list_item().fragment(kDisabled);
  return m;
}

MessageBuilder& push_where(MessageBuilder& m, float x, float z, std::string_view region, double life, float life_max) {
  m.fragment(std::format("x {:.0f}", x));
  m.list_item().fragment(std::format("z {:.0f}", z));
  m.list_item().fragment(std::format("life {:.0f} of {:.0f}", life, life_max));
  if (!region.empty()) m.list_item().fragment(std::format("region {}", region));
  return m;
}
MessageBuilder& push_scan_item(MessageBuilder& m, std::string_view label, float distance, int clock_hour, int index1, int count) {
  m.fragment(label);
  m.list_item().fragment(std::format("{:.0f} away", distance));
  m.list_item().fragment(std::format("{} o'clock", clock_hour));
  m.list_item();
  push_position(m, index1, count);
  return m;
}
MessageBuilder& push_nothing_nearby(MessageBuilder& m, std::string_view group_plural) {
  m.fragment("no").fragment(group_plural).fragment("nearby");
  return m;
}
MessageBuilder& push_count(MessageBuilder& m, int count, std::string_view singular, std::string_view plural) {
  m.list_item().fragment(std::format("{} {}", count, count == 1 ? singular : plural));
  return m;
}

}  // namespace gd::strings
