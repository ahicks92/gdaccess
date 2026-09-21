#pragma once
// The free cursor's arithmetic (docs/controls.md "Advanced targeting"): where Shift+W/A/S/D move a world point.
// Engine-free: the caller supplies the player's and the cursor's world xz and the screen axes (unit vectors of
// screen-up and screen-right in world xz, world::screen_axes), and stores the result.
namespace gd::core {
enum class CursorMode { Grid, Polar };                  // Grid = 1 unit along the screen axes; Polar = along / around the player-to-cursor line
enum class CursorKey { Forward, Back, Left, Right };    // W S A D
struct XZ { float x = 0, z = 0; };
constexpr float kCursorStep = 1.0f;       // units per press (both modes' W/S, Grid's A/D)
constexpr float kCursorTurnDeg = 30.0f;   // Polar A/D
// Grid: Forward/Back = +/- up, Right/Left = +/- right. Polar: Forward/Back = 1 unit out / in along the line from
// the player through the cursor (never past the player), Right/Left = the line turned 30 degrees clockwise /
// counterclockwise about the player, distance kept. The cursor is always the FAR end of the line.
// heading: the line's direction (unit, world xz), remembered by the caller across steps: read only when the cursor
// sits on the player (Back pulled it all the way in), so the next Forward goes back out along the same line
// instead of flipping or restarting; written whenever the result is off the player. Null or zero = screen-up.
XZ step_cursor(CursorMode mode, CursorKey key, XZ player, XZ cursor, XZ up, XZ right, XZ* heading = nullptr);
}
