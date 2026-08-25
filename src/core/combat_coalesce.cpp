#include "combat_coalesce.h"
#include <cmath>

namespace gd::core {
void CombatCoalescer::push(const In& e) {
  bool has_num = e.is_number;
  bool has_tags = !e.tags.empty();
  // Words (Miss / Dodge / Block) stand alone and are never merged.
  if (!has_num && !has_tags) { words_.push_back({false, 0, e.crit, e.word, {}, e.pan, e.gain}); return; }
  if (!enabled_) {  // coalescing off: numbers and effects pass straight through, unmerged
    if (has_num) words_.push_back({true, e.amount, e.crit, {}, e.tags, e.pan, e.gain});
    else words_.push_back({false, 0, false, {}, e.tags, e.pan, e.gain});
    return;
  }
  int kx = (int)std::lround(e.x), kz = (int)std::lround(e.z);
  // Attach to an open bucket at this place: numbers merge only with matching crit-ness, but an effect token
  // (or a number landing on an effect-only bucket) adopts the bucket regardless.
  for (Bucket& b : buckets_) {
    if (b.kx != kx || b.kz != kz || e.t - b.opened >= window_) continue;
    if (has_num && b.has_number && b.crit != e.crit) continue;
    if (has_num) {
      if (!b.has_number) { b.has_number = true; b.crit = e.crit; b.amount = e.amount; }
      else b.amount += e.amount;
    }
    for (const std::string& t : e.tags) b.tags.push_back(t);
    b.pan = e.pan; b.gain = e.gain; ++merged_;
    return;
  }
  buckets_.push_back({kx, kz, e.crit, has_num, e.amount, e.tags, e.pan, e.gain, e.t});
}

std::vector<CombatCoalescer::Out> CombatCoalescer::flush(double now) {
  std::vector<Out> out;
  // Words first (they are rare and carry meaning), then the buckets whose window has closed, in arrival order.
  for (Out& w : words_) out.push_back(std::move(w));
  words_.clear();
  for (size_t i = 0; i < buckets_.size();) {
    Bucket& b = buckets_[i];
    if (now - b.opened >= window_) {
      out.push_back({b.has_number, b.amount, b.crit, {}, std::move(b.tags), b.pan, b.gain});
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
