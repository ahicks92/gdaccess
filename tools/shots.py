"""Authoring screenshots of rooms (docs/rooms.md M4). Drives the DEV game instance: for each room, teleports
the character to a few well-spread points inside it, reveals the fog there, screenshots, and draws the room's
outline and exits onto a copy using the game's own projection (/project). Facts for the describer (nearby
labelled entities, the sample points) go to meta.json next to the shots, and the db's shots table + the
room's status are updated.

  uv run tools/shots.py room devilscrossing:58:88 [--samples 3] [--no-fog]
  uv run tools/shots.py region devilscrossing [--status unseen] [--limit N]
Output: build/shots/<region>/<room key with ':' -> '_'>/NN.png, NN_overlay.png, meta.json"""
from __future__ import annotations

import argparse
import functools
import json
import os
import re
import subprocess
import sys
import time
import urllib.parse
import urllib.request

import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gdmap.roomsdb import RoomsDb  # noqa: E402

PORT = 8791
DB = "assets/rooms.db"
OUT = "build/shots"
CELL = 0.25

print = functools.partial(print, flush=True)   # the run is watched through a redirected log


def get(path, **q):
    url = f"http://127.0.0.1:{PORT}{path}" + ("?" + urllib.parse.urlencode(q) if q else "")
    return urllib.request.urlopen(url, timeout=30).read().decode("utf-8", "replace")


def close_game_windows():
    """An open InGameUI window (character sheet, bag) drawn over a shot ruins it (the first 15 Lower
    Crossing shots, 2026-08-23). Escape closes the top window through the mod's screen; the PromptBox
    container reads visible even when empty, so it is ignored."""
    wins = []
    for _ in range(4):
        # only the top-level window rows ("  Inventory  +0xbbf0 vt=exe+0x31cc98 visible=1"); the dump's
        # widget detail lines (the exit window's buttons etc.) also say visible= and must not count
        wins = [m.group(1) for ln in get("/ingame").splitlines()
                if (m := re.match(r"^  (\w+) +\+0x[0-9a-f]+ vt=exe\+0x[0-9a-f]+ visible=1$", ln))
                and m.group(1) != "PromptBox"]
        if not wins:
            return
        print("closing open window:", wins)
        get("/key", name="escape")
        time.sleep(0.5)
    raise SystemExit(f"game windows still open after 4 Escapes: {wins}")


def player_xz() -> tuple[float, float]:
    m = re.search(r"player world=\(([-\d.]+), [-\d.]+, ([-\d.]+)\)", get("/player"))
    return (float(m.group(1)), float(m.group(2))) if m else (0.0, 0.0)


STONES: np.ndarray | None = None   # (N, 2) world xz, one walkable cell per ~10 units: teleport stepping stones


def build_stones(grid) -> np.ndarray:
    """Downsample the region's walkable cells to a ~10-unit lattice. Teleport hops need a walkable LANDING
    within streaming range, not walkable continuity, so a dense lattice bridges moats that the sparse room
    anchors cannot (the east cliffs of Lower Crossing sit 170 anchor-units from the mainland, 2026-08-23)."""
    x0, z0, cell, labels, label_keys = grid
    step = max(1, int(10.0 / cell))
    walk = labels >= 0
    out = []
    for r in range(0, walk.shape[0], step):
        for c in range(0, walk.shape[1], step):
            ys, xs = np.nonzero(walk[r:r + step, c:c + step])
            if len(ys):
                out.append((x0 + (c + xs[0] + 0.5) * cell, z0 + (r + ys[0] + 0.5) * cell))
    return np.array(out) if out else np.empty((0, 2))


def stone_path(start, goal, hop: float = 50.0) -> list:
    """Fewest-hops BFS over the stones with edges <= hop, from the stone nearest `start` to the one nearest
    `goal`. Returns [] when a gap wider than `hop` separates them (then no teleport route exists at all)."""
    import collections
    if STONES is None or not len(STONES):
        return []
    d_start = ((STONES - start) ** 2).sum(axis=1)
    d_goal = ((STONES - goal) ** 2).sum(axis=1)
    s_i, g_i = int(d_start.argmin()), int(d_goal.argmin())
    prev = {s_i: None}
    q = collections.deque([s_i])
    while q:
        i = q.popleft()
        if i == g_i:
            break
        near = np.nonzero(((STONES - STONES[i]) ** 2).sum(axis=1) <= hop * hop)[0]
        for j in near:
            j = int(j)
            if j not in prev:
                prev[j] = i; q.append(j)
    if g_i not in prev:
        return []
    out = []
    i = g_i
    while i is not None:
        out.append(tuple(STONES[i])); i = prev[i]
    return out[::-1]


def teleport(x: float, z: float, hop: float = 60.0, wait_s: float = 25.0) -> str:
    """Far targets crash the game if their chunk is not streamed in: hop toward them until close, THEN wait.
    Chunks stream around the PLAYER, so waiting without approaching never loads a chunk 60+ units away
    (the stuck-tour wedge, 2026-08-23: the whole west of Lower Crossing skipped from one cliff)."""
    px, pz = player_xz()
    while True:
        dx, dz = x - px, z - pz
        dist = (dx * dx + dz * dz) ** 0.5
        if dist <= 25.0 or ("loaded=true" in get("/teleport", x=f"{x:.2f}", z=f"{z:.2f}", check=1) and dist <= hop * 1.5):
            break
        step = min(hop, max(10.0, dist - 15.0))
        hx, hz = px + dx / dist * step, pz + dz / dist * step
        # hop to the nearest walkable point along the way: try the hop point, then fall back shorter
        moved = False
        for frac in (1.0, 0.7, 0.4):
            r = get("/teleport", x=f"{px + (hx - px) * frac:.2f}", z=f"{pz + (hz - pz) * frac:.2f}")
            if r.startswith("teleported"):
                moved = True
                break
            time.sleep(0.5)
        if not moved:
            # the straight line is off the mesh (water, cliffs): walk the stepping-stone lattice instead
            path = stone_path((px, pz), (x, z), hop)
            for wx, wz in path:
                t0 = time.time()
                while "loaded=true" not in (chk := get("/teleport", x=f"{wx:.2f}", z=f"{wz:.2f}", check=1)):
                    if time.time() - t0 > wait_s:
                        return f"waypoint chunk for ({wx:.1f}, {wz:.1f}) never loaded (last check: {chk.strip()})"
                    time.sleep(1.0)
                r = get("/teleport", x=f"{wx:.2f}", z=f"{wz:.2f}")
                if not r.startswith("teleported"):
                    continue   # a bake-only anchor the live mesh refuses: try the next waypoint
                moved = True
                time.sleep(1.0)
        if not moved:
            # no route on the ground (a void between dungeon levels): the check below force-loads the
            # target chunk (Region::BackgroundLoadLevel in the route), after which a direct jump is safe
            break
        time.sleep(1.0)
        px, pz = player_xz()
    t0 = time.time()
    while "loaded=true" not in (chk := get("/teleport", x=f"{x:.2f}", z=f"{z:.2f}", check=1)):
        if time.time() - t0 > wait_s:
            return f"chunk for ({x:.1f}, {z:.1f}) never loaded (last check: {chk.strip()})"
        time.sleep(1.0)
    return get("/teleport", x=f"{x:.2f}", z=f"{z:.2f}").strip()


def shot(path: str) -> None:
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import gd
    gd.capture(path)


def farthest_points(cells_xz: np.ndarray, want: int, min_sep: float = 6.0) -> list[tuple[float, float]]:
    """wotr's Rooms(): centroid-seeded farthest-point sampling; stop early when nothing is >= min_sep away."""
    if len(cells_xz) == 0:
        return []
    c = cells_xz.mean(axis=0)
    first = cells_xz[np.argmin(((cells_xz - c) ** 2).sum(axis=1))]
    picked = [first]
    d = np.sqrt(((cells_xz - first) ** 2).sum(axis=1))
    while len(picked) < want:
        i = int(np.argmax(d))
        if d[i] < min_sep:
            break
        picked.append(cells_xz[i])
        d = np.minimum(d, np.sqrt(((cells_xz - cells_xz[i]) ** 2).sum(axis=1)))
    return [(float(p[0]), float(p[1])) for p in picked]


def project(points: list[tuple[float, float]]) -> list[tuple[float, float, float, float, bool]]:
    out = []
    for i in range(0, len(points), 120):
        chunk = points[i:i + 120]
        txt = get("/project", pts=";".join(f"{x:.2f},{z:.2f}" for x, z in chunk))
        for line in txt.splitlines():
            m = re.match(r"([-\d.]+),([-\d.]+) -> ([-\d.]+),([-\d.]+) (visible|off)", line)
            if m:
                out.append((float(m.group(1)), float(m.group(2)), float(m.group(3)), float(m.group(4)), m.group(5) == "visible"))
    return out


ENTITY_RE = re.compile(r"\s*([\d.]+)\s+id=(\d+)\s+(\S+)\s+label='([^']*)' at \(([-\d.]+), ([-\d.]+), ([-\d.]+)\).*'([^']*)'\s*$")
STAGE_CLASSES = {"Decoration", "Decal", "FixedActor", "Item", "Npc", "Door", "Chest", "Shrine", "Riftgate", "StaticTeleporter", "Destructible"}


def parse_entities(txt: str) -> list[dict]:
    """/entities lines: 'dist id=N Class label='...' at (x, y, z) ptr 'record''. Keep labelled things and the
    permanent stage (decorations, decals, fixed actors) by record name; skip engine helpers and units."""
    ents = []
    for line in txt.splitlines():
        m = ENTITY_RE.match(line)
        if not m:
            continue
        cls, label, record = m.group(3), m.group(4), m.group(8)
        if not label and cls not in STAGE_CLASSES:
            continue
        ents.append({"dist": float(m.group(1)), "id": int(m.group(2)), "cls": cls, "label": label,
                     "x": float(m.group(5)), "z": float(m.group(7)), "record": record.rsplit("/", 1)[-1].removesuffix(".dbr")})
    return ents


_WM = None
_CHUNK_CACHE: dict[str, tuple] = {}   # chunk name -> (region, grid, [(record, cells)])


def chunk_terrain(wm, name: str):
    """Per-process cache: the chunk's grid and each painted layer's cells (the map parse is the slow part)."""
    from gdmap.level import mask_to_cells, parse_terrain_layers, parse_tiles, walk_grid
    if name not in _CHUNK_CACHE:
        r = wm.by_name[name]
        body = wm.level_body(r)
        g = walk_grid(parse_tiles(body))
        ox, oz = float(r.world_offset[0]), float(r.world_offset[2])
        layers = [(l.record, mask_to_cells(l.mask, g, ox, oz)) for l in parse_terrain_layers(body) if l.mask is not None]
        _CHUNK_CACHE[name] = (r, g, layers)
    return _CHUNK_CACHE[name]


def terrain_facts(region_key: str, room: dict, grid) -> dict:
    """Fraction of the room's cells under each painted terrain layer (the describer's 'muddy', 'cobbled')."""
    global _WM
    from gdmap.mapfile import WorldMap
    x0, z0, cell, labels, label_keys = grid
    label = label_keys.index(room["key"])
    mask = labels == label
    n = int(mask.sum())
    if n == 0:
        return {}
    if _WM is None:
        _WM = WorldMap()
    wm = _WM
    chunks = json.loads(RoomsDb(DB).c.execute("SELECT chunks FROM regions WHERE key=?", (region_key,)).fetchone()[0])
    bx0, bz0, bx1, bz1 = json.loads(room["bbox"])
    out = {}
    for lvl in chunks:
        name = lvl.rsplit("/", 1)[-1].replace("\\", "/").removeprefix("Region").removesuffix(".lvl")
        r = wm.by_name[name]
        ox, oz = r.world_offset[0], r.world_offset[2]
        if bx1 < ox or bx0 > ox + 128 or bz1 < oz or bz0 > oz + 128:
            continue
        _, g, layers = chunk_terrain(wm, name)
        for record, cells in layers:
            # place the chunk grid's cells into the region grid frame
            c0 = int(round((g.x0 - x0) / cell)); r0 = int(round((g.z0 - z0) / cell))
            sub = np.zeros_like(mask)
            h, w = cells.shape
            rs, cs = slice(max(r0, 0), min(r0 + h, mask.shape[0])), slice(max(c0, 0), min(c0 + w, mask.shape[1]))
            if rs.stop > rs.start and cs.stop > cs.start:
                sub[rs, cs] = cells[rs.start - r0: rs.stop - r0, cs.start - c0: cs.stop - c0]
            k = int((sub & mask).sum())
            if k:
                lname = record.rsplit("/", 1)[-1].removesuffix(".dbr")
                out[lname] = round(out.get(lname, 0) + k / n, 3)
    return dict(sorted(out.items(), key=lambda kv: -kv[1]))


def shoot_room(db: RoomsDb, region_key: str, room: dict, grid, samples: int, fog: bool) -> dict:
    x0, z0, cell, labels, label_keys = grid
    label = label_keys.index(room["key"])
    ys, xs = np.nonzero(labels == label)
    cells = np.stack([x0 + (xs + 0.5) * cell, z0 + (ys + 0.5) * cell], axis=1)
    want = max(1, min(samples, 1 + int(room["area"] / 200)))
    pts = farthest_points(cells, want)
    # outline: boundary cells (a 4-neighbour with another label or unwalkable), subsampled every ~2 units
    mask = labels == label
    pad = np.pad(mask, 1)
    inner = pad[1:-1, 1:-1] & pad[:-2, 1:-1] & pad[2:, 1:-1] & pad[1:-1, :-2] & pad[1:-1, 2:]
    bys, bxs = np.nonzero(mask & ~inner)
    step = max(1, len(bys) // 240)
    outline = [(x0 + (bx + 0.5) * cell, z0 + (by + 0.5) * cell) for by, bx in list(zip(bys, bxs))[::step]]
    exits = [dict(zip(("room_a", "room_b", "x", "z", "width", "cut"), row)) for row in
             db.c.execute("SELECT room_a, room_b, x, z, width, cut FROM exits WHERE region_key=? AND (room_a=? OR room_b=?)",
                          (region_key, room["key"], room["key"]))]
    folder = os.path.join(OUT, region_key, room["key"].replace(":", "_"))
    os.makedirs(folder, exist_ok=True)
    meta = {"room": room, "samples": [], "exits": exits, "outline_points": len(outline),
            "terrain": terrain_facts(region_key, room, grid)}
    t_room, t_tele = time.time(), 0.0
    for i, (sx, sz) in enumerate(pts):
        t0 = time.time()
        tele = teleport(sx, sz)
        if not tele.startswith("teleported") and "no navmesh floor" in tele and i == 0:
            # the sample sits on the bake but the live mesh refuses it (bake wider than runtime, an obstacle):
            # the anchor is the room's clearance maximum, the safest point in it
            sx, sz = room["anchor_x"], room["anchor_z"]
            tele = teleport(sx, sz)
        t_tele += time.time() - t0
        if not tele.startswith("teleported"):
            print(f"  {room['key']} sample {i}: {tele}; skipped")
            continue
        if fog:
            get("/fog", x=f"{sx:.2f}", z=f"{sz:.2f}", radius=40)
        time.sleep(1.6)     # let the chunk stream and the camera settle
        raw = os.path.join(folder, f"{i:02d}.png")
        shot(raw)
        # a black frame is the loading fade after a big hop (found sampling the first runs, 2026-08-23)
        for _ in range(6):
            if np.asarray(Image.open(raw).convert("L")).mean() >= 8.0:
                break
            time.sleep(1.5)
            shot(raw)
        else:
            print(f"  {room['key']} sample {i}: still black after retries")
        proj = project(outline)
        pexits = project([(e["x"], e["z"]) for e in exits])
        pself = project([(sx, sz)])
        ents = parse_entities(get("/entities"))
        im = Image.open(raw).convert("RGB")
        d = ImageDraw.Draw(im)
        for (_, _, px, py, vis) in proj:
            if vis:
                d.ellipse([px - 3, py - 3, px + 3, py + 3], fill=(255, 255, 0), outline=(0, 0, 0))
        for e, (_, _, px, py, vis) in zip(exits, pexits):
            if vis:
                other = e["room_b"] if e["room_a"] == room["key"] else e["room_a"]
                d.ellipse([px - 9, py - 9, px + 9, py + 9], fill=(255, 60, 60), outline=(0, 0, 0), width=2)
                d.text((px + 11, py - 6), other.split(":", 1)[-1], fill=(255, 255, 255))
        for (_, _, px, py, vis) in pself:
            if vis:
                d.rectangle([px - 6, py - 6, px + 6, py + 6], outline=(0, 255, 255), width=3)
        im.save(os.path.join(folder, f"{i:02d}_overlay.png"))
        visible = sum(1 for p in proj if p[4])
        meta["samples"].append({"x": sx, "z": sz, "teleport": tele, "shot": raw, "outline_visible": visible,
                                "outline_total": len(proj), "entities": ents[:40]})
        db.c.execute("INSERT INTO shots(room_key, path, cam, sha) VALUES(?,?,?,?)", (room["key"], raw, json.dumps({"x": sx, "z": sz}), ""))
        print(f"  {room['key']} sample {i}: {tele}; outline visible {visible}/{len(proj)}; {len(ents)} labelled entities")
    with open(os.path.join(folder, "meta.json"), "w", encoding="utf-8") as f:
        json.dump(meta, f, indent=1)
    if meta["samples"]:    # a room every sample of which failed stays unseen (the run log says why)
        db.c.execute("UPDATE rooms SET status='shot' WHERE key=? AND status IN ('unseen','stale')", (room["key"],))
    db.c.commit()
    print(f"  {room['key']}: {len(meta['samples'])}/{len(pts)} samples in {time.time() - t_room:.1f}s (teleport {t_tele:.1f}s)")
    return meta


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("room"); s.add_argument("key"); s.add_argument("--samples", type=int, default=2); s.add_argument("--no-fog", action="store_true")
    s = sub.add_parser("region"); s.add_argument("region"); s.add_argument("--status", default="unseen"); s.add_argument("--limit", type=int, default=0)
    s.add_argument("--samples", type=int, default=2); s.add_argument("--no-fog", action="store_true")
    args = ap.parse_args()
    db = RoomsDb(DB)
    if args.cmd == "room":
        region_key = args.key.split(":", 1)[0]
        rooms = [r for r in db.rooms(region_key) if r["key"] == args.key]
    else:
        region_key = args.region
        rooms = [r for r in db.rooms(region_key) if r["status"] == args.status]
    global STONES
    g0 = db.grid(region_key)
    if g0:
        STONES = build_stones(g0)
        print(f"{len(STONES)} teleport stones")
    close_game_windows()
    # The dev character poses among live monsters; a death mid-tour ruins shots (respawn screenshots).
    # Despite the name this is a setter (the bool lands in SetInvincibleConfigCmd+0x10), so it is idempotent.
    m = re.search(r" id=(\d+)", get("/player"))
    if m:
        print("invincible:", get("/lua", code=f"local p = Player.Get({m.group(1)}); p:ToggleInvincible(true)").strip())
    if args.cmd == "region":
        print("game", get("/pause", set=0).strip(), "(a hot reload in the world leaves the game paused; unpause before the tour)")
        # a nearest-neighbour tour from the character's position keeps every hop short (chunk streaming)
        px, pz = player_xz()
        tour = []
        while rooms:
            i = min(range(len(rooms)), key=lambda k: (rooms[k]["anchor_x"] - px) ** 2 + (rooms[k]["anchor_z"] - pz) ** 2)
            r = rooms.pop(i); tour.append(r); px, pz = r["anchor_x"], r["anchor_z"]
        rooms = tour
        if args.limit:
            rooms = rooms[:args.limit]
    grid = db.grid(region_key)
    if not grid or not rooms:
        print("nothing to do"); return
    print(f"{len(rooms)} rooms")
    for r in rooms:
        shoot_room(db, region_key, r, grid, args.samples, not args.no_fog)


if __name__ == "__main__":
    main()
