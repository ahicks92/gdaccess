// The rooms authoring workflow (docs/rooms.md M5). Run with the Workflow tool:
//   Workflow({ scriptPath: 'tools/workflows/rooms_author.js', args: { region: 'devilscrossing', rooms: [...keys], subregions: true } })
// Every agent is Opus explicitly (the user's Fable limit): the work is images and prose. Agents read and
// write ONLY through `uv run tools/author.py` (facts, describe, verify, subregion, assign) and the shots under
// build/shots; the rules are docs/rooms-description-rules.md.
export const meta = {
  name: 'rooms-author',
  description: 'Assign sub-regions for a region, then title + describe + verify its shot rooms',
  phases: [
    { title: 'Sub-regions', detail: 'one Opus agent over the floor plan + rooms.json', model: 'opus' },
    { title: 'Describe', detail: 'one Opus agent per room from its shots and facts', model: 'opus' },
    { title: 'Verify', detail: 'one Opus agent per room against the rules; one fix round', model: 'opus' },
  ],
}

const ROOT = 'D:/projects/in_progress/gdaccess'
const region = args.region
const rooms = args.rooms || []
const RULES = `${ROOT}/docs/rooms-description-rules.md`
const CLI = `cd ${ROOT} && uv run tools/author.py`

const COMMON = `You are working on GD Access, a screen-reader mod for Grim Dawn. Blind players hear a place as
"<region>, <sub-region>, <room title>" and press X for a prose description. Read the rules first:
${RULES}. All reads and writes go through the CLI "${CLI} ..." (run it with the Bash tool; use
PYTHONIOENCODING=utf-8). Never edit the database or any file directly. Region key: ${region}.`

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
  properties: { key: { type: 'string' }, title: { type: 'string' }, body: { type: 'string' }, written: { type: 'boolean' }, problem: { type: 'string' } },
  required: ['key', 'title', 'body', 'written'],
}
const VERDICT_SCHEMA = {
  type: 'object',
  properties: { key: { type: 'string' }, ok: { type: 'boolean' }, reasons: { type: 'array', items: { type: 'string' } } },
  required: ['key', 'ok', 'reasons'],
}

if (args.subregions) {
  phase('Sub-regions')
  const r = await agent(`${COMMON}
Task: divide the region into 5-15 named SUB-REGIONS and assign EVERY room to one. A sub-region is the
level of place a player without the map plans a route by ("the prison", "the road north of the gate",
"the graveyard"); names are short noun phrases, consistent in voice, unique, no room ids or coordinates.
Inputs: run "${CLI} export ${region}" and read the JSON it writes (rooms with plan_id, anchor_x/anchor_z in
world units (z grows south, north is -z), area, class, bbox, exits between room keys, and for rooms that
have been photographed: terrain fractions and landmark labels). Look at the floor plan PNG named in the JSON
(ids at each room's anchor; north up) with the Read tool; you may crop it with a short Python script
(uv run python -c ...) if the ids are too small. Group by connectivity and geography: rooms inside one
walled structure, one road stretch between junctions, one field. Islands (island=1) join the nearest group.
Write results: for each sub-region "${CLI} subregion ${region} <key> --name \\"<Name>\\" --summary \\"<one sentence>\\""
(key = lowercase slug like prison, north_road), then "${CLI} assign <room_key> <subregion_key>" for every
room (202 calls is fine; batch them in one Bash call with && or a loop). Finish with "${CLI} status ${region}"
and report: the sub-regions with their room counts, the number of unassigned rooms (must be 0), and notes.`,
    { model: 'opus', label: 'subregions', phase: 'Sub-regions', schema: SUB_SCHEMA })
  log(`sub-regions: ${r ? r.subregions.map(s => `${s.name} (${s.rooms})`).join(', ') : 'agent failed'}; unassigned ${r ? r.unassigned : '?'}`)
}

function describePrompt(key, retryReason) {
  return `${COMMON}
Task: write the TITLE and BODY for room ${key}.
1. Run "${CLI} facts ${key}" and read the JSON: region name, sub-region name, terrain fractions under the
   room, the labelled entities and stage records near each sample point (class + record name, e.g.
   Decoration gibbetsuspended_1a, Decal ambdecal_roadtracksstraight01), the exits with bearings and the
   neighbours' titles, and the overlay shot paths.
2. Look at EVERY overlay shot with the Read tool. Yellow dots = this room's outline; red dots = exits with
   the neighbour's key; cyan box = the character. Describe only what is inside the outline; the rest is
   context at most.
3. Follow the rules file strictly (no units/NPC names, no lifecycle state, no dimensions, no invented
   interiors, title 2-5 words unique in its sub-region and not repeating the region/sub-region name, body 2-4
   sentences, one voice).
4. Write it: "${CLI} describe ${key} --title \\"...\\" --body \\"...\\"" (quote carefully; avoid inner
   double quotes). Confirm the CLI printed ok.${retryReason ? `
This is a retry; the verifier rejected the previous attempt: ${retryReason}. Fix exactly that.` : ''}
Return key, title, body, written=true (or written=false with the problem).`
}

function verifyPrompt(key, title, body) {
  return `${COMMON}
Task: VERIFY the description of room ${key} against the rules. Title: "${title}". Body: "${body}".
Run "${CLI} facts ${key}" for the facts, and look at at least one overlay shot with the Read tool. Check:
title 2-5 words, no room id/coordinates/dimensions, does not repeat the region or sub-region name;
body 2-4 sentences; no unit or NPC names (compare with the facts' Npc/Monster/Player labels); no lifecycle
state words (locked, open, looted, dead, alive, spawn); nothing described that is not visible inside the
outline or present in the facts; directions consistent with the exits' bearings. If it passes run
"${CLI} verify ${key}"; if not run "${CLI} verify ${key} --fail \\"<reason>\\"". Return ok and the reasons.`
}

phase('Describe')
const results = await pipeline(
  rooms,
  key => agent(describePrompt(key), { model: 'opus', label: `describe:${key.split(':').slice(1).join(':')}`, phase: 'Describe', schema: DESC_SCHEMA }),
  async (d, key) => {
    if (!d || !d.written) return { key, ok: false, reasons: [d ? d.problem || 'not written' : 'describer failed'], title: d && d.title }
    const v = await agent(verifyPrompt(key, d.title, d.body), { model: 'opus', label: `verify:${key.split(':').slice(1).join(':')}`, phase: 'Verify', schema: VERDICT_SCHEMA })
    if (!v || v.ok) return { key, ok: !!v, reasons: v ? [] : ['verifier failed'], title: d.title }
    const d2 = await agent(describePrompt(key, v.reasons.join('; ')), { model: 'opus', label: `redo:${key.split(':').slice(1).join(':')}`, phase: 'Describe', schema: DESC_SCHEMA })
    if (!d2 || !d2.written) return { key, ok: false, reasons: v.reasons, title: d.title }
    const v2 = await agent(verifyPrompt(key, d2.title, d2.body), { model: 'opus', label: `verify2:${key.split(':').slice(1).join(':')}`, phase: 'Verify', schema: VERDICT_SCHEMA })
    return { key, ok: !!(v2 && v2.ok), reasons: v2 ? v2.reasons : ['verifier failed'], title: d2.title }
  },
)
const done = results.filter(Boolean)
log(`${done.filter(r => r.ok).length}/${rooms.length} rooms verified`)
return { verified: done.filter(r => r.ok).map(r => `${r.key}: ${r.title}`), rejected: done.filter(r => !r.ok) }
