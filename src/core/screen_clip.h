#pragma once
// Where a straight line from a point inside a rectangle toward a target leaves that rectangle. Used for mouse
// transitions the mod injects: the game's mouse handler ignores events outside its client area, so a button
// release aimed at a target that has left the window is delivered at the last on-screen point of the
// player-to-target line instead (inset by `margin` pixels so it is unambiguously inside). Engine-free.
namespace gd::core {

// Clips the segment from (fx, fy) toward (tx, ty) to the rectangle [margin, w - margin] x [margin, h - margin].
// Returns false when the start point itself is outside the inset rectangle (nothing of the line is on screen).
// Otherwise writes the target when it is inside, else the point where the segment crosses the rectangle's edge.
inline bool clip_toward(float fx, float fy, float tx, float ty, float w, float h, float margin, float& ox, float& oy) {
  const float x0 = margin, y0 = margin, x1 = w - margin, y1 = h - margin;
  if (x1 <= x0 || y1 <= y0) return false;
  if (fx < x0 || fx > x1 || fy < y0 || fy > y1) return false;
  float t = 1.0f;   // fraction of the segment that stays inside
  const float dx = tx - fx, dy = ty - fy;
  if (dx > 0 && tx > x1) t = (x1 - fx) / dx < t ? (x1 - fx) / dx : t;
  if (dx < 0 && tx < x0) t = (x0 - fx) / dx < t ? (x0 - fx) / dx : t;
  if (dy > 0 && ty > y1) t = (y1 - fy) / dy < t ? (y1 - fy) / dy : t;
  if (dy < 0 && ty < y0) t = (y0 - fy) / dy < t ? (y0 - fy) / dy : t;
  if (t < 0) t = 0;
  ox = fx + dx * t;
  oy = fy + dy * t;
  return true;
}

// The farthest point of the segment from (fx, fy) to (tx, ty) that lies in none of `rects` (items with x, y, w, h;
// the HUD's screen rectangles), sampled from the target end back toward the start in `steps` even steps and never
// closer to the start than `min_t` of the way (a point on the player's own body is not an aim). False when every
// sample is covered. Used to aim a press along the player-to-target line when the target's own point is on the HUD.
template <class Rects>
inline bool back_off_rects(float fx, float fy, float tx, float ty, const Rects& rects, int steps, float min_t, float& ox, float& oy) {
  if (steps < 1) steps = 1;
  for (int i = 0; i <= steps; ++i) {
    float t = 1.0f - (float)i / (float)steps;
    if (t < min_t) return false;
    float x = fx + (tx - fx) * t, y = fy + (ty - fy) * t;
    bool covered = false;
    for (const auto& r : rects) if (x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h) { covered = true; break; }
    if (!covered) { ox = x; oy = y; return true; }
  }
  return false;
}

}  // namespace gd::core
