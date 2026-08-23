# Rules for room titles and descriptions

Ported from wotr-access's `docs/design/description-rules.md` (accepted as the starting point 2026-08-22); the
Grim Dawn specifics are marked. These are the rules the authoring workflow's describer and verifier follow.

## What a room entry is
- **Title**: a short place name a blind player can navigate by, 2-5 words, in the vocabulary of the sub-region
  it belongs to ("the prison yard", "cell block corridor", "the road north of the gate"). Unique within
  its sub-region; no room ids, no coordinates, no dimensions. Spoken as "Devil's Crossing, the prison,
  cell block corridor", so the title must not repeat the region or sub-region name.
- **Body**: 2-4 sentences of prose, spoken on the X key. What the place is, what it looks like, what is
  permanently here, and how it connects ("A cobbled yard inside the prison wall; the road runs through it
  north to south, a gibbet and a brazier stand by the gate."). No "this room", no "you are in".

## Ground truth
- From the live game only: the shots (`build/shots/<region>/<room>/NN_overlay.png`, the yellow dots are the
  room's outline, red dots its exits with the neighbour's key, the cyan box the character) and the facts in
  `author.py facts` (terrain layers under the room with fractions, nearby entities by class and record name,
  exits with bearings and neighbours' titles). Never from memory of the game or the wiki.
- Every claim needs to be visible in a shot of THIS room (inside the yellow outline) or present in its facts.
  The camera shows a wide area: things outside the outline belong to a neighbour, say so only as "beyond the
  wall" context if at all. Directions from the facts' bearings, never eyeballed from the picture.

## In
- The permanent stage: ground and its material (the terrain fractions: "sparse rocky grass with a gravel
  road", "fieldstone walk", "brick rubble floor"), walls, buildings, fences, gates, stairs, bridges, ruins,
  furniture, carts, barrels, crates, tents, lanterns, torches, braziers, decals (road tracks, scattered
  papers, bloodstains), trees and bushes, water, mud, light and atmosphere.
- Scanner-invisible flavour that is permanent: the road's ruts, a hanging cage, the smell of smoke if the
  braziers are there. Unflinching detail where the game is grim (gibbets, corpses that are set dressing).
- Connections: where the road goes, which way the gate is, what the stairs lead to (from the exits).

## Out
- **Units**: NPCs, monsters, the player, pets, their names and what they do (the review cursor lists them
  live). Exception: a named NPC who defines the place may be referred to as a fixture only through their
  stall or post ("the smith's anvil"), never by presence.
- **Anything with lifecycle state**: loot, chests (open/closed), doors (locked/open), quest objects, shrines'
  state, traps, destructibles, spawns. A chest "sits here forever" in prose after it was looted. Mention the
  structure ("a shrine platform") not the state.
- Invented things, guesses about what is inside buildings, dimensions ("about 30 by 20"), compass
  bearings to things outside the room, game mechanics.

## Consistency
- One voice for the whole region. Sub-region names are fixed by the sub-region pass; titles reuse their
  nouns ("the prison" -> "prison yard", "cell block", "prison gate").
- The same structure seen from two rooms gets the same name in both.
- Untitled-room fallbacks ("room 193") are not prose and must not leak into titles.

## Process notes (Grim Dawn)
- Shots are taken at the far zoom, north up, 45 degrees; a building's interior is often hidden under its
  roof; say what is visible, do not guess the interior.
- The first samples of a room are its centre and its far ends; a long room's shots overlap its neighbours.
- Verify mode checks: title present and unique within the sub-region, no unit names from the facts in the
  text, no state words (locked, open, looted, dead), no numbers with units, body 2-4 sentences.
