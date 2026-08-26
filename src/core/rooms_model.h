#pragma once
// Engine-free model of the rooms data (assets/rooms.db, written by tools/rooms.py): a run-length-encoded
// label grid per region looked up by world position, a dwell-gated "current room" (hysteresis), and the
// exit cycling state. Bearings/distances are computed by the caller with world::clock_hour.
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace gd::core::rooms {

struct LabelGrid {
  double x0 = 0, z0 = 0, cell = 0.25;
  int w = 0, h = 0;
  std::vector<int16_t> labels;   // row-major [row = z][col = x], -1 = unwalkable
  // Height handling (db schema v2): heights = the base (lowest) layer's floor y in decimeters per cell
  // (INT16_MIN unknown; empty vector = no height data, lookups ignore y). Where walkable layers stack
  // (a bridge, an overpass, an upper floor) the upper layer is a sparse overlay cell whose label was
  // resolved offline (-1 = an enclosed upper floor no room owns: announce nothing, not the room below).
  struct Overlay { uint32_t cell; int16_t y_dm; int16_t label; };   // cell = row * w + col; sorted by cell
  std::vector<int16_t> heights_dm;
  std::vector<Overlay> overlays;

  bool decode_rle(const uint8_t* blob, size_t size, int width, int height);  // (int16 value, uint16 run)*
  bool decode_heights(const uint8_t* blob, size_t size);                     // same RLE; needs w/h set
  bool decode_overlays(const uint8_t* blob, size_t size);                    // {u16 row, u16 col, i16 y_dm, i16 label}*
  int at(int col, int row) const;                                              // -1 outside
  int at(int col, int row, double y) const;      // overlay-aware: picks the layer whose floor y is nearer
  // The floor height (world y) of the layer at(col,row,y) resolves to -- the overlay's when it picks the
  // overlay, else the base plane's. False when the grid has no height for the cell.
  bool floor_y(int col, int row, double y, double& out) const;
  bool floor_y_at(double x, double z, double y, double& out) const;   // world (x, z) -> the cell's floor y
  // The label under (x, z), searching rings of radius 0..ring cells for the nearest labelled cell
  // (wotr's RoomAt: a point just off the mesh still resolves to the room beside it). Pass the player's y
  // so a stacked cell resolves to the layer under their feet; NaN ignores height.
  int label_at(double x, double z, double y, int ring = 2) const;
  int label_at(double x, double z, int ring = 2) const;
  // Distinct OTHER-room labels whose cells fall within `radius` world units of (x, z), each with the nearest
  // such cell's world centre and distance (height-aware via at(col,row,y)). The caller filters these by real
  // reachability (world::find_path) to get the room's exits; the bearing to the nearest cell may point through
  // a wall (accepted -- it's the same as the old adjacency exits).
  struct Neighbor { int label; double x, z, dist; };
  std::vector<Neighbor> neighbors_within(double x, double z, double y, int current, double radius) const;

  // Is a navmesh path (world points, from room `a` to room `b`) a DIRECT route between them -- i.e. it never
  // spends more than `tol` world units inside a THIRD labelled room? Samples each segment at ~cell resolution
  // and resolves each sample with label_at(x, z, y, ring). Samples labelled `a`, `b`, or -1 (unlabelled / off
  // this grid) are fine; a contiguous run longer than `tol` inside some other room means the route detours
  // through it -> not a direct exit. The `tol` slack absorbs grid discretization at room boundaries. An empty
  // or single-point path is treated as direct (caller couldn't compute a corridor -> fail open).
  bool path_is_direct(const std::vector<std::array<double, 3>>& pts, int a, int b, int ring = 2,
                      double tol = 2.0) const;
  // The first point along the path (sampled like path_is_direct) whose cell is labelled `b`: where the route
  // actually ENTERS room `b` -- the opening, as opposed to the nearest cell of `b`'s blob, which can lie on the
  // far side of a wall. Returns false if no sample resolves to `b`.
  bool path_entry_point(const std::vector<std::array<double, 3>>& pts, int b, std::array<double, 3>& out,
                        int ring = 2) const;
};

// The current room changes immediately once the player has been in it for settle_ms (a genuine move);
// within the first settle_ms after a change (the back-and-forth on a boundary) a candidate must persist
// dwell_ms first. Returns true on the frame the current room changes. Off-grid observations (-1) are ignored.
struct Hysteresis {
  int dwell_ms = 400;
  int settle_ms = 1000;
  int current = -1;
  int candidate = -1;
  int candidate_since = 0;
  int entered = 0;           // when `current` became current
  bool update(int observed, int now_ms);
  void reset() { current = candidate = -1; candidate_since = entered = 0; }
};

// "i of n" cycling with wrap; next(count, +1/-1) from the last index (wotr's continue-from rule).
struct Cycle {
  int last = -1;
  int next(int count, int dir);
  void reset() { last = -1; }
};

}  // namespace gd::core::rooms
