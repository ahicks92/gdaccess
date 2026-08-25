"""Describe rooms with an OpenRouter vision model -- a cheap Python port of tools/workflows/rooms_author.js
(docs/rooms.md). Reuses author.py's build_facts / check_room / save_description and the description rules, and
the same shot images + trimmed facts. Cost lives on OpenRouter (pay-per-use, off the Anthropic limit), so this
is ~1000x cheaper than the Opus workflow.

  uv run tools/describe_or.py describe <region> [--model google/gemini-3.7-flash] [--status shot] [--limit N] [--workers 8]
  uv run tools/describe_or.py subregions <region> [--model ...]
  uv run tools/describe_or.py room <room_key> [--model ...]        # one room, prints the result (bake-off)

OPENROUTER_API_KEY is read from .env (gitignored). The mechanical check (author.py check_room) is the quality
gate: a weak model just burns a few cheap retries, it never lowers the bar. Prices for the cost line come from
MODEL_PRICES below (per 1M tokens); update if OpenRouter changes them."""
from __future__ import annotations

import argparse
import base64
import json
import os
import random
import sys
import threading
import time
import urllib.request
from concurrent.futures import ThreadPoolExecutor, as_completed

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import author  # noqa: E402  (build_facts, check_room, save_description, cmd_subregion/assign helpers)
from gdmap.roomsdb import RoomsDb  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DB = os.path.join(ROOT, "assets", "rooms.db")
RULES = open(os.path.join(ROOT, "docs", "rooms-description-rules.md"), encoding="utf-8").read()
ENDPOINT = "https://openrouter.ai/api/v1/chat/completions"
MODEL_PRICES = {   # $ per 1M tokens (prompt, completion)
    "google/gemini-3.7-flash": (0.375, 1.875),
    "google/gemini-2.5-flash-lite": (0.10, 0.40),
    "qwen/qwen3-vl-32b-instruct": (0.104, 0.416),
    "amazon/nova-lite-v1": (0.06, 0.24),
}
_cost_lock = threading.Lock()
_cost = {"in": 0, "out": 0, "calls": 0}


def api_key():
    for line in open(os.path.join(ROOT, ".env"), encoding="utf-8"):
        if line.startswith("OPENROUTER_API_KEY="):
            return line.split("=", 1)[1].strip()
    raise SystemExit("OPENROUTER_API_KEY not in .env")


KEY = None


def data_url(path):
    with open(path, "rb") as f:
        return "data:image/png;base64," + base64.b64encode(f.read()).decode()


class CreditsExhausted(Exception):
    """OpenRouter is out of credit -- stop the whole run (the user must add credit; no workarounds)."""


class SafetyBlocked(Exception):
    """The model's safety filter refused (Gemini false-flags innocuous scenery). Record + skip the room."""


def _is_credits(code, msg):
    m = msg.lower()
    return code == 402 or "insufficient" in m or "requires more credit" in m or ("credit" in m and "add" in m)


def _is_safety(code, msg):
    m = msg.lower()
    return any(w in m for w in ("safety", "blocklist", "prohibited", "csam", "content_filter", "content policy", "blocked"))


def or_chat(model, messages, schema=None, max_tokens=800, retries=5):
    """One chat completion. Returns (content_str, usage_dict). Retries 429/5xx with backoff. Raises
    CreditsExhausted on a 402/credit error (stop the run) and SafetyBlocked on a content-filter refusal (skip)."""
    body = {"model": model, "messages": messages, "max_tokens": max_tokens, "temperature": 0.4}
    if schema:
        body["response_format"] = {"type": "json_schema", "json_schema": {"name": "out", "strict": True, "schema": schema}}
    data = json.dumps(body).encode()
    for attempt in range(retries):
        req = urllib.request.Request(ENDPOINT, data=data, headers={
            "Authorization": f"Bearer {KEY}", "Content-Type": "application/json",
            "HTTP-Referer": "https://github.com/gdaccess", "X-Title": "gdaccess rooms"})
        try:
            with urllib.request.urlopen(req, timeout=120) as r:
                o = json.load(r)
            if isinstance(o.get("error"), dict):   # OpenRouter can return 200 with an error body
                em = str(o["error"].get("message", "")); ec = o["error"].get("code", 0)
                if _is_credits(ec, em): raise CreditsExhausted(em)
                if _is_safety(ec, em): raise SafetyBlocked(em)
                raise RuntimeError(f"OpenRouter error: {em[:200]}")
            u = o.get("usage", {}) or {}
            with _cost_lock:
                _cost["in"] += u.get("prompt_tokens", 0); _cost["out"] += u.get("completion_tokens", 0); _cost["calls"] += 1
            ch = (o.get("choices") or [{}])[0]
            fr = ch.get("finish_reason") or ch.get("native_finish_reason")
            if fr in ("content_filter", "safety") or _is_safety(0, str(o.get("prompt_feedback", ""))):
                raise SafetyBlocked(f"finish_reason={fr}")
            return (ch.get("message", {}) or {}).get("content"), u
        except urllib.error.HTTPError as e:
            code = e.code; msg = e.read().decode(errors="replace")[:300]
            if _is_credits(code, msg): raise CreditsExhausted(msg)
            if _is_safety(code, msg): raise SafetyBlocked(msg)
            if code in (429, 500, 502, 503, 529) and attempt < retries - 1:
                ra = e.headers.get("Retry-After") if e.headers else None
                wait = float(ra) if (ra and ra.isdigit()) else 2 ** attempt
                time.sleep(wait + random.uniform(0, 1.5)); continue
            if schema and code == 400 and attempt == 0:   # model may not support json_schema -> fall back
                body.pop("response_format", None); data = json.dumps(body).encode(); continue
            raise RuntimeError(f"OpenRouter {code}: {msg}")
        except (urllib.error.URLError, TimeoutError) as e:
            if attempt < retries - 1:
                time.sleep(2 ** attempt); continue
            raise RuntimeError(f"network: {e}")
    raise RuntimeError("exhausted retries")


def credits_remaining():
    """(remaining, total) OpenRouter credit in $, or (None, None) if the endpoint is unavailable."""
    try:
        req = urllib.request.Request("https://openrouter.ai/api/v1/credits", headers={"Authorization": f"Bearer {KEY}"})
        d = json.load(urllib.request.urlopen(req, timeout=30)).get("data", {})
        tot = float(d.get("total_credits", 0)); used = float(d.get("total_usage", 0))
        return tot - used, tot
    except Exception:   # noqa: BLE001
        return None, None


def parse_json(content):
    s = content.strip()
    if s.startswith("```"):
        s = s.split("```", 2)[1].removeprefix("json").strip()
    return json.loads(s)


DESC_SCHEMA = {"type": "object", "properties": {"title": {"type": "string"}, "body": {"type": "string"}},
               "required": ["title", "body"], "additionalProperties": False}

SYSTEM = f"""You write short place descriptions for a screen-reader mod for Grim Dawn (an ARPG). A blind player
hears "<region>, <sub-region>, <room title>" and can ask for a one-or-two-sentence description of where they are.
Follow these rules exactly:

{RULES}

Return ONLY JSON: {{"title": "...", "body": "..."}}. Title: 2-6 lowercase words, no digits, a usable place name
(not the region or sub-region name). Body: one sentence, two at most, under 40 words, plain nouns and verbs -- the
ground plus the one or two fixtures you would recognise the place by. No exits, no neighbours, no directions
(north/south/leads/way out), no units, no NPC/monster names."""


def describe_room(key, model):
    """Describe one room with the mechanical check + retry loop. Returns dict(key,title,body,passed,problem,attempts)."""
    db = RoomsDb(DB)
    facts = author.build_facts(db, key)
    if "error" in facts:
        return {"key": key, "passed": False, "problem": facts["error"], "attempts": 0}
    slim = {k: facts[k] for k in ("room", "region_name", "subregion", "terrain", "nearby") if k in facts}
    slim["room"] = {kk: facts["room"].get(kk) for kk in ("cls", "area")}
    content = [{"type": "text", "text":
                f"Room {key}.\nFacts (terrain fractions, and 'nearby' = fixtures under/around the room):\n"
                f"{json.dumps(slim)}\n\nLook at the shot(s): yellow dots outline THIS room, red dots are exits, "
                f"the cyan box is the character. Only what is inside the yellow outline counts. Write the title and body."}]
    for p in facts.get("shots", [])[:2]:
        if os.path.exists(p):
            content.append({"type": "image_url", "image_url": {"url": data_url(p)}})
    messages = [{"role": "system", "content": SYSTEM}, {"role": "user", "content": content}]
    d = None; problem = ""
    for attempt in range(4):
        try:
            out, _ = or_chat(model, messages, schema=DESC_SCHEMA, max_tokens=2048)
            if not out or not out.strip():
                problem = "empty response"; continue          # retry: some responses come back with null content
            d = parse_json(out)
        except CreditsExhausted:
            raise                                              # stop the whole run -- never worked around
        except SafetyBlocked as e:
            db.c.execute("UPDATE rooms SET status='blocked' WHERE key=?", (key,)); db.c.commit()
            with _cost_lock:
                with open(os.path.join(ROOT, "build", "rooms", "safety_skips.txt"), "a", encoding="utf-8") as fh:
                    fh.write(f"{key}\t{e}\n")
            return {"key": key, "passed": False, "blocked": True, "problem": f"safety: {e}", "attempts": attempt + 1}
        except Exception as e:                                    # noqa: BLE001
            problem = f"call/parse: {e}"; continue              # retry (a truncated/garbled JSON often succeeds next try)
        title, body = str(d.get("title") or "").strip(), str(d.get("body") or "").strip()
        save_final = author.save_description(db, key, title, body)
        problems = author.check_room(db, key)
        if not problems:
            db.c.execute("UPDATE rooms SET status='verified' WHERE key=? AND title IS NOT NULL", (key,)); db.c.commit()
            return {"key": key, "title": save_final, "body": body, "passed": True, "attempts": attempt + 1}
        problem = "; ".join(problems)
        messages += [{"role": "assistant", "content": json.dumps({"title": title, "body": body})},
                     {"role": "user", "content": f"That failed the check: {problem}. Fix exactly those and return corrected JSON."}]
    return {"key": key, "title": (d or {}).get("title"), "body": None, "passed": False, "problem": problem, "attempts": 4}


def cmd_room(a):
    print(json.dumps(describe_room(a.key, a.model), indent=1))
    price = MODEL_PRICES.get(a.model, (0, 0))
    print(f"cost: in={_cost['in']} out={_cost['out']} ~${(_cost['in']*price[0]+_cost['out']*price[1])/1e6:.5f}")


def cmd_describe(a):
    db = RoomsDb(DB)
    # Default: every room not verified AND not blocked (a shot room, or a described-but-failed leftover), so a
    # re-run retries stragglers but never re-hits a safety-blocked room. --status pins one exact status.
    keys = [r["key"] for r in db.rooms(a.region)
            if (r["status"] == a.status if a.status else r["status"] not in ("verified", "blocked"))]
    if a.limit:
        keys = keys[:a.limit]
    if not keys:
        print(f"nothing to describe in {a.region} ({'status=' + a.status if a.status else 'all verified/blocked'})"); return
    rem, tot = credits_remaining()
    if rem is not None:
        print(f"OpenRouter credit: ${rem:.2f} remaining of ${tot:.2f}")
        if rem <= 0.5:
            print("STOP: OpenRouter credit exhausted (<=$0.50). Add credit and re-run; not working around it."); sys.exit(42)
    print(f"describing {len(keys)} rooms in {a.region} with {a.model}, {a.workers} workers")
    t0 = time.time(); done = []; stopped = False
    with ThreadPoolExecutor(max_workers=a.workers) as ex:
        futs = {ex.submit(describe_room, k, a.model): k for k in keys}
        try:
            for i, fut in enumerate(as_completed(futs), 1):
                r = fut.result(); done.append(r)
                if i % 10 == 0 or not r["passed"]:
                    tag = "ok" if r["passed"] else ("BLOCKED" if r.get("blocked") else f"FAIL({r.get('problem','')[:60]})")
                    print(f"  [{i}/{len(keys)}] {r['key']}: {tag}")
        except CreditsExhausted as e:
            stopped = True
            print(f"\nSTOP: OpenRouter credit exhausted ({e}). {sum(1 for r in done if r['passed'])} done this run; "
                  f"the rest stay resumable. Add credit and re-run; not working around it.")
            for f in futs: f.cancel()
    passed = sum(1 for r in done if r["passed"])
    price = MODEL_PRICES.get(a.model, (0, 0))
    dollars = (_cost["in"] * price[0] + _cost["out"] * price[1]) / 1e6
    print(f"\n{passed}/{len(keys)} passed in {time.time()-t0:.0f}s; {len(keys)-passed} failed")
    print(f"tokens in={_cost['in']:,} out={_cost['out']:,} calls={_cost['calls']}; "
          f"~${dollars:.3f} total = ${dollars/max(len(keys),1):.5f}/room")
    blocked = [r for r in done if r.get("blocked")]
    fails = [r for r in done if not r["passed"] and not r.get("blocked")]
    if blocked:
        print(f"safety-blocked (skipped, logged to build/rooms/safety_skips.txt): {len(blocked)}")
    if fails:
        print("failures:", [f"{r['key']}: {r.get('problem','')[:50]}" for r in fails[:10]])
    if stopped:
        sys.exit(42)   # credit exhaustion -> the overnight driver stops here


SUB_SCHEMA = {"type": "object", "properties": {
    "subregions": {"type": "array", "items": {"type": "object", "properties": {
        "key": {"type": "string"}, "name": {"type": "string"}, "summary": {"type": "string"}},
        "required": ["key", "name", "summary"], "additionalProperties": False}},
    "assignments": {"type": "object", "additionalProperties": {"type": "string"}}},
    "required": ["subregions", "assignments"], "additionalProperties": False}


def cmd_subregions(a):
    db = RoomsDb(DB)
    rooms = db.rooms(a.region)
    name = (db.c.execute("SELECT name FROM regions WHERE key=?", (a.region,)).fetchone() or [a.region])[0]
    slim = [{"key": r["key"], "x": round(r["anchor_x"]), "z": round(r["anchor_z"]), "cls": r["cls"], "area": round(r["area"])}
            for r in rooms]
    exits = [(ra, rb) for ra, rb, in db.c.execute(
        "SELECT room_a, room_b FROM exits WHERE region_key=? AND room_b LIKE ?", (a.region, a.region + ":%"))]
    n = len(rooms)
    target = "2-4" if n < 60 else ("4-8" if n < 200 else "8-15")
    prompt = (f"Region \"{name}\" has {n} rooms. Divide it into {target} named SUB-REGIONS (a player-without-a-map "
              f"level of place: 'the upper galleries', 'the main cavern') and assign EVERY room to one, grouping by "
              f"geography (anchor x,z; z grows south) and connectivity (exits). Names: short noun phrases, unique, "
              f"consistent in voice, no room ids or coordinates. Rooms (key,x,z,cls,area):\n{json.dumps(slim)}\n"
              f"Exits (room_a,room_b):\n{json.dumps(exits)}\n"
              f"Return JSON: subregions=[{{key(lowercase slug),name,summary(one sentence)}}], "
              f"assignments={{room_key: subregion_key}} covering all {n} rooms.")
    try:
        out, _ = or_chat(a.model, [{"role": "user", "content": prompt}], schema=SUB_SCHEMA, max_tokens=8000)
    except CreditsExhausted as e:
        print(f"STOP: OpenRouter credit exhausted ({e})."); sys.exit(42)
    except SafetyBlocked as e:
        print(f"sub-regions safety-blocked ({e}); leaving unassigned (describe still works)."); return
    d = parse_json(out)
    for s in d["subregions"]:
        db.c.execute("INSERT INTO subregions(key,region_key,name,summary) VALUES(?,?,?,?) "
                     "ON CONFLICT(key) DO UPDATE SET name=excluded.name, summary=excluded.summary",
                     (s["key"], a.region, s["name"], s.get("summary")))
    assigned = 0
    for rk, sk in d["assignments"].items():
        assigned += db.c.execute("UPDATE rooms SET subregion_key=? WHERE key=?", (sk, rk)).rowcount
    db.c.commit()
    print(f"{len(d['subregions'])} sub-regions, assigned {assigned}/{n} rooms:",
          ", ".join(f"{s['name']}" for s in d["subregions"]))
    price = MODEL_PRICES.get(a.model, (0, 0))
    print(f"~${(_cost['in']*price[0]+_cost['out']*price[1])/1e6:.5f}")


def main():
    global KEY
    KEY = api_key()
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    for cmd in ("describe", "subregions"):
        s = sub.add_parser(cmd); s.add_argument("region")
        s.add_argument("--model", default="google/gemini-3.7-flash")
        if cmd == "describe":
            s.add_argument("--status", default=None, help="exact status to process; default = all non-verified (resumable)"); s.add_argument("--limit", type=int, default=0); s.add_argument("--workers", type=int, default=8)
        s.set_defaults(fn={"describe": cmd_describe, "subregions": cmd_subregions}[cmd])
    s = sub.add_parser("room"); s.add_argument("key"); s.add_argument("--model", default="google/gemini-3.7-flash"); s.set_defaults(fn=cmd_room)
    a = ap.parse_args()
    a.fn(a)


if __name__ == "__main__":
    main()
