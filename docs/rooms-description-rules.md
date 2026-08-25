# Rules for room titles and descriptions

Decided with the user 2026-08-22 after the first twelve rooms of Devil's Crossing (starting point: wotr-access's
`docs/design/description-rules.md`). The authoring workflow's describer follows these; `tools/author.py check`
enforces the mechanical half.

## What the player hears
- Entering a place: "<region>, <sub-region>, <title>" in the player's voice (only the parts that changed).
- X: the body, through the screen reader. Exits are NOT part of it: V cycles them live with bearings.

## Title
- Two to four words, lowercase, a place name you can navigate by: "gallows stretch", "signpost fork",
  "prison gate", "cell block stairs". Unique within its sub-region. Never repeats the region or sub-region
  name, never a room id, coordinate or size.

## Body
- One sentence, two at most. Under 40 words. This is a Diablo-style game: the body is a glance that lets
  the player recognise the place, not scenery prose.
- Say what the ground is and the one or two fixtures you would know the place by: "A rutted gravel road
  under the prison wall, gibbets along its south side." "A plank bridge on trestles over cattail mud."
- Plain nouns, plain verbs. No thesaurus: no "weathered", "splintered", "mist-filled", "forcing up between",
  "hemmed in". An adjective only when it IS the identifying feature (a red rug, a burning barrel).
- No exits, no neighbours' names, no "ways out", no "the road carries on to". The road or bridge itself
  may be named as a thing that is here.
- No units or NPC names (the review cursor lists them live). No lifecycle state: nothing about chests,
  doors, loot, quest objects, shrines being open/locked/looted. A burning barrel or campfire counts as
  fixed stage only if it is a placed decoration (Decoration records in the facts), not a unit.
- No dimensions, no compass bearings to things outside the room, no invented interiors, nothing the shots
  or facts do not show inside the yellow outline.

## Ground truth
- The overlay shots (yellow dots = this room's outline, red dots = exits, cyan box = the character) and the
  facts from `author.py facts` (terrain fractions, nearby decoration/decal records, region and sub-region).
  Never from memory of the game.

## Consistency
- One voice per region; titles reuse the sub-region's nouns. The same structure seen from two rooms has the
  same name in both.

## Mechanical checks (`author.py check <key>`)
Title 2-6 words, lowercase, no digits (a duplicate title is de-duplicated automatically at describe time with a
trailing " N" suffix -- do not add one yourself); body 1-2 sentences, <= 40 words; no banned words (exit,
exits, way out, ways out, leads, carries on, north/south/east/west-ward phrasing of connections, locked, open,
looted, alive, spawn, quest); no NPC/Monster/Player labels from the facts; no neighbour titles. ("dead" is
accepted for now.)
