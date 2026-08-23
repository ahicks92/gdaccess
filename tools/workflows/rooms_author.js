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
  properties: { key: { type: 'string' }, title: { type: 'string' }, body: { type: 'string' }, passed: { type: 'boolean' }, problem: { type: 'string' } },
  required: ['key', 'title', 'body', 'passed'],
}
const REVIEW_SCHEMA = {
  type: 'object',
  properties: { key: { type: 'string' }, ok: { type: 'boolean' }, reasons: { type: 'array', items: { type: 'string' } } },
  required: ['key', 'ok', 'reasons'],
}

if (args.subregions) {
  phase('Sub-regions')
  const r = await agent(`${COMMON}
Task: divide the region into 5-15 named SUB-REGIONS and assign EVERY room to one. A sub-region is the
level of place a player without the map plans a route by ("the prison", "the north road", "the graveyard");
names are short noun phrases, consistent in voice, unique, no room ids or coordinates.
Inputs: run "${CLI} export ${region}" and read the JSON it writes (rooms with plan_id, anchor_x/anchor_z in
world units (z grows south, north is -z), area, class, bbox, exits between room keys, and for photographed
rooms: terrain fractions and landmark labels). Look at the floor plan PNG named in the JSON (ids at each
room's anchor; north up) with the Read tool; crop it with a short Python script (uv run python -c ...) if
the ids are too small. Group by connectivity and geography: rooms inside one walled structure, one road
stretch between junctions, one field. Islands (island=1) join the nearest group.
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
1. Run "${CLI} facts ${key}": region and sub-region names, terrain fractions under the room, nearby
   decoration/decal records per sample point (e.g. Decoration gibbetsuspended_1a), the shot paths.
2. Look at each shot with the Read tool (yellow dots = this room's outline, red dots = exits, cyan box =
   the character). Only what is inside the outline counts.
3. Write per the rules: title 2-4 lowercase words; body ONE sentence, two at most, under 40 words, plain
   nouns and verbs, the ground plus the one or two fixtures you would recognise the place by. NO exits, NO
   neighbours, NO "ways out", no adjective soup, no units, no lifecycle state.
   Examples of the wanted size: "A rutted gravel road under the prison wall, gibbets along its south side."
   "A plank bridge on trestles over cattail mud." "A rubble lane between canvas tents with a campfire."
4. Save: "${CLI} describe ${key} --title \\"...\\" --body \\"...\\"" (no inner double quotes), then run
   "${CLI} check ${key}". If it prints FAIL, fix exactly what it lists and save + check again (up to 3
   rounds). Return key, title, body, passed (true only if the last check printed ok), problem otherwise.`
}

function reviewPrompt(key, title, body) {
  return `${COMMON}
Task: REVIEW room ${key} against the rules as a reader would. Title: "${title}". Body: "${body}".
Run "${CLI} facts ${key}" and look at one shot with the Read tool. Is the body a plain one-or-two-sentence
glance that matches what is inside the outline, with no exits/neighbours/adjective soup/units/state, and is
the title a usable place name? If not, say exactly what is wrong. Do not edit anything. Return ok and reasons.`
}

phase('Describe')
const results = await pipeline(
  rooms,
  key => agent(describePrompt(key), { model: 'opus', label: `describe:${key.split(':').slice(1).join(':')}`, phase: 'Describe', schema: DESC_SCHEMA }),
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
log(`${done.filter(r => r.passed).length}/${rooms.length} rooms passed the check`)
return {
  passed: done.filter(r => r.passed).map(r => `${r.key}: ${r.title} -- ${r.body}`),
  failed: done.filter(r => !r.passed).map(r => `${r.key}: ${r.problem || ''}`),
  reviews: done.filter(r => r.review).map(r => ({ key: r.key, ok: r.review.ok, reasons: r.review.reasons })),
}
