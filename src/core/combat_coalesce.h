#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Merges the floating combat numbers that land at the same place inside a short window into one number, so
// an area attack on one enemy (or several hits in one tick) is spoken once with the sum instead of as a pile
// of overlapping voices. Words (Miss / Dodge / Block) are never merged. Engine-free, unit-tested.
namespace gd::core {
class CombatCoalescer {
 public:
  struct In { bool is_number; double amount; bool crit; std::string word; float x, z; float pan, gain; double t; };
  struct Out { bool is_number; double amount; bool crit; std::string word; float pan, gain; };
  void set_enabled(bool on) { enabled_ = on; }
  bool enabled() const { return enabled_; }
  void set_window(double seconds) { window_ = seconds < 0 ? 0 : seconds; }
  double window() const { return window_; }
  void set_max_per_flush(int n) { max_per_flush_ = n < 1 ? 1 : n; }
  void push(const In& e);                 // game thread, from the hook's drained events
  std::vector<Out> flush(double now);     // once per frame
  size_t pending() const { return buckets_.size() + words_.size(); }
  uint64_t merged() const { return merged_; }
  uint64_t dropped() const { return dropped_; }
  void clear() { buckets_.clear(); words_.clear(); }

 private:
  struct Bucket { int kx, kz; bool crit; double amount; float pan, gain; double opened; };
  bool enabled_ = true;
  double window_ = 0.15;
  int max_per_flush_ = 4;
  std::vector<Bucket> buckets_;
  std::vector<Out> words_;
  uint64_t merged_ = 0, dropped_ = 0;
};
}  // namespace gd::core
