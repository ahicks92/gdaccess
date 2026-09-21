#include "cursor_step.h"
#include <cmath>

namespace gd::core {
namespace {
constexpr float kPi = 3.14159265358979f;
constexpr float kOnPlayer = 1e-3f;
void remember_heading(XZ* heading, XZ player, XZ result) {
  if (!heading) return;
  float vx = result.x - player.x, vz = result.z - player.z, d = std::sqrt(vx * vx + vz * vz);
  if (d > kOnPlayer) *heading = {vx / d, vz / d};
}
}
XZ step_cursor(CursorMode mode, CursorKey key, XZ player, XZ cursor, XZ up, XZ right, XZ* heading) {
  if (mode == CursorMode::Grid) {
    XZ d = key == CursorKey::Forward ? up : key == CursorKey::Back ? XZ{-up.x, -up.z} : key == CursorKey::Right ? right : XZ{-right.x, -right.z};
    XZ out{cursor.x + d.x * kCursorStep, cursor.z + d.z * kCursorStep};
    remember_heading(heading, player, out);
    return out;
  }
  // Polar: the cursor in the (up, right) frame about the player; theta 0 = screen-up, positive toward screen-right.
  float vx = cursor.x - player.x, vz = cursor.z - player.z;
  float f = vx * up.x + vz * up.z, r = vx * right.x + vz * right.z;
  float dist = std::sqrt(f * f + r * r);
  float theta = 0.0f;
  if (dist > kOnPlayer) theta = std::atan2(r, f);
  else {
    dist = 0.0f;
    if (heading && (heading->x != 0.0f || heading->z != 0.0f))   // on the player: the remembered line
      theta = std::atan2(heading->x * right.x + heading->z * right.z, heading->x * up.x + heading->z * up.z);
  }
  switch (key) {
    case CursorKey::Forward: dist += kCursorStep; break;
    case CursorKey::Back: dist = dist > kCursorStep ? dist - kCursorStep : 0.0f; break;
    case CursorKey::Right: theta += kCursorTurnDeg * kPi / 180.0f; break;
    case CursorKey::Left: theta -= kCursorTurnDeg * kPi / 180.0f; break;
  }
  float c = std::cos(theta), s = std::sin(theta);
  XZ out{player.x + dist * (c * up.x + s * right.x), player.z + dist * (c * up.z + s * right.z)};
  remember_heading(heading, player, out);
  if (heading && dist == 0.0f) *heading = {c * up.x + s * right.x, c * up.z + s * right.z};   // turned in place: keep the turn
  return out;
}
}
