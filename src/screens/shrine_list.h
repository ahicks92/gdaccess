#pragma once
// The devotion shrines this character has discovered, as rows for a screen (the Ctrl+M map's "shrines" Tab stop).
// Data: src/shrine_table.h (generated offline from world001.map: one shrine per chunk, position = the chunk's
// origin, so distances are to chunk precision) joined with Player::GetDiscoveredShrineUIDs / GetShrineUIDs (the
// restored ones) through gameapi::shrine_uids -- the UID is taken to be the chunk GUID (confirm with /shrines).
// Row: "desecrated shrine, Burrwitch" / "not restored, Burrwitch Village Rift, 1200 away, 3 o'clock"; Enter makes
// it the follow target (the ' key then pings it) and runs the caller's close.
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <string>
#include <vector>
#include "gameapi.h"
#include "hooks.h"
#include "screens/window_base.h"
#include "shrine_table.h"
#include "world.h"

namespace gd::screens {

inline void add_shrine_rows(gd::core::GraphBuilder& b, Snapshot<gameapi::ShrineUids>& cache, const world::Vec3& me, bool have_me,
                            std::function<void(const shrine_table::Shrine&, const std::string& label)> on_pick) {
  using gd::core::MessageBuilder; using gd::core::ControlId;
  const gameapi::ShrineUids& uids = cache.get([] { return gameapi::shrine_uids(); }, 60);
  struct SRow { const shrine_table::Shrine* s; float distance; bool restored; };
  std::vector<SRow> found;
  for (const shrine_table::Shrine& s : shrine_table::kShrines) {
    gameapi::UniqueId id; std::memcpy(id.b, s.guid, 16);
    bool discovered = std::find(uids.discovered.begin(), uids.discovered.end(), id) != uids.discovered.end();
    bool restored = std::find(uids.restored.begin(), uids.restored.end(), id) != uids.restored.end();
    if (!discovered && !restored) continue;
    world::Vec3 p{(float)s.x, (float)s.y, (float)s.z};
    float d = have_me ? std::sqrt((p.x - me.x) * (p.x - me.x) + (p.z - me.z) * (p.z - me.z)) : 0.f;
    found.push_back({&s, d, restored});
  }
  if (found.empty()) { b.add_item(ControlId::structural("shrine.none"), line_item(std::string(strings::kNoShrines))); return; }
  std::stable_sort(found.begin(), found.end(), [](const SRow& a, const SRow& c) { return a.distance < c.distance; });
  for (const SRow& r : found) {
    const shrine_table::Shrine* s = r.s;
    MessageBuilder label;
    label.fragment(std::string(s->corrupted ? strings::kDesecratedShrine : strings::kRuinedShrine)).list_item().fragment(s->area);
    std::string zone = hooks::localize(s->zone_tag);
    world::Vec3 p{(float)s->x, (float)s->y, (float)s->z};
    float d = r.distance; int hour = world::clock_hour(p); bool restored = r.restored;
    auto value = [zone, d, hour, restored, have_me] {
      MessageBuilder m;
      m.fragment(std::string(restored ? strings::kRestored : strings::kNotRestored));
      if (!zone.empty()) m.list_item().fragment(zone);
      if (have_me) { m.list_item(); strings::push_distance_bearing(m, d, hour); }
      return m.build();
    };
    std::string text = label.build();
    b.add_item(ControlId::structural(std::string("shrine.") + s->icon), row_item(text, value, [s, text, on_pick] { if (on_pick) on_pick(*s, text); }));
  }
}

}  // namespace gd::screens
