# Sonar cue loudness (2026-08-26)

The sonar (src/sonar.cpp) plays one file per kind from `assets/audio/interactables/` and applies a per-kind dB
trim (`kDefaultTrimDb`, live `/sonar?trim=<kind>,<dB>`, `trim=all,0` = defaults, `all,1` = flat) so the cues sit at
one perceived level without editing the files. The measure is `tools/loudness.py`: ITU-R BS.1770 K-weighting
(mono, resampled to 48 kHz, ungated -- these are one-shots), plus A-weighted RMS (the wall tones' older measure)
and true peak, and it prints the trim to the reference and the headroom left after it.

Cues and measurements (reference = units-enemy, -13.9 LKFS):

- enemy `units-enemy.wav` -13.9, trim 0
- loot / restored shrine `unknown.wav` -18.2, trim +4.3 (4.1 dB headroom left)
- transition `transition.wav` (se_old_pack00 door05) -22.5, wants +8.6 but peaks at -1.6 dBFS: trim capped at +1.5,
  so it stays ~7 dB under the others by the meter. To meet it instead, lower everything (reference = the door).
- destructible `destructible.wav` (push33) -12.6, trim -1.3
- ruined / desecrated shrine `shrine-ruined.wav` (push17) -12.7 (peak +0.4 dBFS raw), trim -1.2
- interactable `interactable.wav` (se_old_pack00 buble05, added 2026-08-28) -18.6, trim +4.7 (0.8 dB headroom left)

Kinds: enemies (`ScanGroup::Enemies`), loot, dungeon entrances (Transitions), live breakables
(`ScanGroup::Destructibles`, the same predicate as the B group's destructibles), devotion shrines
(`ScanGroup::Shrines` = is-a `StaticShrine`; restored = `StaticShrine::IsCleansed`, state 6, the one branch its
`GetGameDescription` takes), and interactables (`ScanGroup::Interactables`: the rest of the N group's objects of
interest -- `IsOfInterest` and not loot / dungeon entrance / shrine / breakable; NPCs excluded -- doors, levers,
riftgates, lore notes, graves).
