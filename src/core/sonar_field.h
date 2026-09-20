#pragma once
// The sonar as a field of per-entity metronomes (replaces the left-to-right sweep, which read as noise with a
// fixed viewpoint and fast-moving enemies). Every nearby thing repeats its OWN tone; the period shrinks as it
// nears the player -- mapped logarithmically in distance, so a step closer changes the rate a lot up close and
// little far off -- and its first pulse is seeded a stable per-id fraction of a period in (a hash of the id; the
// left/right position was the seed until 2026-09-20, which put a whole flank on one fraction) so co-distant things
// stagger, and a pulse due within `collide_s` of another of the same kind is pushed past it, so two things drifting
// through each other never fire as one. Engine-free: the host hands (id, distance, phase, kind)
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
  bool hash_phase = true;      // seed a new id at hash(id) of a period (false: the host's Item::phase, the old pan seed)
  double collide_s = 0.04;     // a due time within this of another same-kind due is pushed later past it (0 = off)
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
  struct Slot { double due, last_seen; int kind; };  // when its next pulse is due (its own phase grid), last frame it was listed
  // Push `due` later until no other same-kind due (already scheduled this frame in `keep`, or carried in next_at_)
  // lies within collide_s of it; a bounded walk, so a dense field costs at most a few windows of delay.
  double push_clear(double due, unsigned id, int kind, const std::unordered_map<unsigned, Slot>& keep) const;
  std::unordered_map<unsigned, Slot> next_at_;
};

}  // namespace gd::core
