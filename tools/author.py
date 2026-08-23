"""The authoring CLI over assets/rooms.db, used by the rooms authoring workflow's agents (docs/rooms.md M5) and
by hand. Everything an agent needs to read or write goes through here; agents never open the db.

  uv run tools/author.py list <region> [--status shot] [--subregion K]   rooms: key, status, cls, area, title, subregion
  uv run tools/author.py facts <room_key>         everything known about a room: meta.json facts, shots, exits,
                                                  sub-region, neighbours' titles (JSON)
  uv run tools/author.py export <region>          build/rooms/<region>_rooms.json: rooms + exits + sub-regions
                                                  (for the sub-region agent) and the db floor plan PNG path
  uv run tools/author.py subregion <region> <key> --name "The prison" [--summary "..."]
  uv run tools/author.py assign <room_key> <subregion_key>
  uv run tools/author.py describe <room_key> --title "..." --body "..."    (status -> described)
  uv run tools/author.py verify <room_key> [--fail "reason"]               (status -> verified | described)
  uv run tools/author.py status <region>          counts per status and per sub-region"""
from __future__ import annotations

import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gdmap.roomsdb import RoomsDb  # noqa: E402

DB = "assets/rooms.db"
SHOTS = "build/shots"


def shot_dir(room_key: str) -> str:
    return os.path.join(SHOTS, room_key.split(":", 1)[0], room_key.replace(":", "_"))


def cmd_list(db, a):
    rows = db.rooms(a.region)
    for r in rows:
        if a.status and r["status"] != a.status:
            continue
        if a.subregion and r["subregion_key"] != a.subregion:
            continue
        print(f"{r['key']:32s} {r['status']:10s} {r['cls']:8s} {r['area']:6.0f} m2  sub={r['subregion_key'] or '-':18s} title={r['title'] or '-'}")


def cmd_facts(db, a):
    region_key = a.key.split(":", 1)[0]
    room = next((r for r in db.rooms(region_key) if r["key"] == a.key), None)
    if not room:
        print(json.dumps({"error": "no such room"})); return
    meta_path = os.path.join(shot_dir(a.key), "meta.json")
    meta = json.load(open(meta_path, encoding="utf-8")) if os.path.exists(meta_path) else {}
    region = db.c.execute("SELECT name FROM regions WHERE key=?", (region_key,)).fetchone()
    sub = db.c.execute("SELECT name, summary FROM subregions WHERE key=?", (room["subregion_key"] or "",)).fetchone()
    neighbours = []
    for ra, rb, x, z, width, cut in db.c.execute("SELECT room_a, room_b, x, z, width, cut FROM exits WHERE region_key=? AND (room_a=? OR room_b=?)",
                                                  (region_key, a.key, a.key)):
        other = rb if ra == a.key else ra
        o = db.c.execute("SELECT title, cls, subregion_key, anchor_x, anchor_z FROM rooms WHERE key=?", (other,)).fetchone()
        dx, dz = x - room["anchor_x"], z - room["anchor_z"]
        bearing = ["north", "north-east", "east", "south-east", "south", "south-west", "west", "north-west"][int(((__import__("math").degrees(__import__("math").atan2(dx, -dz)) % 360) + 22.5) // 45) % 8]
        neighbours.append({"room": other, "title": o[0] if o else None, "cls": o[1] if o else None, "subregion": o[2] if o else None,
                           "bearing": bearing, "width": width, "cap_cut": bool(cut)})
    shots = sorted(f for f in os.listdir(shot_dir(a.key)) if f.endswith("_overlay.png")) if os.path.isdir(shot_dir(a.key)) else []
    out = {"room": room, "region_name": region[0] if region else region_key, "subregion": {"key": room["subregion_key"], "name": sub[0] if sub else None, "summary": sub[1] if sub else None},
           "terrain": meta.get("terrain", {}), "samples": [{"x": s["x"], "z": s["z"], "entities": s["entities"]} for s in meta.get("samples", [])],
           "shots": [os.path.abspath(os.path.join(shot_dir(a.key), f)) for f in shots], "exits": neighbours}
    print(json.dumps(out, indent=1))


def cmd_export(db, a):
    rooms = db.rooms(a.region)
    exits = [dict(zip(("room_a", "room_b", "x", "z", "width", "cut"), row)) for row in
             db.c.execute("SELECT room_a, room_b, x, z, width, cut FROM exits WHERE region_key=?", (a.region,))]
    subs = [dict(zip(("key", "name", "summary"), row)) for row in db.c.execute("SELECT key, name, summary FROM subregions WHERE region_key=?", (a.region,))]
    # the floor plan's ids are the rooms' order by (anchor z, x) = db.rooms() order
    for i, r in enumerate(rooms):
        r["plan_id"] = i
        meta_path = os.path.join(shot_dir(r["key"]), "meta.json")
        if os.path.exists(meta_path):
            meta = json.load(open(meta_path, encoding="utf-8"))
            r["terrain"] = meta.get("terrain", {})
            labels = sorted({e["label"] for s in meta.get("samples", []) for e in s["entities"] if e.get("label")})
            r["landmarks"] = labels[:12]
    name = db.c.execute("SELECT name FROM regions WHERE key=?", (a.region,)).fetchone()
    out = {"region": a.region, "name": name[0] if name else a.region, "plan_png": os.path.abspath(f"build/rooms/{a.region}_db.png"),
           "rooms": rooms, "exits": exits, "subregions": subs}
    path = f"build/rooms/{a.region}_rooms.json"
    os.makedirs("build/rooms", exist_ok=True)
    json.dump(out, open(path, "w", encoding="utf-8"), indent=1)
    print(path, len(rooms), "rooms", len(exits), "exits", len(subs), "sub-regions")


def cmd_subregion(db, a):
    db.c.execute("INSERT INTO subregions(key, region_key, name, summary) VALUES(?,?,?,?) ON CONFLICT(key) DO UPDATE SET name=excluded.name, summary=COALESCE(excluded.summary, subregions.summary)",
                 (a.key, a.region, a.name, a.summary))
    db.c.commit(); print("ok")


def cmd_assign(db, a):
    n = db.c.execute("UPDATE rooms SET subregion_key=? WHERE key=?", (a.subregion, a.key)).rowcount
    db.c.commit(); print("ok" if n else "no such room")


def cmd_describe(db, a):
    n = db.c.execute("UPDATE rooms SET title=?, body=?, status='described' WHERE key=?", (a.title.strip(), a.body.strip(), a.key)).rowcount
    db.c.commit(); print("ok" if n else "no such room")


def cmd_verify(db, a):
    if a.fail:
        n = db.c.execute("UPDATE rooms SET status='described' WHERE key=?", (a.key,)).rowcount
        print(f"kept as described ({a.fail})" if n else "no such room")
    else:
        n = db.c.execute("UPDATE rooms SET status='verified' WHERE key=? AND title IS NOT NULL", (a.key,)).rowcount
        print("ok" if n else "no such room or no title")
    db.c.commit()


def cmd_status(db, a):
    print(dict(db.c.execute("SELECT status, COUNT(*) FROM rooms WHERE region_key=? GROUP BY status", (a.region,)).fetchall()))
    for key, name, n in db.c.execute("SELECT s.key, s.name, COUNT(r.key) FROM subregions s LEFT JOIN rooms r ON r.subregion_key=s.key WHERE s.region_key=? GROUP BY s.key", (a.region,)):
        print(f"  {key:24s} {name:30s} {n} rooms")
    un = db.c.execute("SELECT COUNT(*) FROM rooms WHERE region_key=? AND subregion_key IS NULL", (a.region,)).fetchone()[0]
    print("  unassigned rooms:", un)


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("list"); s.add_argument("region"); s.add_argument("--status"); s.add_argument("--subregion"); s.set_defaults(fn=cmd_list)
    s = sub.add_parser("facts"); s.add_argument("key"); s.set_defaults(fn=cmd_facts)
    s = sub.add_parser("export"); s.add_argument("region"); s.set_defaults(fn=cmd_export)
    s = sub.add_parser("subregion"); s.add_argument("region"); s.add_argument("key"); s.add_argument("--name", required=True); s.add_argument("--summary"); s.set_defaults(fn=cmd_subregion)
    s = sub.add_parser("assign"); s.add_argument("key"); s.add_argument("subregion"); s.set_defaults(fn=cmd_assign)
    s = sub.add_parser("describe"); s.add_argument("key"); s.add_argument("--title", required=True); s.add_argument("--body", required=True); s.set_defaults(fn=cmd_describe)
    s = sub.add_parser("verify"); s.add_argument("key"); s.add_argument("--fail"); s.set_defaults(fn=cmd_verify)
    s = sub.add_parser("status"); s.add_argument("region"); s.set_defaults(fn=cmd_status)
    a = ap.parse_args()
    a.fn(RoomsDb(DB), a)


if __name__ == "__main__":
    main()
