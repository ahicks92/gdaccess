#include "core/sonar_field.h"
#include <algorithm>
#include <cmath>

namespace gd::core {

double FieldParams::period_for(float dist) const {
  float lo = dist_near > 0.01f ? dist_near : 0.01f;
  float hi = dist_far > lo ? dist_far : lo + 0.01f;
  float d = std::clamp(dist, lo, hi);
  // Linear in log(distance): d(log d)/dd = 1/d, so the rate swings hardest where d is smallest (up close).
  double u = std::log((double)d / lo) / std::log((double)hi / lo);   // 0 at lo, 1 at hi
  return period_near + u * (period_far - period_near);
}

static double hash01(unsigned id) {   // a stable fraction in [0, 1) per id: neighbouring ids land far apart
  unsigned h = id * 2654435761u; h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
  return (double)(h & 0xffffffu) / (double)0x1000000u;
}

double SonarField::push_clear(double due, unsigned id, int kind, const std::unordered_map<unsigned, Slot>& keep) const {
  const double w = params_.collide_s;
  if (w <= 0) return due;
  for (int pass = 0; pass < 16; ++pass) {   // each pass moves past the nearest clash; converges fast in practice
    bool moved = false;
    auto check = [&](const std::unordered_map<unsigned, Slot>& m, bool skip_kept) {
      for (const auto& [oid, slot] : m) {
        if (oid == id || slot.kind != kind) continue;
        if (skip_kept && keep.count(oid)) continue;   // next_at_ entry superseded by this frame's keep entry
        if (std::fabs(slot.due - due) < w) { due = slot.due + w; moved = true; }
      }
    };
    check(keep, false);
    check(next_at_, true);
    if (!moved) return due;
  }
  return due;
}

std::vector<SonarField::Ping> SonarField::update(const std::vector<Item>& items, double now) {
  std::vector<Ping> out;
  std::unordered_map<unsigned, Slot> keep;
  keep.reserve(items.size() + next_at_.size());
  for (const Item& it : items) {
    double T = params_.period_for(it.dist);
    if (T < 0.01) T = 0.01;
    auto found = next_at_.find(it.id);
    double due;
    if (found == next_at_.end()) {
      double ph = params_.hash_phase ? hash01(it.id) : (it.phase < 0 ? 0 : it.phase > 1 ? 1 : it.phase);
      due = push_clear(now + ph * T, it.id, it.kind, keep);   // first pulse a fraction in; seeding, not firing, this frame
    } else {
      due = found->second.due;
      if (now >= due) {
        out.push_back({it.id, it.kind});
        do { due += T; } while (due <= now);   // advance whole periods: burst-proof, keeps the phase grid
        due = push_clear(due, it.id, it.kind, keep);   // and off any same-kind neighbour it would have landed on
      }
    }
    keep.emplace(it.id, Slot{due, now, it.kind});
  }
  // Grace: an id not listed this frame keeps its grid until it has been unseen for grace_s.
  for (const auto& [id, slot] : next_at_)
    if (!keep.count(id) && now - slot.last_seen <= params_.grace_s) keep.emplace(id, slot);
  next_at_.swap(keep);
  return out;
}

void SonarField::reset() { next_at_.clear(); }

}  // namespace gd::core
