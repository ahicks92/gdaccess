#pragma once
// The sonar sweep scheduler, ported from wotr-access SonarSystem: everything sonifiable near the listener
// is pinged once per sweep, one voice at a time from left to right, the gap between voices shrinking with
// the count (gap = clamp(spread / count, gap_min, gap_max)), then a rest before the next sweep. Engine-free:
// the host builds the entry list at the start of each sweep and plays whatever `next` hands back.
#include <optional>
#include <vector>

namespace gd::core {

struct SweepParams {
  double spread_s = 0.75;   // a whole sweep wants to take about this long
  double gap_min_s = 0.10;  // but never faster than this between voices
  double gap_max_s = 0.20;  // nor slower (a lone item still repeats every gap_max + rest)
  double rest_s = 0.40;     // silence between sweeps
  double gap_for(int count) const;
};

struct SweepEntry {
  unsigned id = 0;
  float lateral = 0;   // ear-frame right offset (sort key: left to right)
  int kind = 0;        // host-defined (which cue)
};

class SonarSweep {
 public:
  // True when the host should build a fresh entry list (first call, or the rest after a sweep elapsed).
  bool wants_entries(double now_s) const;
  // Start a sweep over `entries` (sorted here by lateral). An empty list rests until the next check.
  void begin(std::vector<SweepEntry> entries, double now_s);
  // The entry to fire now, if its slot has come. At most one per call; call every frame.
  std::optional<SweepEntry> next(double now_s);
  void reset();
  const SweepParams& params() const { return params_; }
  SweepParams& params() { return params_; }
  int remaining() const { return (int)entries_.size() - index_; }

 private:
  SweepParams params_;
  std::vector<SweepEntry> entries_;
  int index_ = 0;
  double next_at_ = 0;     // when the next voice (or the next sweep) may fire
  bool sweeping_ = false;
  bool started_ = false;
};

}  // namespace gd::core
