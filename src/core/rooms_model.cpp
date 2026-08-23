#include "core/rooms_model.h"
#include <cmath>
#include <cstring>

namespace gd::core::rooms {

bool LabelGrid::decode_rle(const uint8_t* blob, size_t size, int width, int height) {
  w = width; h = height;
  labels.assign((size_t)w * h, -1);
  size_t pos = 0;
  for (size_t i = 0; i + 4 <= size; i += 4) {
    int16_t v; uint16_t n;
    std::memcpy(&v, blob + i, 2);
    std::memcpy(&n, blob + i + 2, 2);
    if (pos + n > labels.size()) return false;
    for (uint16_t k = 0; k < n; ++k) labels[pos + k] = v;
    pos += n;
  }
  return pos == labels.size();
}

int LabelGrid::at(int col, int row) const {
  if (col < 0 || row < 0 || col >= w || row >= h) return -1;
  return labels[(size_t)row * w + col];
}

int LabelGrid::label_at(double x, double z, int ring) const {
  if (w <= 0 || h <= 0 || cell <= 0) return -1;
  int col = (int)std::floor((x - x0) / cell), row = (int)std::floor((z - z0) / cell);
  for (int r = 0; r <= ring; ++r) {
    int best = -1; double best_d = 1e30;
    for (int dr = -r; dr <= r; ++dr)
      for (int dc = -r; dc <= r; ++dc) {
        if (std::abs(dr) != r && std::abs(dc) != r) continue;   // ring only
        int l = at(col + dc, row + dr);
        if (l < 0) continue;
        double d = (double)dr * dr + (double)dc * dc;
        if (d < best_d) { best_d = d; best = l; }
      }
    if (best >= 0) return best;
  }
  return -1;
}

bool Hysteresis::update(int observed, int now_ms) {
  if (observed == current) { candidate = -1; return false; }
  if (observed < 0) { candidate = -1; return false; }       // off the grid: keep the current room
  if (observed != candidate) { candidate = observed; candidate_since = now_ms; }
  if (now_ms - candidate_since >= dwell_ms || current < 0) {  // the first room is immediate
    current = observed; candidate = -1;
    return true;
  }
  return false;
}

int Cycle::next(int count, int dir) {
  if (count <= 0) { last = -1; return -1; }
  if (last < 0 || last >= count) last = dir > 0 ? 0 : count - 1;
  else last = ((last + dir) % count + count) % count;
  return last;
}

}  // namespace gd::core::rooms
