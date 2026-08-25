#!/bin/bash
# Overnight Act 1 authoring driver: per region -> shoot -> subregions -> describe (gemini-3.7-flash via
# OpenRouter, 32 workers). The db file on disk is the checkpoint (resumable); no per-region git commits
# (a 40 MB binary committed 80x would bloat the repo -- commit once at the end).
# HARD RULES (see memory act1-authoring-openrouter): OpenRouter only, never the Opus workflow; stop on credit
# exhaustion (describe_or exits 42); safety-blocked rooms are skipped+logged by describe_or; test char only.
# Stops (does NOT auto-recover) if the game is not in-world as 'test' -- never risks the user's 'real' save.
cd /d/projects/in_progress/gdaccess || exit 1
DEV=http://127.0.0.1:8791
WORKERS=32
regions=$(uv run python -c "import json;print('\n'.join(json.load(open('build/rooms/worklist.json'))))" | tr -d '\r')   # Python stdout is CRLF on Windows; a trailing \r would break every region-key match

inworld_test(){ p=$(curl -s --max-time 8 "$DEV/player"); echo "$p" | grep -q "name='test'" && ! echo "$p" | grep -q "world=0x0"; }
# region passed as argv[1] (never interpolated into the SQL) to avoid quote clashes with `python -c "..."`
todo_count(){ uv run python -c "import sqlite3,sys;c=sqlite3.connect('assets/rooms.db');print(sum(1 for r in c.execute('SELECT status FROM rooms WHERE region_key=?',(sys.argv[1],)) if r[0] not in ('verified','blocked')))" "$1"; }
unseen_count(){ uv run python -c "import sqlite3,sys;c=sqlite3.connect('assets/rooms.db');print(sum(1 for r in c.execute('SELECT status FROM rooms WHERE region_key=?',(sys.argv[1],)) if r[0]=='unseen'))" "$1"; }
has_sub(){ uv run python -c "import sqlite3,sys;c=sqlite3.connect('assets/rooms.db');print(c.execute('SELECT COUNT(*) FROM subregions WHERE region_key=?',(sys.argv[1],)).fetchone()[0])" "$1"; }
status_line(){ uv run python -c "import sqlite3,sys;c=sqlite3.connect('assets/rooms.db');print(dict(c.execute('SELECT status,COUNT(*) FROM rooms WHERE region_key=? GROUP BY status',(sys.argv[1],)).fetchall()))" "$1"; }

echo "=== ACT1 RUN START $(date) ==="
for rk in $regions; do
  [ "$(todo_count "$rk")" = "0" ] && { echo "SKIP $rk (already done)"; continue; }
  if ! inworld_test; then echo "STOP: game not in-world as 'test' before $rk -- banking progress, resume after loading test char. $(date)"; break; fi

  echo "=== SHOOT $rk $(date) ==="
  uv run tools/shots.py region "$rk" 2>&1 | tail -1
  if [ "$(unseen_count "$rk")" -gt 3 ]; then    # one retry if the tour under-shot (transient)
    inworld_test && { echo "  reshoot $rk (unseen remained)"; uv run tools/shots.py region "$rk" 2>&1 | tail -1; }
  fi

  [ "$(has_sub "$rk")" = "0" ] && { echo "=== SUBREGIONS $rk ==="; uv run tools/describe_or.py subregions "$rk" 2>&1 | tail -1; sc=${PIPESTATUS[0]}; [ "$sc" = "42" ] && { echo "STOP: credits at subregions $rk"; break; }; }

  echo "=== DESCRIBE $rk $(date) ==="
  uv run tools/describe_or.py describe "$rk" --workers "$WORKERS" 2>&1 | tail -5
  code=${PIPESTATUS[0]}
  [ "$code" = "42" ] && { echo "STOP: OpenRouter credits exhausted at $rk. $(date)"; break; }
  echo "=== DONE $rk : $(status_line "$rk") $(date) ==="
done
echo "=== ACT1 RUN ENDED $(date) ==="
