"""Room segmentation over the game's baked navmesh (offline; never touches the running game), and the
rooms database the mod ships (assets/rooms.db). docs/rooms.md has the model and vocabulary.

  uv run tools/rooms.py regions [--grep devils]            list chunks (name, location, world offset)
  uv run tools/rooms.py grid 0A001 [0A002 ...]            extract + walkable grid stats + clearance PNG
  uv run tools/rooms.py segment 0A001 [...] [params]       one chunk, floor plan only (experiments)
  uv run tools/rooms.py area devilscrossing [params] [--write] [--name "Devil's Crossing"] [--set persist=1.0,...]
                                  all overworld chunks of a region (world-map location), stitched;
                                  --write stores grid/rooms/exits in the db, --set stores params
  uv run tools/rooms.py exits [devilscrossing] [--write]  recompute intra-region exits from the stored grid
                                  (heal tile-seam erosion, one exit per pair); no re-segmentation. Run after
                                  area --write, before seams --write. Default: all regions.
  uv run tools/rooms.py seams [--write]                    cross-region exits over region boundaries
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
import struct
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


UG_STEM_RE = re.compile(r"_?[A-Z]?\d+$")   # UG_CaveBurial_A01 -> UG_CaveBurial; Void_SecretA06 -> Void_Secret


def chunk_stem(r):
    """The game's own sub-level grouping for an underground chunk (its .lvl name minus the _A0N section
    suffix), or None for a surface chunk (surface chunk names are unique, grouped by XZ instead)."""
    if not r.underground:
        return None
    base = re.split(r"[\\/]", r.lvl_path)[-1].replace("Region", "").replace(".lvl", "")
    return UG_STEM_RE.sub("", base)


def cluster_chunks(regs, step=200):
    """Partition a location record's chunks into physical places. GD lays each dungeon out in its own far-off
    patch of the shared world XZ frame (not stacked in Y), so a location record can hold the overworld plus
    several disjoint interiors. Segmenting them together allocates a label grid over their whole bounding box
    -- mostly empty, and it blew to 20 GB on the necropolis. So: connected components by Chebyshev XZ distance
    <= step (one 128-unit chunk stays joined; a >1-chunk gap breaks), THEN merge any components that share an
    underground .lvl stem -- that re-joins a dungeon split across a coordinate gap (the burial cave's two
    chunks are a vertical line 256 units apart; XZ alone splits them, the stem UG_CaveBurial rejoins them).
    Verified to reproduce the authored regions: DC surface (9 chunks + inert no-nav 0W002), LC surface (exact
    10), burial cave (exact 2). No dungeon stem spans a big enough box to re-blow up (necropolis's largest is
    UG_Crypt_Final, 1248x384). Returns clusters (lists of Region), largest first."""
    parent = list(range(len(regs)))

    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]; a = parent[a]
        return a

    def union(a, b):
        ra, rb = find(a), find(b)
        if ra != rb:
            parent[ra] = rb

    for i in range(len(regs)):
        for j in range(i + 1, len(regs)):
            if (abs(regs[i].world_offset[0] - regs[j].world_offset[0]) <= step
                    and abs(regs[i].world_offset[2] - regs[j].world_offset[2]) <= step):
                union(i, j)
    stem_rep = {}
    for i, r in enumerate(regs):
        s = chunk_stem(r)
        if s is None:
            continue
        if s in stem_rep:
            union(i, stem_rep[s])
        else:
            stem_rep[s] = i
    comps = {}
    for i, r in enumerate(regs):
        comps.setdefault(find(i), []).append(r)
    return sorted(comps.values(), key=lambda g: (-len(g), g[0].name))


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
    from gdmap.rooms import bridge_walk_seams
    print(f"  seam-stitched {bridge_walk_seams(grid)} walkable cells (runtime-navmesh connectivity)")
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
        from gdmap.rooms import resolve_overlays
        overlays = resolve_overlays(grid, seg.labels)
        if overlays:
            orphan = sum(1 for _, _, _, lab in overlays if lab < 0)
            print(f"  overlay cells: {len(overlays)} ({len(overlays) * 0.0625:.0f} m2), {orphan} unlabeled")
        counts = db.write_segmentation(key, args.name, regs[0].location if regs else key, chunks, params, signature, ALGO_VERSION, grid, seg, overlays)
        print(f"  written to {DB}: {counts}")


def cmd_exits(wm, args):
    """Recompute a region's intra-region exits from its STORED label grid -- no re-segmentation, so authored
    room titles/anchors are untouched (re-segmenting the bridge-corrected grid orphaned 36% of DC's authored
    rooms; this pass touches only the exits table). It heals the tile-cache's seam erosion in a working copy
    (fill_label_seams: the runtime navmesh stitches internal tile seams the bake severs, so islanded rooms
    like the riftgate circle get their opening back) and emits one exit per room pair (the widest run), fixing
    the "no exit" and "several exits to the same room" complaints. Cross-region (foreign) rows are preserved;
    re-run `seams` after this if a region boundary changed. Re-run after any `area --write`."""
    from gdmap.level import Grid
    from gdmap.rooms import exits_of, fill_label_seams
    from gdmap.roomsdb import rle_decode
    db = RoomsDb(DB)
    keys = [args.region] if args.region else [r[0] for r in db.c.execute("SELECT region_key FROM grids ORDER BY region_key")]
    for rk in keys:
        row = db.c.execute("SELECT x0, z0, w, h, labels, heights, label_keys FROM grids WHERE region_key=?", (rk,)).fetchone()
        if not row:
            print(f"{rk}: no grid"); continue
        x0, z0, w, h, lab_blob, h_blob, keys_json = row
        labels = rle_decode(lab_blob, h, w).astype(np.int32)
        heights = rle_decode(h_blob, h, w) if h_blob else None
        label_keys = json.loads(keys_json)
        filled = fill_label_seams(labels, heights, x0, z0)
        grid = Grid(x0, z0, labels >= 0, np.zeros(labels.shape, dtype=np.float32), 0)
        exits = exits_of(labels, grid)
        n = int(labels.max()) + 1

        def key_of(i):
            return label_keys[i] if 0 <= i < len(label_keys) else ""
        rows = [(rk, key_of(e.a), key_of(e.b), e.x, e.z, e.width, int(e.cut)) for e in exits
                if key_of(e.a) and key_of(e.b)]
        old = db.c.execute("SELECT COUNT(*) FROM exits WHERE region_key=? AND room_b LIKE ?", (rk, rk + ":%")).fetchone()[0]
        print(f"{rk}: healed {filled} seam cells; {old} intra exits -> {len(rows)} (foreign rows kept)")
        if args.write:
            db.c.execute("DELETE FROM exits WHERE region_key=? AND room_b LIKE ?", (rk, rk + ":%"))
            db.c.executemany("INSERT INTO exits(region_key, room_a, room_b, x, z, width, cut) VALUES(?,?,?,?,?,?,?)", rows)
            db.c.commit()
    if args.write:
        print("written. Re-run `seams --write` if any region boundary changed.")


def cmd_seams(wm, args):
    """Cross-region exits. A per-region watershed sees its grid edge as a wall, so an opening at a region
    boundary (the Devil's Crossing -> Lower Crossing road at z=-256, found live 2026-08-23) never becomes an
    exit. This pass scans every region pair for adjacent walkable cells across the seam (heights within 1
    unit when known), clusters them into openings and writes exit rows into BOTH regions with the far side's
    room key. Idempotent; re-run after ANY `area --write` (which deletes its region's exit rows)."""
    import collections
    db = RoomsDb(DB)
    regions = {}
    for rk, x0, z0, w, h, lab, hts, keys in db.c.execute(
            "SELECT region_key, x0, z0, w, h, labels, heights, label_keys FROM grids"):
        from gdmap.roomsdb import rle_decode
        labels = rle_decode(lab, h, w)
        heights = rle_decode(hts, h, w) if hts else None
        regions[rk] = (x0, z0, labels, heights, json.loads(keys))
    total = 0
    for a, b in [(a, b) for i, a in enumerate(sorted(regions)) for b in sorted(regions)[i + 1:]]:
        ax0, az0, alab, ah, akeys = regions[a]
        bx0, bz0, blab, bh, bkeys = regions[b]
        awalk, bwalk = alab >= 0, blab >= 0
        pairs = []   # (labelA, labelB, seam_x, seam_z)
        # integer cell offset of B's frame relative to A's (grids are chunk-aligned, cell 0.25)
        dc0 = int(round((bx0 - ax0) / 0.25)); dr0 = int(round((bz0 - az0) / 0.25))
        for dr, dc in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            ars, acs = np.nonzero(awalk)
            nr, nc = ars + dr, acs + dc
            inside_a = (nr >= 0) & (nr < awalk.shape[0]) & (nc >= 0) & (nc < awalk.shape[1])
            open_in_a = np.zeros(len(ars), dtype=bool)
            open_in_a[inside_a] = awalk[nr[inside_a], nc[inside_a]]
            br, bc = nr - dr0, nc - dc0
            in_b = (br >= 0) & (br < bwalk.shape[0]) & (bc >= 0) & (bc < bwalk.shape[1])
            hit = ~open_in_a & in_b
            hit[hit] &= bwalk[br[hit], bc[hit]]
            if ah is not None and bh is not None:
                ok = np.abs(ah[ars[hit], acs[hit]].astype(np.int32) - bh[br[hit], bc[hit]].astype(np.int32)) <= 10
                idx = np.nonzero(hit)[0]
                hit[idx[~ok]] = False
            for k in np.nonzero(hit)[0]:
                la = int(alab[ars[k], acs[k]]); lb = int(blab[br[k], bc[k]])
                sx = ax0 + (acs[k] + 0.5 + dc * 0.5) * 0.25
                sz = az0 + (ars[k] + 0.5 + dr * 0.5) * 0.25
                pairs.append((la, lb, sx, sz))
        # cluster per (room pair) by proximity along the seam
        by_rooms = collections.defaultdict(list)
        for la, lb, sx, sz in pairs:
            by_rooms[(la, lb)].append((sx, sz))
        rows = []
        for (la, lb), pts in by_rooms.items():
            pts.sort()
            clusters = []
            for p in pts:
                if clusters and min(abs(p[0] - q[0]) + abs(p[1] - q[1]) for q in clusters[-1][-6:]) <= 0.55:
                    clusters[-1].append(p)
                else:
                    clusters.append([p])
            for cl in clusters:
                if len(cl) < 3:   # under 0.75 units of opening: bake noise, not a passage
                    continue
                xs = [p[0] for p in cl]; zs = [p[1] for p in cl]
                width = max(max(xs) - min(xs), max(zs) - min(zs)) + 0.25
                rows.append((sum(xs) / len(xs), sum(zs) / len(zs), width, akeys[la], bkeys[lb]))
        if rows:
            print(f"{a} <-> {b}: {len(rows)} cross-region exits")
            for x, z, width, ka, kb in rows:
                print(f"   ({x:7.1f}, {z:7.1f}) width {width:4.1f}  {ka} <-> {kb}")
        total += len(rows)
        if args.write:
            for rk, other in ((a, b), (b, a)):
                db.c.execute("DELETE FROM exits WHERE region_key=? AND room_b LIKE ?", (rk, other + ":%"))
            for x, z, width, ka, kb in rows:
                db.c.execute("INSERT INTO exits(region_key, room_a, room_b, x, z, width, cut) VALUES(?,?,?,?,?,?,0)",
                             (a, ka, kb, x, z, width))
                db.c.execute("INSERT INTO exits(region_key, room_a, room_b, x, z, width, cut) VALUES(?,?,?,?,?,?,0)",
                             (b, kb, ka, x, z, width))
    if args.write:
        db.c.commit()
        print(f"written: {total} seams into both sides' exit rows")


def prettify_stem(stem):
    """A dungeon .lvl stem -> a display name: drop the UG_ prefix, split camelCase and underscores, title-case
    (UG_CaveBurial -> 'Cave Burial', UG_DCPrisonCellar -> 'DC Prison Cellar', UG_Crypt_Final -> 'Crypt Final')."""
    s = stem.removeprefix("UG_").replace("_", " ")
    s = re.sub(r"(?<=[a-z])(?=[A-Z])", " ", s)          # caveBurial -> cave Burial
    s = re.sub(r"(?<=[A-Z])(?=[A-Z][a-z])", " ", s)     # DCPrison -> DC Prison
    return " ".join(w if w.isupper() else w.capitalize() for w in s.split())


def zone_names():
    """{location basename -> the game's zone name} for the riftgate map locations, read offline: each location
    .dbr's ZoneNameTag resolved through Text_EN.arc, minus the trailing ' Rift'. Empty on any failure (the
    driver then title-cases the basename)."""
    out = {}
    try:
        import lz4.block
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import arz
        d, strings, recs = arz.load()
        loc_tag = {}
        for path, rname, off, csz, dsz in recs:
            if "/riftgatemap/locations/" in path.lower() and path.lower().endswith(".dbr"):
                rec = arz.decode(d, strings, off, csz, dsz)
                t = rec.get("ZoneNameTag") or rec.get("TeleportNameTag")
                if t:
                    loc_tag[path.rsplit("/", 1)[-1][:-4]] = t[0]
        P = r"C:\Program Files (x86)\Steam\steamapps\common\Grim Dawn\resources\Text_EN.arc"
        dd = open(P, "rb").read()
        _, _, numE, numP, recSize, strSize, recOff = struct.unpack_from("<IIIIIII", dd, 0)
        parts = [struct.unpack_from("<III", dd, recOff + 12 * i) for i in range(numP)]
        strs = dd[recOff + recSize:recOff + recSize + strSize]; ent = recOff + recSize + strSize
        tag = {}
        for i in range(numE):
            f = struct.unpack_from("<11I", dd, ent + 44 * i)
            np_, pi = f[7], f[8]
            blob = b""
            for j in range(pi, pi + np_):
                o, c, u = parts[j]; ch = dd[o:o + c]
                blob += ch if c == u else lz4.block.decompress(ch, uncompressed_size=u)
            for line in blob.decode("utf-8-sig", errors="replace").splitlines():
                if "=" in line:
                    k, _, v = line.partition("="); tag[k.strip()] = v.strip()
        for base, t in loc_tag.items():
            name = tag.get(t, "")
            if name:
                out[base] = re.sub(r"\s+Rift$", "", name)
    except Exception as e:                                       # noqa: BLE001 -- names are best-effort
        print(f"  (zone name resolve failed: {type(e).__name__}: {e}; falling back to title-cased basenames)")
    return out


def cmd_build(wm, args):
    """Tier 1: segment every physical cluster of a location record (or --all) into the db so the mod announces
    "<zone>, room N" everywhere -- no authoring. Names: the game's own zone name for a surface cluster (read
    offline), the prettified .lvl stem for a dungeon cluster. Clusters that match an already-authored db region
    are skipped untouched. Preview by default; --write commits. Re-run `seams --write` afterwards."""
    db = RoomsDb(DB)
    stored = {}
    for rk, chunks in db.c.execute("SELECT region_key, (SELECT chunks FROM regions WHERE key=grids.region_key) FROM grids"):
        if chunks:
            stored[rk] = frozenset(json.loads(chunks))
    zn = zone_names()
    base_of = lambda loc: loc.split("_", 1)[-1] if "_" in loc else loc
    if args.all:
        seen, locs = set(), []
        for r in wm.regions:
            if r.location and r.location not in seen:
                seen.add(r.location); locs.append(r.location)
    else:
        locs = [args.location]
    used_keys = set(stored)
    wrote = skipped = failed = total_rooms = 0
    for loc in locs:
        regs = wm.by_location(loc)
        if not regs:
            continue
        base = base_of(loc)
        clusters = cluster_chunks(regs, args.step)
        # The zone name goes to the PRIMARY cluster (the largest surface one, else the largest overall -- a
        # dungeon-only location like undergroundtransit still has a real zone name); the other clusters are
        # separate interiors named from their .lvl stem.
        primary = next((c for c in clusters if not all(r.underground for r in c)), clusters[0] if clusters else None)
        for cl in clusters:
            paths = frozenset(r.lvl_path for r in cl)
            if any(s and s <= paths for s in stored.values()):    # matches an authored region -> leave it be
                skipped += 1
                continue
            if cl is primary:
                key = base
                name = zn.get(loc) or base.replace("_", " ").title()
            else:
                stem = chunk_stem(cl[0]) or base
                key = f"{base}_{re.sub(r'[^a-z0-9]', '', stem.lower())}"
                name = prettify_stem(stem)
            k, n = key, 2
            while k in used_keys:
                k = f"{key}_{n}"; n += 1
            key = k; used_keys.add(key)
            try:
                grid, roads, chunks, signature = build_area(wm, cl, False)
                params = params_from(args, db.params(key))
                seg = segment(grid, params)
                rooms_n = len(seg.rooms); total_rooms += rooms_n
                print(f"  {key:34s} {name:24s} {len(cl):2d} chunks -> {rooms_n} rooms" + ("" if args.write else "  (preview)"))
                if args.write:
                    from gdmap.rooms import resolve_overlays
                    overlays = resolve_overlays(grid, seg.labels)
                    db.write_segmentation(key, name, loc, chunks, params, signature, ALGO_VERSION, grid, seg, overlays)
                wrote += 1
            except Exception as e:                                # noqa: BLE001 -- one bad cluster must not abort the run
                failed += 1
                print(f"  {key:34s} {name:24s} FAILED: {type(e).__name__}: {e}")
    print(f"\n{'WROTE' if args.write else 'PREVIEW'}: {wrote} clusters, {total_rooms} rooms; {skipped} authored skipped; {failed} failed")
    if args.write:
        print("Now run: uv run tools/rooms.py seams --write")


def cmd_clusters(wm, args):
    """Preview the physical-place partition of a location record (or --all location records). With --segment,
    segment each cluster offline (no write) and report room counts + a grand total -- the dry-run that sizes a
    full authoring pass. Clusters whose chunk set already matches an authored db region are flagged [authored]
    and (with --segment) skipped, so the enumeration never re-does or disturbs DC/LC/the cave."""
    import json as _json
    db = RoomsDb(DB)
    stored = {}   # region_key -> frozenset(lvl_path)
    for rk, chunks in db.c.execute("SELECT region_key, "
                                   "(SELECT chunks FROM regions WHERE key=grids.region_key) FROM grids"):
        if chunks:
            stored[rk] = frozenset(_json.loads(chunks))
    if args.all:
        seen, locs = set(), []
        for r in wm.regions:
            if r.location and r.location not in seen:
                seen.add(r.location); locs.append(r.location)
    else:
        locs = [args.location]
    grand_rooms = grand_clusters = grand_authored = 0
    for loc in locs:
        regs = wm.by_location(loc)
        if not regs:
            print(f"{loc}: no chunks"); continue
        base = loc.split("_", 1)[-1] if "_" in loc else loc
        clusters = cluster_chunks(regs, args.step)
        print(f"=== {loc}: {len(regs)} chunks -> {len(clusters)} clusters ===")
        for ci, cl in enumerate(clusters):
            xs = [r.world_offset[0] for r in cl]; zs = [r.world_offset[2] for r in cl]
            span = (max(xs) - min(xs) + 128, max(zs) - min(zs) + 128)
            ug = sum(1 for r in cl if r.underground)
            paths = frozenset(r.lvl_path for r in cl)
            # authored if a stored region's chunks are all present here (a cluster may add only inert no-nav
            # chunks, e.g. DC's 0W002 -- those drop out in build_area, so the grid and anchors are unchanged)
            authored = max((rk for rk, s in stored.items() if s and s <= paths),
                           key=lambda rk: len(stored[rk]), default=None)
            grand_clusters += 1
            extra = sorted(p.split("/")[-1].replace("Region", "").replace(".lvl", "")
                           for p in (paths - stored[authored])) if authored else []
            tag = (f" [authored: {authored}" + (f", +{extra}" if extra else "") + "]") if authored else ""
            kind = "underground" if ug == len(cl) else ("mixed" if ug else "surface")
            head = f"  #{ci} {len(cl):3d} chunks {kind:11s} span={span[0]}x{span[1]}{tag}"
            if args.segment and not authored:
                try:
                    grid, roads, chunks, _ = build_area(wm, cl, False)
                    t0 = time.time()
                    seg = segment(grid, params_from(args, db.params(base)))
                    n = len(seg.rooms)
                    grand_rooms += n
                    print(f"{head} -> {n} rooms ({time.time()-t0:.1f}s, {int(grid.walk.sum())*0.0625:.0f} m2)")
                except Exception as e:                                   # noqa: BLE001 -- dry run must not abort
                    print(f"{head} -> FAILED: {type(e).__name__}: {e}")
            else:
                if authored:
                    grand_authored += 1
                print(f"{head}{' (segment skipped: authored)' if authored and args.segment else ''}"
                      + ("" if args.segment else f"  chunks={sorted(r.name for r in cl)}"))
    print(f"\nTOTAL: {grand_clusters} clusters ({grand_authored} authored/skipped)"
          + (f", {grand_rooms} rooms in the un-authored clusters" if args.segment else ""))


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
    s = sub.add_parser("exits", help="recompute a region's intra exits from the stored grid (heal seams, one per pair); re-run after area --write")
    s.add_argument("region", nargs="?", default=None, help="region key (default: all regions)")
    s.add_argument("--write", action="store_true"); s.set_defaults(fn=cmd_exits)
    s = sub.add_parser("seams", help="cross-region exits over all region pairs; re-run after any area --write")
    s.add_argument("--write", action="store_true"); s.set_defaults(fn=cmd_seams)
    s = sub.add_parser("clusters", help="partition a location record into physical places (XZ components + UG stem merge); --segment for a dry-run room count")
    s.add_argument("location", nargs="?", default=None); s.add_argument("--all", action="store_true")
    s.add_argument("--step", type=int, default=200, help="Chebyshev XZ chunk-gap threshold (units); 128 = one chunk")
    s.add_argument("--segment", action="store_true", help="segment each cluster (no write) and count rooms")
    add_params(s); s.set_defaults(fn=cmd_clusters)
    s = sub.add_parser("build", help="Tier 1: segment every cluster of a location (or --all) into the db with game zone names; skips authored regions")
    s.add_argument("location", nargs="?", default=None); s.add_argument("--all", action="store_true")
    s.add_argument("--step", type=int, default=200); s.add_argument("--write", action="store_true")
    add_params(s); s.set_defaults(fn=cmd_build)
    s = sub.add_parser("status"); s.set_defaults(fn=cmd_status)
    s = sub.add_parser("plan"); s.add_argument("location"); s.add_argument("--scale", type=int, default=1)
    s.add_argument("--out", default=None); s.set_defaults(fn=cmd_plan)
    args = ap.parse_args()
    wm = WorldMap()
    args.fn(wm, args)


if __name__ == "__main__":
    main()
