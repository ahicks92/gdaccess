"""Fit the compass the game's dialogue uses against the world coordinates in assets/rooms.db (docs/compass.md).
Usage: uv run tools/compass_fit.py -- prints the rms disagreement for the mod's yaw-0 pin, the game's default yaw, pi/2
and the free best fit, then every anchor. Anchors = a direction the game's text (Conversations.arc / Quests.arc /
Text_EN lore) gives between two places; the places are room centroids (surface regions only). 2026-09-11."""
import os
import sqlite3, math
c = sqlite3.connect(r"D:\projects\in_progress\gdaccess\assets\rooms.db")
UG = ("_ug", "cave", "cellar", "undercity", "interior", "armory", "outcast", "deeps", "mine", "crypt", "tomb", "lab", "transit")
cent = {}
for name, region, n, x, z in c.execute("select area_name, region_key, count(*), avg(anchor_x), avg(anchor_z) from rooms where area_name is not null group by area_name, region_key"):
    if any(u in region.lower() for u in UG): continue
    cent.setdefault(name, []).append((n, x, z))
def area(name):
    if name not in cent: return None
    n = sum(a[0] for a in cent[name]); return (sum(a[0] * a[1] for a in cent[name]) / n, sum(a[0] * a[2] for a in cent[name]) / n)
sub = {}
for name, region, n, x, z in c.execute("select s.name, s.region_key, count(*), avg(r.anchor_x), avg(r.anchor_z) from rooms r join subregions s on s.key=r.subregion_key group by s.key"):
    sub[(region, name)] = (x, z)
P = {
    "DC": sub[("devilscrossing", "The prison")], "LCbridge": sub[("devilscrossing", "The crossroads")],
    "Hargate": sub[("devilscrossing", "Hargate's Isle")], "BurialHill": sub[("lowercrossing", "Burial Hill")],
}
for a in ("Burrwitch Village", "Wightmire", "Arkovian Foothills", "Homestead", "Lower Crossing", "Sorrow's Bastion", "The Blood Grove",
          "Darkvale Gate", "Prospector's Trail", "Fort Ikon", "Twin Falls", "Old Arkovia", "Cronley's Hideout", "Burrwitch Estates",
          "Moldering Fields", "East Marsh", "Plains of Strife", "Rotting Croplands", "Fallow Fields", "Smuggler's Pass", "Jagged Waste",
          "Old Grove", "Burrwitch Outskirts", "The Conflagration", "Broken Hills", "Mountain Deeps", "Village of Darkvale", "Foggy Bank"):
    P[a] = area(a)
for k, v in P.items():
    if v is None: print("MISSING", k)
# (from, to, bearing the text gives, source)
A = [
    ("DC", "Burrwitch Village", 0, "Bourbon: Burrwitch Village to the north, past Wightmire"),
    ("DC", "Wightmire", 0, "Bourbon: follow the road north, through Wightmire"),
    ("DC", "Burrwitch Outskirts", 0, "Bourbon: keep pressing north ... outskirts of Burrwitch"),
    ("DC", "Lower Crossing", 0, "quest log: head north across the bridge"),
    ("DC", "Arkovian Foothills", 315, "bridge object / Barnabas: Arkovian Foothills to the northwest"),
    ("DC", "Homestead", 315, "Barnabas: farmlands far to the northwest (Homestead)"),
    ("DC", "Homestead", 0, "Barnabas: nobody's been as far north as Homestead"),
    ("LCbridge", "Hargate", 315, "Kasparov: boat past the bridge, take it northwest to Hargate's Isle"),
    ("DC", "Old Grove", 270, "Creed's note: Old Grove west of Devil's Crossing (cut region)"),
    ("Burrwitch Village", "Burrwitch Estates", 0, "Bourbon: Krieg's mansion in the northern district"),
    ("Burrwitch Village", "Moldering Fields", 180, "Harmond: Moldering Fields, south of Burrwitch"),
    ("Burrwitch Village", "East Marsh", 90, "Keeper of Tomes: East Marsh, east of Burrwitch"),
    ("Cronley's Hideout", "Homestead", 0, "Cronley's note: headed north to Homestead"),
    ("Twin Falls", "Old Arkovia", 180, "Twin Falls rover: south of here, the ruins of Arkovia"),
    ("Old Arkovia", "Twin Falls", 0, "Eva: a hive to the north near Twin Falls"),
    ("Prospector's Trail", "Homestead", 0, "Ulgrim: follow the trail north to Homestead"),
    ("Homestead", "Sorrow's Bastion", 0, "captain: Sorrow's Bastion, north of Homestead"),
    ("Homestead", "The Blood Grove", 0, "scoutmaster: Blood Grove, north of Homestead"),
    ("Homestead", "Fort Ikon", 0, "captain/Creed: Fort Ikon to the (far) north"),
    ("Homestead", "Fallow Fields", 0, "emissary: the road north out of Homestead through the Fallow Fields"),
    ("Homestead", "Rotting Croplands", 270, "Douglass: Rotting Cropland west of Homestead"),
    ("Homestead", "Smuggler's Pass", 180, "bounty: Smuggler's Pass ... never try heading north (so it lies south)"),
    ("Homestead", "DC", 135, "bounty: lands to the southeast, roads down to Devil's Crossing"),
    ("Sorrow's Bastion", "Darkvale Gate", 0, "captain: Darkvale Gates, north of Sorrow's Bastion"),
    ("Fort Ikon", "Plains of Strife", 0, "bounty: Plains of Strife, north of Fort Ikon"),
    ("Homestead", "The Conflagration", 0, "bounty: the Conflagration, north of Homestead"),
]
def bearing(yaw, dx, dz):
    nx, nz = -math.sin(yaw), -math.cos(yaw); ex, ez = math.cos(yaw), -math.sin(yaw)
    return math.degrees(math.atan2(dx * ex + dz * ez, dx * nx + dz * nz)) % 360
def wrap(d): return (d + 180) % 360 - 180
rows = []
for f, t, b, src in A:
    if P.get(f) is None or P.get(t) is None: continue
    rows.append((P[t][0] - P[f][0], P[t][1] - P[f][1], b, src))
def score(yaw): return sum(wrap(bearing(yaw, dx, dz) - b) ** 2 for dx, dz, b, _ in rows) / len(rows)
best = min((score(math.radians(d)), d) for d in range(0, 360))
print(f"anchors used: {len(rows)}")
for label, yaw in (("current yaw 0", 0.0), ("game default 0.8727", 0.8727), ("proposed pi/2", math.pi / 2), (f"best fit {best[1]} deg", math.radians(best[1]))):
    res = [abs(wrap(bearing(yaw, dx, dz) - b)) for dx, dz, b, _ in rows]
    print(f"{label:24s} rms={math.sqrt(score(yaw)):5.1f}  mean={sum(res)/len(res):5.1f}  worst={max(res):5.1f}  n>45={sum(r > 45 for r in res)}")
print()
yaw = math.radians(best[1])
for dx, dz, b, src in rows:
    print(f"text {b:3d}  fit {bearing(yaw, dx, dz):5.1f}  cur {bearing(0, dx, dz):5.1f}  def {bearing(0.8727, dx, dz):5.1f}  prop {bearing(math.pi/2, dx, dz):5.1f}   {src}")
