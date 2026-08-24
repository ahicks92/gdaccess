#pragma once
// The sonar as a field of per-entity metronomes (replaces the left-to-right sweep, which read as noise with a
// fixed viewpoint and fast-moving enemies). Every nearby thing repeats its OWN tone; the period shrinks as it
// nears the player -- mapped logarithmically in distance, so a step closer changes the rate a lot up close and
// little far off -- and its left/right position offsets the phase (50 % from the left = half a period late) so
// co-distant things stagger instead of firing as one. Engine-free: the host hands (id, distance, phase, kind)
// each frame and plays back whatever `update` reports fired, re-evaluating pan/gain at play time.
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace gd::core {

struct FieldParams {
  double period_near = 0.14;   // seconds between pulses at dist_near and closer (urgent)
  double period_far = 0.80;    // seconds at dist_far and beyond (2x the old flat 0.40s sweep cadence)
  float dist_near = 2.0f;      // at/under this distance, period_near
  float dist_far = 25.0f;      // at/over this distance, period_far
  double period_for(float dist) const;   // log-in-distance interpolation between the two endpoints
};

class SonarField {
 public:
  struct Item { unsigned id; float dist; float phase; int kind; };   // phase 0..1 = left..right
  struct Ping { unsigned id; int kind; };
  // Advance to now_s over the current item set. Returns the ids whose tone pulses this frame (0 or more).
  // A newly seen id is seeded a phase-offset into its period (so a wave of arrivals staggers) and does not
  // fire on its first frame; ids absent from `items` are forgotten.
  std::vector<Ping> update(const std::vector<Item>& items, double now_s);
  void reset();
  FieldParams& params() { return params_; }
  const FieldParams& params() const { return params_; }
  std::size_t tracked() const { return next_at_.size(); }

 private:
  FieldParams params_;
  std::unordered_map<unsigned, double> next_at_;   // per id, when its next pulse is due (its own phase grid)
};

}  // namespace gd::core
