#include "combat_coalesce.h"
#include <cmath>

namespace gd::core {
void CombatCoalescer::push(const In& e) {
  if (!e.is_number) { words_.push_back({false, 0, e.crit, e.word, e.pan, e.gain}); return; }
  if (!enabled_) { words_.push_back({true, e.amount, e.crit, {}, e.pan, e.gain}); return; }
  int kx = (int)std::lround(e.x), kz = (int)std::lround(e.z);
  for (Bucket& b : buckets_) {
    if (b.kx == kx && b.kz == kz && b.crit == e.crit && e.t - b.opened < window_) {
      b.amount += e.amount; b.pan = e.pan; b.gain = e.gain; ++merged_;
      return;
    }
  }
  buckets_.push_back({kx, kz, e.crit, e.amount, e.pan, e.gain, e.t});
}

std::vector<CombatCoalescer::Out> CombatCoalescer::flush(double now) {
  std::vector<Out> out;
  // Words first (they are rare and carry meaning), then the buckets whose window has closed, in arrival order.
  for (Out& w : words_) out.push_back(std::move(w));
  words_.clear();
  for (size_t i = 0; i < buckets_.size();) {
    Bucket& b = buckets_[i];
    if (now - b.opened >= window_) {
      out.push_back({true, b.amount, b.crit, {}, b.pan, b.gain});
      buckets_.erase(buckets_.begin() + (long long)i);
    } else ++i;
  }
  if ((int)out.size() > max_per_flush_) {
    dropped_ += out.size() - max_per_flush_;
    out.resize(max_per_flush_);
  }
  return out;
}
}  // namespace gd::core
