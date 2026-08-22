#pragma once

// Announces a fraction (health / max) when it moves into another step bucket: with step 0.10, 79 % is in the
// "70" bucket, so going from 81 % to 79 % fires 70 and climbing back from 69 % to 71 % fires 70 again -- one
// announcement per decade crossed, in either direction, no jitter inside a decade. Engine-free, unit-tested.
namespace gd::core {
class ThresholdWatcher {
 public:
  explicit ThresholdWatcher(double step = 0.10) : step_(step > 0 ? step : 0.10) {}
  void reset() { bucket_ = -1; }
  // true -> announce out_percent (the bucket now occupied, as a percentage). The first update after a reset
  // only records where we are.
  bool update(double fraction, int& out_percent);
  int bucket() const { return bucket_; }

 private:
  double step_;
  int bucket_ = -1;
};
}  // namespace gd::core
