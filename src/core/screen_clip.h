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

}  // namespace gd::core
