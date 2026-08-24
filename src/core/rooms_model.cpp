#include "core/rooms_model.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

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

bool LabelGrid::decode_heights(const uint8_t* blob, size_t size) {
  heights_dm.assign((size_t)w * h, INT16_MIN);
  size_t pos = 0;
  for (size_t i = 0; i + 4 <= size; i += 4) {
    int16_t v; uint16_t n;
    std::memcpy(&v, blob + i, 2);
    std::memcpy(&n, blob + i + 2, 2);
    if (pos + n > heights_dm.size()) { heights_dm.clear(); return false; }
    for (uint16_t k = 0; k < n; ++k) heights_dm[pos + k] = v;
    pos += n;
  }
  if (pos != heights_dm.size()) { heights_dm.clear(); return false; }
  return true;
}

bool LabelGrid::decode_overlays(const uint8_t* blob, size_t size) {
  overlays.clear();
  for (size_t i = 0; i + 8 <= size; i += 8) {
    uint16_t row, col; int16_t y_dm, label;
    std::memcpy(&row, blob + i, 2);
    std::memcpy(&col, blob + i + 2, 2);
    std::memcpy(&y_dm, blob + i + 4, 2);
    std::memcpy(&label, blob + i + 6, 2);
    overlays.push_back({(uint32_t)row * (uint32_t)w + col, y_dm, label});
  }
  std::sort(overlays.begin(), overlays.end(), [](const Overlay& a, const Overlay& b) { return a.cell < b.cell; });
  return true;
}

int LabelGrid::at(int col, int row) const {
  if (col < 0 || row < 0 || col >= w || row >= h) return -1;
  return labels[(size_t)row * w + col];
}

int LabelGrid::at(int col, int row, double y) const {
  int base = at(col, row);
  if (std::isnan(y) || overlays.empty() || col < 0 || row < 0 || col >= w || row >= h) return base;
  uint32_t cell_i = (uint32_t)row * (uint32_t)w + col;
  auto it = std::lower_bound(overlays.begin(), overlays.end(), cell_i,
                             [](const Overlay& o, uint32_t c) { return o.cell < c; });
  if (it == overlays.end() || it->cell != cell_i) return base;
  int16_t base_dm = heights_dm.empty() ? INT16_MIN : heights_dm[cell_i];
  if (base_dm == INT16_MIN) return base;
  double y_dm = y * 10.0;
  return std::abs(y_dm - it->y_dm) < std::abs(y_dm - base_dm) ? it->label : base;
}

int LabelGrid::label_at(double x, double z, double y, int ring) const {
  if (w <= 0 || h <= 0 || cell <= 0) return -1;
  int col = (int)std::floor((x - x0) / cell), row = (int)std::floor((z - z0) / cell);
  for (int r = 0; r <= ring; ++r) {
    int best = -1; double best_d = 1e30;
    for (int dr = -r; dr <= r; ++dr)
      for (int dc = -r; dc <= r; ++dc) {
        if (std::abs(dr) != r && std::abs(dc) != r) continue;   // ring only
        int l = at(col + dc, row + dr, y);
        if (l < 0) continue;
        double d = (double)dr * dr + (double)dc * dc;
        if (d < best_d) { best_d = d; best = l; }
      }
    if (best >= 0) return best;
  }
  return -1;
}

int LabelGrid::label_at(double x, double z, int ring) const {
  return label_at(x, z, std::numeric_limits<double>::quiet_NaN(), ring);
}

std::vector<LabelGrid::Neighbor> LabelGrid::neighbors_within(double x, double z, double y, int current, double radius) const {
  std::vector<Neighbor> out;
  if (w <= 0 || h <= 0 || cell <= 0 || radius <= 0) return out;
  int col = (int)std::floor((x - x0) / cell), row = (int)std::floor((z - z0) / cell);
  int rc = (int)std::ceil(radius / cell);
  double r2 = radius * radius;
  for (int dr = -rc; dr <= rc; ++dr)
    for (int dc = -rc; dc <= rc; ++dc) {
      int l = at(col + dc, row + dr, y);
      if (l < 0 || l == current) continue;
      double cx = x0 + (col + dc + 0.5) * cell, cz = z0 + (row + dr + 0.5) * cell;
      double d2 = (cx - x) * (cx - x) + (cz - z) * (cz - z);
      if (d2 > r2) continue;
      bool found = false;
      for (Neighbor& n : out)
        if (n.label == l) { if (d2 < n.dist) { n.x = cx; n.z = cz; n.dist = d2; } found = true; break; }
      if (!found) out.push_back({l, cx, cz, d2});
    }
  for (Neighbor& n : out) n.dist = std::sqrt(n.dist);
  return out;
}

bool LabelGrid::path_is_direct(const std::vector<std::array<double, 3>>& pts, int a, int b, int ring,
                               double tol) const {
  if (pts.size() < 2) return true;   // no corridor to judge -> fail open
  const double step = cell > 0 ? std::min(cell, 1.0) : 1.0;   // fine enough to catch a thin room
  int foreign = -1;      // the third room the current contiguous run is inside (-1 = not in one)
  double run = 0;        // world-unit length accumulated inside `foreign`
  for (size_t i = 1; i < pts.size(); ++i) {
    const double ax = pts[i - 1][0], ay = pts[i - 1][1], az = pts[i - 1][2];
    const double dx = pts[i][0] - ax, dy = pts[i][1] - ay, dz = pts[i][2] - az;
    const double seg = std::sqrt(dx * dx + dz * dz);
    const int n = std::max(1, (int)std::ceil(seg / step));
    const double inc = seg / n;
    // Sample k = 1..n (skip k = 0: it duplicates the previous segment's endpoint).
    for (int k = 1; k <= n; ++k) {
      const double t = (double)k / n;
      const int l = label_at(ax + dx * t, az + dz * t, ay + dy * t, ring);
      if (l < 0 || l == a || l == b) { foreign = -1; run = 0; continue; }
      if (l != foreign) { foreign = l; run = 0; }
      run += inc;
      if (run > tol) return false;   // the route lingers in a third room -> not a direct exit
    }
  }
  return true;
}

bool Hysteresis::update(int observed, int now_ms) {
  if (observed == current) { candidate = -1; return false; }
  if (observed < 0) { candidate = -1; return false; }       // off the grid: keep the current room
  bool settled = current < 0 || now_ms - entered >= settle_ms;   // the first room is immediate too
  if (!settled) {
    if (observed != candidate) { candidate = observed; candidate_since = now_ms; }
    if (now_ms - candidate_since < dwell_ms) return false;
  }
  current = observed; candidate = -1; entered = now_ms;
  return true;
}

int Cycle::next(int count, int dir) {
  if (count <= 0) { last = -1; return -1; }
  if (last < 0 || last >= count) last = dir > 0 ? 0 : count - 1;
  else last = ((last + dir) % count + count) % count;
  return last;
}

}  // namespace gd::core::rooms
