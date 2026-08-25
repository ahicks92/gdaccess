// The rooms authoring workflow (docs/rooms.md M5). Run with the Workflow tool:
//   Workflow({ scriptPath: 'tools/workflows/rooms_author.js', args: { region: 'devilscrossing', rooms: [...keys], subregions: false } })
// Every agent is Opus explicitly (the user's Fable limit): the work is images and prose. Agents read and
// write ONLY through `uv run tools/author.py` (facts, describe, check, subregion, assign) and the shots under
// build/shots; the rules are docs/rooms-description-rules.md. One agent per room: it describes, runs the
// mechanical check itself and fixes until it passes; an independent Opus reviewer reads every 10th room.
export const meta = {
  name: 'rooms-author',
  description: 'Assign sub-regions for a region, then title + describe (self-checked) its shot rooms',
  phases: [
    { title: 'Sub-regions', detail: 'one Opus agent over the floor plan + rooms.json', model: 'opus' },
    { title: 'Describe', detail: 'one Opus agent per room: facts + shots -> title/body, self-checked', model: 'opus' },
    { title: 'Review', detail: 'an Opus reviewer on every 10th room', model: 'opus' },
  ],
}

const ROOT = 'D:/projects/in_progress/gdaccess'
const region = args.region
const rooms = args.rooms || []
const RULES = `${ROOT}/docs/rooms-description-rules.md`
const CLI = `cd ${ROOT} && export PYTHONIOENCODING=utf-8 && uv run tools/author.py`

const COMMON = `You are working on GD Access, a screen-reader mod for Grim Dawn (an ARPG). Blind players hear a
place as "<region>, <sub-region>, <room title>" and press X for a one-or-two-sentence description. Read the
rules first: ${RULES}. All reads and writes go through the CLI "${CLI} ..." (Bash tool). Never edit the
database or any file directly. Region key: ${region}.`

const SUB_SCHEMA = {
  type: 'object',
  properties: {
    subregions: { type: 'array', items: { type: 'object', properties: { key: { type: 'string' }, name: { type: 'string' }, rooms: { type: 'integer' } }, required: ['key', 'name', 'rooms'] } },
    unassigned: { type: 'integer' },
    notes: { type: 'string' },
  },
  required: ['subregions', 'unassigned'],
}
const DESC_SCHEMA = {
  type: 'object',
  properties: {
    key: { type: 'string' }, title: { type: 'string' }, body: { type: 'string' }, passed: { type: 'boolean' }, problem: { type: 'string' },
    shots_suspect: { type: 'boolean' }, shots_problem: { type: 'string' },
  },
  required: ['key', 'title', 'body', 'passed', 'shots_suspect'],
}
const REVIEW_SCHEMA = {
  type: 'object',
  properties: { key: { type: 'string' }, ok: { type: 'boolean' }, reasons: { type: 'array', items: { type: 'string' } } },
  required: ['key', 'ok', 'reasons'],
}

if (args.subregions) {
  phase('Sub-regions')
  const r = await agent(`${COMMON}
Task: divide the region into named SUB-REGIONS (5-15 for a large overworld region; a small dungeon of a
few dozen rooms may need only 2-4, e.g. one per level) and assign EVERY room to one. A sub-region is the
level of place a player without the map plans a route by ("the prison", "the north road", "the graveyard");
names are short noun phrases, consistent in voice, unique, no room ids or coordinates.
Inputs: run "${CLI} export ${region}" and read the JSON it writes (rooms with plan_id, anchor_x/anchor_z in
world units (z grows south, north is -z), area, class, bbox, exits between room keys, and for photographed
rooms: terrain fractions and landmark labels). Look at the floor plan PNG named in the JSON (ids at each
room's anchor; north up) with the Read tool; crop it with a short Python script (uv run python -c ...) if
the ids are too small. Group by connectivity and geography: rooms inside one walled structure, one road
stretch between junctions, one field. Islands (island=1) join the nearest group.
The game's own area names are the best sub-region names where they fit: the minimap label in the TOP-RIGHT
corner of a shot names the game's area at that spot (open "${ROOT}/build/shots/${region}/<key with : as
_>/00_overlay.png" for one room per cluster you are unsure of). Prefer those names over invented ones.
Write results: "${CLI} subregion ${region} <key> --name \\"<Name>\\" --summary \\"<one sentence>\\"" per
sub-region (key = lowercase slug), then "${CLI} assign <room_key> <subregion_key>" for every room (batch
them in one Bash call). Finish with "${CLI} status ${region}" and report the sub-regions with room counts
and the number of unassigned rooms (must be 0).`,
    { model: 'opus', label: 'subregions', phase: 'Sub-regions', schema: SUB_SCHEMA })
  log(`sub-regions: ${r ? r.subregions.map(s => `${s.name} (${s.rooms})`).join(', ') : 'agent failed'}; unassigned ${r ? r.unassigned : '?'}`)
}

function describePrompt(key) {
  return `${COMMON}
Task: write the TITLE and BODY for room ${key}, then make the mechanical check pass.
1. Run "${CLI} facts ${key}": region and sub-region names, terrain fractions under the room, "nearby" =
   the fixtures under/around the room (decoration/decal record names + any NPC/monster label), deduped and
   nearest-first (e.g. gibbetsuspended_1a), and the shot paths.
2. Look at each shot with the Read tool (yellow dots = this room's outline, red dots = exits, cyan box =
   the character). Only what is inside the outline counts.
3. Write per the rules: title 2-6 lowercase words; body ONE sentence, two at most, under 40 words, plain
   nouns and verbs, the ground plus the one or two fixtures you would recognise the place by. NO exits, NO
   neighbours, NO "ways out", no adjective soup, no units, no lifecycle state.
   Examples of the wanted size: "A rutted gravel road under the prison wall, gibbets along its south side."
   "A plank bridge on trestles over cattail mud." "A rubble lane between canvas tents with a campfire."
4. Save: "${CLI} describe ${key} --title \\"...\\" --body \\"...\\"" (no inner double quotes), then run
   "${CLI} check ${key}". If it prints FAIL, fix exactly what it lists and save + check again (up to 3
   rounds). Return key, title, body, passed (true only if the last check printed ok), problem otherwise.
Set shots_suspect=true (with shots_problem saying why) when the images themselves look wrong for the job:
a black or near-black frame, a game window or menu drawn over the room, the outline mostly off-screen, a
death/respawn screen, or a scene that contradicts the terrain facts. Still write the best title/body you
can from what is usable; the suspect rooms get re-shot and re-described.`
}

function reviewPrompt(key, title, body) {
  return `${COMMON}
Task: REVIEW room ${key} against the rules as a reader would. Title: "${title}". Body: "${body}".
Run "${CLI} facts ${key}" and look at one shot with the Read tool. Is the body a plain one-or-two-sentence
glance that matches what is inside the outline, with no exits/neighbours/adjective soup/units/state, and is
the title a usable place name? If not, say exactly what is wrong. Do not edit anything. Return ok and reasons.`
}

// Consistency pass (added 2026-08-22 after the per-room reviewer rejected 8 of 19 sampled rooms for one class of
// problem: a describer never sees its neighbours, so adjacent rooms came out as "fern grass slope" / "open grass
// slope", "cobbled wagon track" / "rutted cart track", bodies naming the next room's shack, the same well described
// twice with different materials). One Opus agent per sub-region reads every title and body in it plus the rooms
// across its exits, and rewrites the confusable ones. args: { consistency: true, subregions_keys: [...], notes: [...] }
const CONS_SCHEMA = {
  type: 'object',
  properties: {
    subregion: { type: 'string' },
    rewritten: { type: 'array', items: { type: 'object', properties: { key: { type: 'string' }, title: { type: 'string' }, body: { type: 'string' } }, required: ['key', 'title', 'body'] } },
    notes: { type: 'string' },
  },
  required: ['subregion', 'rewritten'],
}
// DEFERRED by default (decided 2026-08-24): the consistency pass was ~21% of authoring cost and ~79% of its
// output was extended thinking over a whole sub-region. We accept less-distinct titles now (duplicates get a
// mechanical " N" suffix at describe time) and polish confusable names later. Only run with consistency:true
// for a deliberate late polish pass on a finished region.
if (args.consistency) {
  phase('Consistency')
  const subs = args.subregion_keys || []
  const notes = (args.notes || []).join('\n')
  const cons = await parallel(subs.map(sub => () => agent(`${COMMON}
Task: make the titles and bodies of sub-region "${sub}" CONSISTENT AND DISTINGUISHABLE when heard in sequence by
a blind player walking through it. Every room already has a title and body that pass the mechanical check; your
job is the editorial layer the per-room writers could not do because they never saw their neighbours.
Inputs: run "${CLI} export ${region}" and read the JSON it writes: rooms (key, subregion_key, title, body, area,
cls, bbox, terrain fractions, landmarks) and exits (room_a, room_b). Your rooms are those with subregion_key
"${sub}"; also read the rooms in OTHER sub-regions that share an exit with one of yours (boundary pairs).
Find and fix, in this order:
1. Confusable titles: two rooms that share an exit, or are in the same sub-region, whose titles differ by one
   word or are synonyms ("fern grass slope" / "open grass slope" / "fern slope"; "cobbled wagon track" /
   "rutted cart track"; "cattail mud pocket" / "cattail mud lane"). Rename so each title names what is distinct
   about THAT room (a fixture, a structure, a position: "the gap between the outcrops", "store back room"). Keep
   the sub-region's naming convention (e.g. "<thing> floor" for house interiors) and the rules' 2-6 lowercase words.
2. Bodies that name a fixture belonging to a neighbour (the next room's shack, the well that is the identifying
   feature of the room next door) or that duplicate the neighbour's body. Replace with something inside the room.
   When unsure what is inside, run "${CLI} facts <key>" and look at ONE shot with the Read tool (only what is
   inside the yellow outline counts) -- do this only for rooms you are changing.
3. The same structure seen from two rooms must be named the same way in both (one well, one name, one material).
4. Reviewer notes on specific rooms (act on the ones in your sub-region, ignore the rest):
${notes}
Do NOT rewrite rooms that are fine; most rooms should be untouched. Keep bodies one sentence, two at most, under
40 words, plain nouns, no exits, no neighbours, no adjective soup. Save every change with
"${CLI} describe <key> --title \\"...\\" --body \\"...\\"" (no inner double quotes) and then run "${CLI} check <key>"
and fix until it prints ok. Return subregion, the list of rooms you rewrote (key, title, body) and short notes.`,
    { model: 'opus', label: `consistency:${sub}`, phase: 'Consistency', schema: CONS_SCHEMA })))
  const changed = cons.filter(Boolean).flatMap(c => c.rewritten.map(r => `${c.subregion} ${r.key}: ${r.title} -- ${r.body}`))
  log(`consistency: ${changed.length} rooms rewritten across ${cons.filter(Boolean).length} sub-regions`)
  if (!rooms.length) return { rewritten: changed, notes: cons.filter(Boolean).map(c => `${c.subregion}: ${c.notes || ''}`) }
}

// Auto-list the region's shot rooms when no explicit `rooms` were passed (so a whole region is just
// {region, subregions:true}). Self-healing: any keys the lister misses stay status 'shot' and are picked up
// on a re-run. --keys-only prints one key per line for a clean parse.
let roomKeys = rooms
if (!roomKeys.length && region) {
  const lister = await agent(`${COMMON}
Run EXACTLY this one command and nothing else: ${CLI} list ${region} --status shot --keys-only
Return every line it prints (each is a room key like ${region}:-703:-1184) as the "keys" array, in order,
omitting none. Do not invent, reformat, or drop any.`,
    { model: 'opus', effort: 'low', label: 'list-shot', schema: { type: 'object', properties: { keys: { type: 'array', items: { type: 'string' } } }, required: ['keys'] } })
  roomKeys = (lister && lister.keys) || []
  log(`auto-listed ${roomKeys.length} shot rooms for ${region}`)
}

phase('Describe')
const results = await pipeline(
  roomKeys,
  key => agent(describePrompt(key), { model: 'opus', effort: 'low', label: `describe:${key.split(':').slice(1).join(':')}`, phase: 'Describe', schema: DESC_SCHEMA }),
  async (d, key, index) => {
    if (!d) return { key, passed: false, problem: 'describer failed' }
    if (d.passed && index % 10 === 0) {
      const v = await agent(reviewPrompt(key, d.title, d.body), { model: 'opus', label: `review:${key.split(':').slice(1).join(':')}`, phase: 'Review', schema: REVIEW_SCHEMA })
      return { ...d, review: v }
    }
    return d
  },
)
const done = results.filter(Boolean)
log(`${done.filter(r => r.passed).length}/${roomKeys.length} rooms passed the check; ${done.filter(r => r.shots_suspect).length} flagged suspect shots`)
return {
  passed: done.filter(r => r.passed).map(r => `${r.key}: ${r.title} -- ${r.body}`),
  failed: done.filter(r => !r.passed).map(r => `${r.key}: ${r.problem || ''}`),
  suspect_shots: done.filter(r => r.shots_suspect).map(r => `${r.key}: ${r.shots_problem || ''}`),
  reviews: done.filter(r => r.review).map(r => ({ key: r.key, ok: r.review.ok, reasons: r.review.reasons })),
}
