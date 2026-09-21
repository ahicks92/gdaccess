#include <doctest/doctest.h>
#include "core/cursor_step.h"
#include <cmath>

using namespace gd::core;
// The mod's pinned camera: yaw 0, screen-up = world -z, screen-right = world +x.
static const XZ kUp{0, -1}, kRight{1, 0};
static bool near(XZ a, float x, float z) { return std::fabs(a.x - x) < 1e-4f && std::fabs(a.z - z) < 1e-4f; }

TEST_CASE("grid mode moves one unit along the screen axes, wherever the player is") {
  XZ p{10, 10}, c{12, 12};
  CHECK(near(step_cursor(CursorMode::Grid, CursorKey::Forward, p, c, kUp, kRight), 12, 11));
  CHECK(near(step_cursor(CursorMode::Grid, CursorKey::Back, p, c, kUp, kRight), 12, 13));
  CHECK(near(step_cursor(CursorMode::Grid, CursorKey::Right, p, c, kUp, kRight), 13, 12));
  CHECK(near(step_cursor(CursorMode::Grid, CursorKey::Left, p, c, kUp, kRight), 11, 12));
}

TEST_CASE("polar forward and back move along the player-to-cursor line and stop at the player") {
  XZ p{0, 0}, c{3, 0};   // 3 units to the screen-right
  CHECK(near(step_cursor(CursorMode::Polar, CursorKey::Forward, p, c, kUp, kRight), 4, 0));
  CHECK(near(step_cursor(CursorMode::Polar, CursorKey::Back, p, c, kUp, kRight), 2, 0));
  XZ half{0.5f, 0};
  CHECK(near(step_cursor(CursorMode::Polar, CursorKey::Back, p, half, kUp, kRight), 0, 0));
  XZ diag{3, -4};   // 5 units away, up-right
  XZ out = step_cursor(CursorMode::Polar, CursorKey::Forward, p, diag, kUp, kRight);
  CHECK(near(out, 3.6f, -4.8f));
}

TEST_CASE("polar left and right turn the line 30 degrees about the player, keeping the distance") {
  XZ p{5, 5}, c{5, 3};   // 2 units screen-up of the player
  XZ r = step_cursor(CursorMode::Polar, CursorKey::Right, p, c, kUp, kRight);
  CHECK(near(r, 5 + 2 * std::sin(30 * 3.14159265f / 180), 5 - 2 * std::cos(30 * 3.14159265f / 180)));
  XZ l = step_cursor(CursorMode::Polar, CursorKey::Left, p, c, kUp, kRight);
  CHECK(near(l, 5 - 2 * std::sin(30 * 3.14159265f / 180), 5 - 2 * std::cos(30 * 3.14159265f / 180)));
  // three rights = 90 degrees: straight to the screen-right
  XZ q = c;
  for (int i = 0; i < 3; ++i) q = step_cursor(CursorMode::Polar, CursorKey::Right, p, q, kUp, kRight);
  CHECK(near(q, 7, 5));
}

TEST_CASE("polar from the player's own position starts the line screen-up; turning in place stays put") {
  XZ p{2, 2};
  CHECK(near(step_cursor(CursorMode::Polar, CursorKey::Forward, p, p, kUp, kRight), 2, 1));
  CHECK(near(step_cursor(CursorMode::Polar, CursorKey::Right, p, p, kUp, kRight), 2, 2));
  CHECK(near(step_cursor(CursorMode::Polar, CursorKey::Back, p, p, kUp, kRight), 2, 2));
}

TEST_CASE("polar remembers the line: pulling back through the player and pushing out again does not flip") {
  XZ p{0, 0}, c{2, 0};   // 2 units to the screen-right
  XZ heading{};
  XZ a = step_cursor(CursorMode::Polar, CursorKey::Back, p, c, kUp, kRight, &heading);
  CHECK(near(a, 1, 0));
  XZ b = step_cursor(CursorMode::Polar, CursorKey::Back, p, a, kUp, kRight, &heading);
  CHECK(near(b, 0, 0));
  XZ again = step_cursor(CursorMode::Polar, CursorKey::Back, p, b, kUp, kRight, &heading);
  CHECK(near(again, 0, 0));   // never past the player
  XZ d = step_cursor(CursorMode::Polar, CursorKey::Forward, p, again, kUp, kRight, &heading);
  CHECK(near(d, 1, 0));       // back out along the same line, not screen-up, not behind
  // turning while on the player turns the remembered line: right 90 degrees from screen-right = screen-down
  XZ h2{1, 0}; XZ on = p;
  for (int i = 0; i < 3; ++i) on = step_cursor(CursorMode::Polar, CursorKey::Right, p, on, kUp, kRight, &h2);
  CHECK(near(on, 0, 0));
  CHECK(near(step_cursor(CursorMode::Polar, CursorKey::Forward, p, on, kUp, kRight, &h2), 0, 1));
  // a grid step also sets the heading, so switching to polar continues from where the cursor is
  XZ h3{}; XZ g = step_cursor(CursorMode::Grid, CursorKey::Left, p, p, kUp, kRight, &h3);
  CHECK(near(g, -1, 0));
  CHECK(near(h3, -1, 0));
}

TEST_CASE("the axes are taken as given: a rotated camera rotates the grid") {
  XZ up{-1, 0}, right{0, -1};   // yaw 90 degrees: screen-up = world -x
  CHECK(near(step_cursor(CursorMode::Grid, CursorKey::Forward, {0, 0}, {0, 0}, up, right), -1, 0));
  CHECK(near(step_cursor(CursorMode::Polar, CursorKey::Forward, {0, 0}, {0, 0}, up, right), -1, 0));
}
