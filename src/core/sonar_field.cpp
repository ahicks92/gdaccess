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
  std::unordered_map<unsigned, Slot> keep;
  keep.reserve(items.size() + next_at_.size());
  for (const Item& it : items) {
    double T = params_.period_for(it.dist);
    if (T < 0.01) T = 0.01;
    auto found = next_at_.find(it.id);
    double due;
    if (found == next_at_.end()) {
      double ph = it.phase < 0 ? 0 : it.phase > 1 ? 1 : it.phase;
      due = now + ph * T;   // first pulse a phase-offset in; seeding, not firing, this frame
    } else {
      due = found->second.due;
      if (now >= due) {
        out.push_back({it.id, it.kind});
        do { due += T; } while (due <= now);   // advance whole periods: burst-proof, keeps the phase grid
      }
    }
    keep.emplace(it.id, Slot{due, now});
  }
  // Grace: an id not listed this frame keeps its grid until it has been unseen for grace_s.
  for (const auto& [id, slot] : next_at_)
    if (!keep.count(id) && now - slot.last_seen <= params_.grace_s) keep.emplace(id, slot);
  next_at_.swap(keep);
  return out;
}

void SonarField::reset() { next_at_.clear(); }

}  // namespace gd::core

namespace gd::core {

double effective_count(const std::vector<double>& c) {
  double s = 0, s2 = 0;
  for (double v : c) { if (v > 0) { s += v; s2 += v * v; } }
  return s2 > 0 ? (s * s) / s2 : 1.0;
}

float compress_gain(double c, double pivot, double r) {
  if (r <= 1.0 || pivot <= 0 || c <= pivot) return 1.0f;   // at or under the pivot: untouched, never boosted
  // c' = pivot * (c/pivot)^(1/r)  ->  amplitude factor sqrt(c'/c) = (c/pivot)^((1/r - 1) / 2)
  return (float)std::pow(c / pivot, (1.0 / r - 1.0) * 0.5);
}

double compressed_power(const std::vector<double>& c, double pivot, double r) {
  double s = 0;
  for (double v : c) {
    if (v <= 0) continue;
    s += (v <= pivot || r <= 1.0) ? v : pivot * std::pow(v / pivot, 1.0 / r);
  }
  return s;
}

double solve_ratio(const std::vector<double>& c, double pivot, double target) {
  if (compressed_power(c, pivot, 1.0) <= target) return 1.0;
  double lo = 1.0, hi = 64.0;
  if (compressed_power(c, pivot, hi) > target) return hi;   // the floor (n * pivot) is above the target: give it all
  for (int i = 0; i < 40; ++i) {   // monotone decreasing in r; 40 halvings of [1, 64] is far below float noise
    double mid = 0.5 * (lo + hi);
    if (compressed_power(c, pivot, mid) > target) lo = mid; else hi = mid;
  }
  return hi;
}

double LevelCompressor::update(const std::vector<double>& c, double now) {
  n_eff_ = effective_count(c);
  power_ = 0; for (double v : c) if (v > 0) power_ += v;
  double goal = power_;
  if (params_.cap > 0 && power_ > params_.cap)
    goal = params_.ratio > 1.0 ? params_.cap + (power_ - params_.cap) / params_.ratio : params_.cap;
  target_ = solve_ratio(c, params_.pivot, goal);
  if (last_ < 0 || params_.slew_s <= 0) { r_ = target_; }
  else {
    double dt = now - last_; if (dt < 0) dt = 0;
    double a = 1.0 - std::exp(-dt / params_.slew_s);
    r_ += (target_ - r_) * a;
  }
  last_ = now;
  return r_;
}

}  // namespace gd::core
