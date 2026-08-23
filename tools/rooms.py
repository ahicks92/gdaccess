"""Room segmentation over the game's baked navmesh (offline; never touches the running game), and the
rooms database the mod ships (assets/rooms.db). docs/rooms.md has the model and vocabulary.

  uv run tools/rooms.py regions [--grep devils]            list chunks (name, location, world offset)
  uv run tools/rooms.py grid 0A001 [0A002 ...]            extract + walkable grid stats + clearance PNG
  uv run tools/rooms.py segment 0A001 [...] [params]       one chunk, floor plan only (experiments)
  uv run tools/rooms.py area devilscrossing [params] [--write] [--name "Devil's Crossing"] [--set persist=1.0,...]
                                  all overworld chunks of a region (world-map location), stitched;
                                  --write stores grid/rooms/exits in the db, --set stores params
  uv run tools/rooms.py status                             per-region counts and staleness from the db
  uv run tools/rooms.py plan devilscrossing               floor plan from the db (what the mod will use)

Params (--persist --min-area --cut-floor --furniture --max-walk --min-split --island-min) default to the
region's stored params, then to gdmap.rooms.Params. Outputs go to build/rooms/ (PNG floor plans; north = -z
= up). Road overlay (--roads) = terrain layers whose record name matches ROAD_RE (a first guess)."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import sys
import time
from dataclasses import asdict, fields

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gdmap.level import mask_to_cells, parse_terrain_layers, parse_tiles, walk_grid  # noqa: E402
from gdmap.mapfile import WorldMap  # noqa: E402
from gdmap.render import render, render_grid  # noqa: E402
from gdmap.rooms import ALGO_VERSION, Params, Segmentation, segment, stitch  # noqa: E402
from gdmap.roomsdb import RoomsDb  # noqa: E402

OUT = "build/rooms"
DB = "assets/rooms.db"
ROAD_RE = re.compile(r"gravel|cobble|flagstone|fieldstone|brick|tile|plank|road|path|pav", re.I)
PARAM_FLAGS = {"persist": "persist", "min_area": "min-area", "cut_floor": "cut-floor", "furniture_max": "furniture",
               "max_walk": "max-walk", "min_split_area": "min-split", "island_min": "island-min"}


def add_params(ap):
    for field, flag in PARAM_FLAGS.items():
        ap.add_argument(f"--{flag}", dest=field, type=float, default=None)
    ap.add_argument("--roads", action="store_true")
    ap.add_argument("--scale", type=int, default=2)
    ap.add_argument("--out", default=None)


def params_from(args, stored: dict | None) -> Params:
    p = Params(**{k: v for k, v in (stored or {}).items() if k in {f.name for f in fields(Params)}})
    for field in PARAM_FLAGS:
        v = getattr(args, field, None)
        if v is not None:
            setattr(p, field, v)
    return p


def load_region(wm: WorldMap, name: str):
    r = wm.by_name[name]
    body = wm.level_body(r)
    tiles = parse_tiles(body)
    grid = walk_grid(tiles)
    return r, body, grid


def road_cells(body, grid, region):
    layers = parse_terrain_layers(body)
    out = np.zeros(grid.shape, dtype=bool)
    names = []
    for l in layers:
        if l.mask is not None and ROAD_RE.search(l.record):
            out |= mask_to_cells(l.mask, grid, float(region.world_offset[0]), float(region.world_offset[2]))
            names.append(l.record.rsplit("/", 1)[-1])
    return out, names


def report(seg, grid, elapsed):
    for line in seg.log:
        print("  " + line)
    print(f"  {elapsed:.1f}s; walkable {int(grid.walk.sum()) * 0.0625:.0f} m2 in {grid.shape[1]}x{grid.shape[0]} cells")
    by_cls = {}
    for r in seg.rooms:
        by_cls[r.cls] = by_cls.get(r.cls, 0) + 1
    print("  classes:", ", ".join(f"{k} {v}" for k, v in sorted(by_cls.items())),
          "; islands:", sum(1 for r in seg.rooms if r.island), "; cap cuts:", sum(1 for e in seg.exits if e.cut))
    for r in sorted(seg.rooms, key=lambda r: -r.area)[:8]:
        print(f"  room {r.id:3d} {r.cls:8s} area {r.area:6.0f} walk {r.walk:5.0f} clear {r.mean_clear:4.1f} "
              f"elong {r.elongation:4.1f} anchor ({r.anchor[0]:.0f}, {r.anchor[1]:.0f})")


def cmd_regions(wm, args):
    for r in wm.regions:
        if args.grep and args.grep.lower() not in (r.name + " " + r.location).lower():
            continue
        print(f"{r.name:24s} {r.location:40s} idx={r.index:3d} offset {r.world_offset} size {r.size:9d}"
              + (" underground" if r.underground else ""))
    print(len(wm.regions), "regions")


def cmd_grid(wm, args):
    os.makedirs(OUT, exist_ok=True)
    from scipy import ndimage
    for name in args.regions:
        r, body, grid = load_region(wm, name)
        clear = ndimage.distance_transform_edt(grid.walk) * 0.25
        wc = clear[grid.walk]
        print(f"{name} ({r.location}) offset {r.world_offset}: grid {grid.shape[1]}x{grid.shape[0]} cells from "
              f"({grid.x0:.0f},{grid.z0:.0f}); walkable {wc.size * 0.0625:.0f} m2; clearance median {np.median(wc):.1f} "
              f"p90 {np.percentile(wc, 90):.1f} max {wc.max():.1f}; multi-layer cells {grid.layers}; "
              f"signature {wm.signature(body)}")
        layers = parse_terrain_layers(body)
        print("  terrain layers:", ", ".join(l.record.rsplit('/', 1)[-1] + ("" if l.mask is not None else " (base)") for l in layers))
        render_grid(grid, f"{OUT}/{name}_clear.png", scale=1, clearance=clear)


def build_area(wm, regs, want_roads):
    """Stitch the chunk records; returns (grid, roads mask or None, chunks used, signature).
    Takes Region records, not names: chunk NAMES collide in the map (two different 0W021s; the by_name
    first-record-wins dict silently swapped Hargate's Isle for a far-west water chunk, 2026-08-23)."""
    grids, hasher = [], hashlib.blake2b(digest_size=16)
    for r in regs:
        name = r.name
        body = wm.level_body(r)
        tiles = parse_tiles(body)
        hasher.update(wm.signature(body).encode())
        if not tiles:
            print(f"  {name}: no nav tiles, skipped")
            continue
        grids.append((r, body, walk_grid(tiles)))
    # tile coordinates are world coordinates (verified live 2026-08-22): no placement offsets
    parts = [(g, (0.0, 0.0)) for r, _, g in grids]
    grid = stitch(parts) if len(parts) > 1 else parts[0][0]
    roads = None
    if want_roads:
        roads = np.zeros(grid.shape, dtype=bool)
        for (r, body, g) in grids:
            rc, rn = road_cells(body, g, r)
            c0 = int(round((g.x0 - grid.x0) / 0.25)); r0 = int(round((g.z0 - grid.z0) / 0.25))
            roads[r0:r0 + g.shape[0], c0:c0 + g.shape[1]] |= rc
    return grid, roads, [r.lvl_path for r, _, _ in grids], hasher.hexdigest()


def run_segment(grid, roads, params, out, scale) -> Segmentation:
    t0 = time.time()
    seg = segment(grid, params)
    render(seg, grid, out, scale=scale, roads=roads)
    print(f"{out}")
    report(seg, grid, time.time() - t0)
    return seg


def cmd_segment(wm, args):
    os.makedirs(OUT, exist_ok=True)
    for name in args.regions:
        grid, roads, _, _ = build_area(wm, [name], args.roads)
        run_segment(grid, roads, params_from(args, None), args.out or f"{OUT}/{name}_rooms.png", args.scale)


def cmd_area(wm, args):
    os.makedirs(OUT, exist_ok=True)
    key = args.key or args.location
    regs = [r for r in wm.by_location(args.location) if not r.underground or args.underground]
    names = [r.name for r in regs]
    if args.only:
        only = args.only.split(",")
        regs = [r for r in regs if r.name in only]
    print(f"{key}: {len(regs)} chunks {[r.name for r in regs]}")
    db = RoomsDb(DB)
    if args.set:
        stored = db.params(key) or {}
        for kv in args.set.split(","):
            k, v = kv.split("=")
            stored[k.strip()] = float(v)
        db.set_params(key, stored)
        print("  stored params:", stored)
    if args.name:
        db.set_name(key, args.name)
    params = params_from(args, db.params(key))
    print("  params:", asdict(params))
    grid, roads, chunks, signature = build_area(wm, regs, args.roads)
    seg = run_segment(grid, roads, params, args.out or f"{OUT}/{key}_rooms.png", args.scale)
    if args.write:
        counts = db.write_segmentation(key, args.name, regs[0].location if regs else key, chunks, params, signature, ALGO_VERSION, grid, seg)
        print(f"  written to {DB}: {counts}")


def cmd_status(wm, args):
    db = RoomsDb(DB)
    for row in db.status():
        print(f"{row['key']:36s} {row['name'] or '':24s} chunks {row['chunks']:2d} {'STALE ' if row['stale'] else ''}rooms {row['rooms']}")


def cmd_plan(wm, args):
    """Render the floor plan from the db's stored grid (what the mod sees)."""
    os.makedirs(OUT, exist_ok=True)
    db = RoomsDb(DB)
    g = db.grid(args.location)
    if not g:
        print("no grid stored for", args.location); return
    x0, z0, cell, labels, label_keys = g
    from gdmap.level import Grid
    from gdmap.render import render
    grid = Grid(x0, z0, labels >= 0, np.zeros(labels.shape, dtype=np.float32), 0)
    rooms = db.rooms(args.location)
    from gdmap.rooms import Room, Exit
    rs = [Room(i, 0, r["area"], (r["anchor_x"], r["anchor_z"]), (r["anchor_x"], r["anchor_z"]), 0, 0, r["cls"], r["walk"],
               tuple(json.loads(r["bbox"])), bool(r["island"])) for i, r in enumerate(rooms)]
    seg = Segmentation(labels.astype(np.int32), np.zeros(labels.shape, dtype=np.float32), rs, [], Params())
    out = args.out or f"{OUT}/{args.location}_db.png"
    render(seg, grid, out, scale=args.scale)
    print(out, len(rooms), "rooms")


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("regions"); s.add_argument("--grep", default=None); s.set_defaults(fn=cmd_regions)
    s = sub.add_parser("grid"); s.add_argument("regions", nargs="+"); s.set_defaults(fn=cmd_grid)
    s = sub.add_parser("segment"); s.add_argument("regions", nargs="+"); add_params(s); s.set_defaults(fn=cmd_segment)
    s = sub.add_parser("area"); s.add_argument("location"); s.add_argument("--underground", action="store_true")
    s.add_argument("--only", default=None, help="comma-separated chunk names to restrict the area to")
    s.add_argument("--key", default=None, help="db region key (default: the location); use with --only to store a dungeon separately")
    s.add_argument("--write", action="store_true"); s.add_argument("--name", default=None)
    s.add_argument("--set", default=None, help="store params: persist=1.0,max_walk=80")
    add_params(s); s.set_defaults(fn=cmd_area)
    s = sub.add_parser("status"); s.set_defaults(fn=cmd_status)
    s = sub.add_parser("plan"); s.add_argument("location"); s.add_argument("--scale", type=int, default=1)
    s.add_argument("--out", default=None); s.set_defaults(fn=cmd_plan)
    args = ap.parse_args()
    wm = WorldMap()
    args.fn(wm, args)


if __name__ == "__main__":
    main()
