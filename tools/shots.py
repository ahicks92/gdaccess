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


def get(path, **q):
    url = f"http://127.0.0.1:{PORT}{path}" + ("?" + urllib.parse.urlencode(q) if q else "")
    return urllib.request.urlopen(url, timeout=30).read().decode("utf-8", "replace")


def shot(path: str) -> None:
    subprocess.run([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)), "gd.py"), "shot", path], check=True,
                   stdout=subprocess.DEVNULL)


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


def terrain_facts(region_key: str, room: dict, grid) -> dict:
    """Fraction of the room's cells under each painted terrain layer (the describer's 'muddy', 'cobbled')."""
    from gdmap.level import mask_to_cells, parse_terrain_layers, parse_tiles, walk_grid
    from gdmap.mapfile import WorldMap
    x0, z0, cell, labels, label_keys = grid
    label = label_keys.index(room["key"])
    mask = labels == label
    n = int(mask.sum())
    if n == 0:
        return {}
    wm = WorldMap()
    chunks = json.loads(RoomsDb(DB).c.execute("SELECT chunks FROM regions WHERE key=?", (region_key,)).fetchone()[0])
    bx0, bz0, bx1, bz1 = json.loads(room["bbox"])
    out = {}
    for lvl in chunks:
        r = wm.by_name[lvl.rsplit("/", 1)[-1].replace("\\", "/").removeprefix("Region").removesuffix(".lvl")]
        ox, oz = r.world_offset[0], r.world_offset[2]
        if bx1 < ox or bx0 > ox + 128 or bz1 < oz or bz0 > oz + 128:
            continue
        body = wm.level_body(r)
        g = walk_grid(parse_tiles(body))
        for layer in parse_terrain_layers(body):
            if layer.mask is None:
                continue
            cells = mask_to_cells(layer.mask, g, float(ox), float(oz))
            # place the chunk grid's cells into the region grid frame
            c0 = int(round((g.x0 - x0) / cell)); r0 = int(round((g.z0 - z0) / cell))
            sub = np.zeros_like(mask)
            h, w = cells.shape
            rs, cs = slice(max(r0, 0), min(r0 + h, mask.shape[0])), slice(max(c0, 0), min(c0 + w, mask.shape[1]))
            if rs.stop > rs.start and cs.stop > cs.start:
                sub[rs, cs] = cells[rs.start - r0: rs.stop - r0, cs.start - c0: cs.stop - c0]
            k = int((sub & mask).sum())
            if k:
                name = layer.record.rsplit("/", 1)[-1].removesuffix(".dbr")
                out[name] = round(out.get(name, 0) + k / n, 3)
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
    for i, (sx, sz) in enumerate(pts):
        tele = get("/teleport", x=f"{sx:.2f}", z=f"{sz:.2f}").strip()
        if fog:
            get("/fog", x=f"{sx:.2f}", z=f"{sz:.2f}", radius=40)
        time.sleep(1.6)     # let the chunk stream and the camera settle
        raw = os.path.join(folder, f"{i:02d}.png")
        shot(raw)
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
    db.c.execute("UPDATE rooms SET status='shot' WHERE key=? AND status IN ('unseen','stale')", (room["key"],))
    db.c.commit()
    return meta


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("room"); s.add_argument("key"); s.add_argument("--samples", type=int, default=3); s.add_argument("--no-fog", action="store_true")
    s = sub.add_parser("region"); s.add_argument("region"); s.add_argument("--status", default="unseen"); s.add_argument("--limit", type=int, default=0)
    s.add_argument("--samples", type=int, default=3); s.add_argument("--no-fog", action="store_true")
    args = ap.parse_args()
    db = RoomsDb(DB)
    if args.cmd == "room":
        region_key = args.key.split(":", 1)[0]
        rooms = [r for r in db.rooms(region_key) if r["key"] == args.key]
    else:
        region_key = args.region
        rooms = [r for r in db.rooms(region_key) if r["status"] == args.status]
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
