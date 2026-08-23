#include "core/sonar_sweep.h"
#include <algorithm>

namespace gd::core {

double SweepParams::gap_for(int count) const {
  double g = spread_s / (count < 1 ? 1 : count);
  return std::clamp(g, gap_min_s, gap_max_s);
}

bool SonarSweep::wants_entries(double now_s) const {
  if (!started_) return true;
  return !sweeping_ && now_s >= next_at_;
}

void SonarSweep::begin(std::vector<SweepEntry> entries, double now_s) {
  std::sort(entries.begin(), entries.end(), [](const SweepEntry& a, const SweepEntry& b) { return a.lateral < b.lateral; });
  entries_ = std::move(entries);
  index_ = 0;
  started_ = true;
  if (entries_.empty()) { sweeping_ = false; next_at_ = now_s + params_.rest_s; return; }
  sweeping_ = true;
  next_at_ = now_s;  // the first voice fires at once
}

std::optional<SweepEntry> SonarSweep::next(double now_s) {
  if (!sweeping_ || now_s < next_at_) return std::nullopt;
  SweepEntry e = entries_[index_++];
  if (index_ >= (int)entries_.size()) { sweeping_ = false; next_at_ = now_s + params_.rest_s; }
  else next_at_ = now_s + params_.gap_for((int)entries_.size());
  return e;
}

void SonarSweep::reset() { entries_.clear(); index_ = 0; sweeping_ = false; started_ = false; next_at_ = 0; }

}  // namespace gd::core
