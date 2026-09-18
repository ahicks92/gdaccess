"""Write docs/masteries.md: one heading per mastery, a table of every skill with the game's own tooltip at level 0 and
at the skill's maximum level. The text comes from the running game (dev route /masteries = GameEngine::GenerateUISkillText
with the skill temporarily at the level, gameapi::skill_tooltip_at); the tree order is the game's class tables
(records/ui/skills/classNN/classtable.dbr tabSkillButtons -> skillNN.dbr skillName), read offline with tools/arz.py.

Usage: uv run tools/gen_masteries_doc.py [--dump build/masteries_dump.txt] [--out docs/masteries.md]
  --dump reuses a saved dump instead of asking the game (the live fetch saves one to build/masteries_dump.txt).
The game must be in the world on a character (any: the tooltips do not depend on the character's own choices, only
the mastery bar and skill levels are temporarily raised and restored around each text).
"""
import argparse, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import arz  # noqa: E402
import gd  # noqa: E402

def fetch_dump(save_to):
    text = gd.get("/masteries", timeout=600)
    if not text.startswith("mastery "):
        sys.exit(f"unexpected /masteries reply: {text[:200]!r}")
    os.makedirs(os.path.dirname(save_to), exist_ok=True)
    open(save_to, "w", encoding="utf-8", newline="\n").write(text)
    return text

def parse_dump(text):
    """-> (masteries {enum: name}, skills {record: {fields..., 'tips': {level: [lines]}}})"""
    masteries, skills = {}, {}
    cur = None; level = None
    for line in text.split("\n"):
        if line.startswith("mastery "):
            m = re.match(r"mastery (\d+) name=(.*)", line); masteries[int(m.group(1))] = m.group(2)
        elif line.startswith("skill record="):
            cur = {"record": line[len("skill record="):], "tips": {}}; level = None
        elif cur is not None and line.startswith("name=") and "tips" in cur and not cur.get("name_set"):
            cur["name"] = line[5:]; cur["name_set"] = True
        elif cur is not None and line.startswith("enum="):
            for kv in line.split(" "):
                k, _, v = kv.partition("="); cur[k] = v
        elif cur is not None and line.startswith("aim="):
            cur["aim"] = line[4:]   # "<spoken word>|<raw target type>|<RTTI class>"
        elif line.startswith("@level "):
            level = int(line[7:]); cur["tips"][level] = []
        elif line.startswith("\t") and cur is not None and level is not None:
            cur["tips"][level].append(line[1:])
        elif line == "end" and cur is not None:
            skills[cur["record"].lower()] = cur; cur = None
    return masteries, skills

def class_order():
    """{mastery enum: [skill record path, ...]} in the game's own pane order."""
    d, strings, recs = arz.load()
    by_path = {p.lower(): (p, rn, off, csz, dsz) for p, rn, off, csz, dsz in recs}
    def rec(path):
        e = by_path.get(path.lower())
        return arz.decode(d, strings, e[2], e[3], e[4]) if e else {}
    order = {}
    for n in range(1, 10):
        table = rec(f"records/ui/skills/class{n:02}/classtable.dbr")
        buttons = []
        for btn in table.get("tabSkillButtons", []):
            r = rec(btn)
            name = r.get("skillName", [""])[0]
            if not name: continue
            # the pane's own layout: columns = the mastery level a skill needs (left to right), rows top to bottom;
            # the mastery bar sits at x 0 under everything. tabSkillButtons itself is authoring order (skill32 after
            # skill01), so sort by drawn position, mastery bar first.
            x, y = r.get("bitmapPositionX", [0])[0], r.get("bitmapPositionY", [0])[0]
            buttons.append((0 if "classtraining" in btn.lower() else 1, x, y, name.lower()))
        buttons.sort()
        order[n - 1] = [b[3] for b in buttons]
    return order

HINT_LINES = {"Left click to add unused skill points."}   # the window's own click hint, not skill information

def cell(lines):
    lines = [re.sub(r"\^[a-zA-Z]", "", l).strip() for l in lines]   # ^o etc. are the game's inline colour codes
    lines = [l for l in lines if l and l not in HINT_LINES]
    return "<br>".join(l.replace("|", "\\|") for l in lines)

def aim_cell(s):
    """The mod's own spoken aim word (world::skill_aim, what Ctrl+digit says for a hotbar slot) plus the game's skill
    class as a hint; passives / modifiers say so; a target type the mod does not speak yet is named honestly."""
    word, tt, cls = (s.get("aim", "||") + "||").split("|")[:3]
    short = cls.replace("SkillSecondary_", "").replace("Skill_", "")
    if s.get("mastery_skill") == "1": return "mastery bar"
    if not word:
        if tt in ("", "-1"):
            if s.get("modifier") == "1" or "Modifier" in cls or "Transmuter" in cls or cls.startswith("SkillSecondary_"): return f"modifier ({short})"
            return f"passive ({short})"
        return f"at a point, not yet spoken by the mod (target type {tt}, {short})"
    if cls == "Skill_TargetedSpawnPet": return f"placed at the cursor, enemy or ground ({short})"   # DBR targetingMode = Point; runtime type 2
    return f"{word} ({short})"

def write_doc(masteries, skills, order, out_path):
    L = []
    L.append("# Masteries and their skills")
    L.append("")
    L.append("Generated by `tools/gen_masteries_doc.py` from the running game (each skill's tooltip as the game builds it, first")
    L.append("with the skill unlearned -- what the tooltip shows before the first point -- then at the skill's maximum level).")
    L.append("Skills are in the order the game draws them: left to right by the mastery level they need (the \"Mastery level\"")
    L.append("column), top to bottom within a column; a modifier stands where the game draws it, labelled with the skill it")
    L.append("modifies. The first row of each mastery is its mastery bar (levels 1-50, the attribute bonuses).")
    L.append("Two caveats: values that depend on the character (\"Main Hand Damage (34 - 54)\", pet stats) are the dev character's,")
    L.append("a level 24 Soldier with a plain weapon; and \"max level\" is the skill's own cap -- the \"Next Level\" block shown after it")
    L.append("is the game's preview of the levels beyond the cap that +skill item bonuses can reach.")
    L.append("")
    L.append("The Aim column is what the mod says for the skill on a hotbar slot: \"self\" needs no aiming, \"around you\" hits")
    L.append("everything near the character, \"at a target\" goes to the virtual cursor (the reviewed or locked thing), \"at a spot\"")
    L.append("is a ground point. \"At a target\" does not require an enemy: the game fires the skill at the cursor's ground point when")
    L.append("nothing is under it (it first looks for an enemy close to that point), except the charges (Blitz, Shadow Strike),")
    L.append("which need an enemy and do nothing without one. Totems, seals, traps and summons (\"placed at the cursor\") drop at")
    L.append("the cursor's point, so a locked enemy puts them under that enemy. Passives and modifiers are never activated. The")
    L.append("game's skill class follows in parentheses as a hint (WPAttack = a default-attack replacer, AttackPathCharge = a")
    L.append("charge to the target).")
    L.append("")
    L.append("## Which masteries are available")
    L.append("")
    L.append("Six masteries ship with the base game: Soldier, Demolitionist, Occultist, Nightblade, Arcanist, Shaman. Three come with")
    L.append("the expansions and are simply absent (a greyed \"?\" tile in the class selection) unless that expansion is installed;")
    L.append("nothing in the game unlocks them and no character progress is needed:")
    L.append("")
    L.append("- Inquisitor and Necromancer: Ashes of Malmouth.")
    L.append("- Oathkeeper: Forgotten Gods.")
    L.append("")
    L.append("A character picks one mastery at level 1 (from level 2 in the skills window) and a second one at level 10; the two can")
    L.append("be any combination. Masteries can never be changed once a point is spent in their bar, only reclaimed down to level 1.")
    L.append("")
    for e in sorted(masteries):
        name = masteries[e]
        L.append(f"## {name}")
        L.append("")
        L.append("| Skill | Mastery level | Max level | Aim | Tooltip at level 0 | Tooltip at max level |")
        L.append("|---|---|---|---|---|---|")
        listed = set()
        paths = order.get(e, [])
        for r in paths:
            if r not in skills: print(f"  {name}: class table lists {r} but the dump has no such skill")
        # the class table order first, then anything of this mastery the table did not list
        extra = [r for r, s in skills.items() if s.get("enum") == str(e) and r not in paths]
        for r in paths + sorted(extra):
            s = skills.get(r)
            if not s or r in listed: continue
            listed.add(r)
            label = s.get("name") or r
            if s.get("mastery_skill") == "1": label = f"{label} (mastery bar)"
            base = s.get("base", "")
            if base and base.lower() in skills: label = f"{label} (modifies {skills[base.lower()].get('name') or base})"
            mx = int(s.get("max", "0") or 0)
            need = "" if s.get("mastery_skill") == "1" else s.get("req", "")
            L.append(f"| {label} | {need} | {mx} | {aim_cell(s)} | {cell(s['tips'].get(0, []))} | {cell(s['tips'].get(mx, []))} |")
        L.append("")
    open(out_path, "w", encoding="utf-8", newline="\n").write("\n".join(L))
    print(f"wrote {out_path}: {len(masteries)} masteries, {len(skills)} skills")

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dump", help="reuse a saved /masteries dump")
    ap.add_argument("--out", default=os.path.join(ROOT, "docs", "masteries.md"))
    a = ap.parse_args()
    text = open(a.dump, encoding="utf-8").read() if a.dump else fetch_dump(os.path.join(ROOT, "build", "masteries_dump.txt"))
    masteries, skills = parse_dump(text)
    write_doc(masteries, skills, class_order(), a.out)

if __name__ == "__main__":
    main()
