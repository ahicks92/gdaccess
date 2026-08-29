#include "threshold_watcher.h"
#include <cmath>

namespace gd::core {
bool ThresholdWatcher::update(double fraction, int& out_percent) {
  if (fraction < 0) fraction = 0;
  if (fraction > 1) fraction = 1;
  int steps = (int)std::lround(1.0 / step_);
  int b = (int)std::floor(fraction / step_ + 1e-9);
  if (b > steps) b = steps;
  if (b < 0) b = 0;
  if (bucket_ < 0) { bucket_ = b; return false; }
  if (b == bucket_) return false;
  bucket_ = b;
  out_percent = (int)std::lround(fraction * 100.0);   // the actual value at the crossing, not the bucket's floor
  return true;
}
}  // namespace gd::core
