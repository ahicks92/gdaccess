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
  double grace_s = 1.5;        // an id missing from the items keeps its phase grid this long (radius-edge flicker,
                               // a visibility blink) before it is forgotten and reseeded on its next sighting
  double period_for(float dist) const;   // log-in-distance interpolation between the two endpoints
};

class SonarField {
 public:
  struct Item { unsigned id; float dist; float phase; int kind; };   // phase 0..1 = left..right
  struct Ping { unsigned id; int kind; };
  // Advance to now_s over the current item set. Returns the ids whose tone pulses this frame (0 or more).
  // A newly seen id is seeded a phase-offset into its period (so a wave of arrivals staggers) and does not
  // fire on its first frame; an id absent from `items` keeps its grid for grace_s (an overdue one fires once on
  // its return, then continues on the grid), after which it is forgotten.
  std::vector<Ping> update(const std::vector<Item>& items, double now_s);
  void reset();
  FieldParams& params() { return params_; }
  const FieldParams& params() const { return params_; }
  std::size_t tracked() const { return next_at_.size(); }

 private:
  FieldParams params_;
  struct Slot { double due, last_seen; };            // when its next pulse is due (its own phase grid), last frame it was listed
  std::unordered_map<unsigned, Slot> next_at_;
};

}  // namespace gd::core

namespace gd::core {

// Crowd compression (2026-09-20). In a busy field the loudness of each thing stops being a usable distance
// signal (several things share the level range), so the level slope is flattened while the RATE signal stays
// exact. Input = the kind's estimated mean power over the coming window: each thing's contribution
// c = gain^2 / period (pulses per second times energy per pulse; the cues are loudness-matched so the sample
// energy drops out) and B = sum c. Below the cap nothing happens. Above it, the field is fitted to
// B_target = cap + (B - cap) / ratio (ratio <= 1 = a hard cap) by range compression: every contribution is
// mapped in dB toward a pivot, c' = pivot * (c / pivot)^(1/r), with r solved per frame so that sum c' = B_target
// -- so the things contributing most give the most, and nothing at or under the pivot is ever touched. The
// pivot sits at ~9 u, so distant things keep their level and rate. One compressor PER KIND
// (a pile of loot never compresses the enemies). r is slewed so a pack arriving does not step everything down.
struct CompressParams {
  double cap = 5.0;       // mean-power budget per kind (one enemy at 2 u is ~4.9; a nine-scarab pack at 2.6..8.8 u ~12)
  double ratio = 0.0;     // output ratio above the cap (in power); <= 1 = hard cap
  double pivot = 0.5;     // contribution that is left alone: an enemy beyond ~9 u (the radius edge is 0.1); chosen by ear 2026-09-20
                          // over 0.1, which spread the loss evenly over a pack (a 1.5 dB blanket) instead of tilting it onto the near ones
  double slew_s = 0.25;   // first-order time constant for r
};
double effective_count(const std::vector<double>& contributions);   // diagnostic: 1 when one dominates, n when n are equal
// Amplitude multiplier taking contribution c to pivot*(c/pivot)^(1/r): (c/pivot)^((1/r - 1)/2), clamped to <= 1.
float compress_gain(double c, double pivot, double r);
double compressed_power(const std::vector<double>& c, double pivot, double r);   // sum of c' at ratio r
// The ratio that fits the field under `target` (1 when it already fits; bisected, capped at 64).
double solve_ratio(const std::vector<double>& c, double pivot, double target);

class LevelCompressor {
 public:
  // Feed this frame's contributions (any order); returns the slewed ratio in force for this frame.
  double update(const std::vector<double>& contributions, double now_s);
  float gain_for(double c) const { return compress_gain(c, params_.pivot, r_); }
  double ratio() const { return r_; }
  double target_ratio() const { return target_; }
  double power() const { return power_; }       // this frame's B
  double n_eff() const { return n_eff_; }
  void reset() { r_ = target_ = n_eff_ = 1.0; power_ = 0.0; last_ = -1.0; }
  CompressParams& params() { return params_; }
  const CompressParams& params() const { return params_; }

 private:
  CompressParams params_;
  double r_ = 1.0, target_ = 1.0, n_eff_ = 1.0, power_ = 0.0, last_ = -1.0;
};

}  // namespace gd::core
