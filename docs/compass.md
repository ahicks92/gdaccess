# The game's compass vs. the mod's (2026-09-11)

## The question

The mod pins the camera yaw to 0 so that world -z is screen-up, and calls that north: the four wall tones are
named north / east / south / west along the world axes, and every clock bearing the mod speaks is measured
from screen-up. The player noticed that the game's dialogue ("head north across the bridge", "Burrwitch to
the north, past the swamps of Wightmire", "the Arkovian Foothills to the northwest") does not line up with
those bearings. Is the mod's north the game's north?

## Answer: no, it is 50 degrees off. The game's north is screen-up at the DEFAULT camera (yaw 0.8727 rad).

Measured, not assumed. `tools/compass_fit.py` takes every direction the game's own text gives between two
places the rooms database can locate (Conversations.arc, Quests.arc and the lore notes in Text_EN.arc; 26
anchors across all three acts), computes the true bearing between the places' room centroids under a
candidate north, and scores the disagreement:

| north = screen-up at yaw | rms error | anchors > 45 deg off |
|---|---|---|
| 0 (the mod's pin) | 63 deg | 15 of 26 |
| pi/2 | 51 deg | 11 of 26 |
| 0.8727 (the game's default camera) | 34 deg | 5 of 26 |
| free best fit: 53 deg | 34 deg | 5 of 26 |

The residual 34 deg is the dialogue's own looseness (people say "north" for anything within a quadrant);
the remaining outliers are two Black Legion bounty lines about the far north of Fort Ikon, one Twin Falls
line about nearby ruins (centroids are coarse at 160 units), and the Old Grove, which is cut content.

Under the default camera, Act 1 reads as written: Burrwitch Village, Burial Hill and the prison bridge are
17..40 deg east of north, the Foothills bridge is north-west (293), Hargate's Isle north-west (339),
Homestead north-west (320). Under the mod's yaw-0 pin every one of those is rotated 50 deg clockwise.

There is no compass in the game's data to read instead: `gameengine.dbr` has camera distance and pitch
defaults but no yaw; the world map records carry no orientation; the HUD "compass" (`Player::
GetCurrentCompassState`) is the quest-direction indicator. `GameCamera::ResetToDefaults` is exported and
is the authoritative way to obtain the default yaw at runtime if it is ever wanted.

## Why the mod keeps yaw 0 anyway (decided with the player)

The camera is a real perspective camera (pitch 46 deg, `CameraPitchDefault`) over a world whose geometry
sits on the axis-aligned tile grid; the "isometric" diagonal look sighted players see IS the 50 deg yaw
against that grid. The yaw-0 pin lines the screen up with the grid, which is exactly why walls read
straight in the wall tones and W walks along corridors. Pinning the default yaw would turn every
grid-aligned corridor into a north-east / north-west diagonal, make W cut across corridors (two keys held
to follow a wall) and put the same diagonal wall into two wall-tone lanes at different ranges. The grid is
the better frame for a keyboard player; the dialogue offset is a fixed rotation the player carries instead.

## The rule (player-facing, in the README)

The game's compass words are rotated 50 deg from the mod's clock:

- the game's **north** is your **11 o'clock** (bearing 310);
- its **east** is between **1 and 2 o'clock** (bearing 40);
- its **south** is between **4 and 5 o'clock** (bearing 130);
- its **west** is about **7 o'clock** (bearing 220).

Checks: Bourbon says Burrwitch and Burial Hill are north; from Lower Crossing they sit at 11 o'clock.
Barnabas says the Foothills bridge is north-west; it is at 8 o'clock.

## If the default yaw is ever wanted

Pin `world::pin_camera` to the game's default instead of 0 (read it back after `GameCamera::ResetToDefaults`
rather than hardcoding 0.8727) and rotate the wall-tone lane table (`kDirs` in `screens/in_game.cpp`,
fixed world directions today) into the screen axes. Clock hours, exits, sonar panning and WASD derive from
the live yaw and follow automatically. Room descriptions are unaffected (the authoring rules forbid compass
words). Leave it off by default for the reasons above.
