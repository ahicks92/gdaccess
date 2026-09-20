"""Describe every shot room region by region through OpenRouter (tools/describe_or.py), biggest region first:
sub-regions once for a region that has none, then the shot rooms, then the re-homed (status described) rooms.
Stops on exit 42 (credit exhausted) -- add credit and re-run; the db keeps every room already written.

  uv run tools/describe_all.py [--workers 32] [--only region]"""
import argparse
import os
import sqlite3
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DB = os.path.join(ROOT, "build", "rooms", "rooms.db")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--workers", type=int, default=32)
    ap.add_argument("--only", default=None)
    a = ap.parse_args()
    c = sqlite3.connect(DB)
    regions = [k for (k,) in c.execute("SELECT region_key FROM rooms WHERE status IN ('shot','described') "
                                       "GROUP BY region_key ORDER BY COUNT(*) DESC")]
    if a.only:
        regions = [r for r in regions if r == a.only]
    tool = os.path.join(ROOT, "tools", "describe_or.py")
    for r in regions:
        print(f"### {r} {time.strftime('%H:%M')}", flush=True)
        if c.execute("SELECT COUNT(*) FROM subregions WHERE region_key=?", (r,)).fetchone()[0] == 0:
            rc = subprocess.call([sys.executable, tool, "subregions", r])
            if rc == 42:
                print("### STOPPED: credit exhausted (sub-regions)", flush=True); sys.exit(42)
        for st in ("shot", "described"):
            rc = subprocess.call([sys.executable, tool, "describe", r, "--status", st, "--workers", str(a.workers)])
            if rc == 42:
                print(f"### STOPPED: credit exhausted at {r}", flush=True); sys.exit(42)
    print(f"### ALL DONE {time.strftime('%H:%M')}", flush=True)


if __name__ == "__main__":
    main()
