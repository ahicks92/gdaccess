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

std::vector<SonarField::Ping> SonarField::update(const std::vector<Item>& items, double now) {
  std::vector<Ping> out;
  std::unordered_map<unsigned, double> keep;
  keep.reserve(items.size());
  for (const Item& it : items) {
    double T = params_.period_for(it.dist);
    if (T < 0.01) T = 0.01;
    auto found = next_at_.find(it.id);
    double due;
    if (found == next_at_.end()) {
      double ph = it.phase < 0 ? 0 : it.phase > 1 ? 1 : it.phase;
      due = now + ph * T;   // first pulse a phase-offset in; seeding, not firing, this frame
    } else {
      due = found->second;
      if (now >= due) {
        out.push_back({it.id, it.kind});
        do { due += T; } while (due <= now);   // advance whole periods: burst-proof, keeps the phase grid
      }
    }
    keep.emplace(it.id, due);
  }
  next_at_.swap(keep);
  return out;
}

void SonarField::reset() { next_at_.clear(); }

}  // namespace gd::core
