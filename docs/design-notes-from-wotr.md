# Design notes carried over from wotr-access

Condensed from a full read of `../wotr-access` (C#/Unity, Pathfinder: WotR) on 2026-08-21. These are the
decisions to reuse in the C++ Grim Dawn mod; game-specific mechanics are omitted.

## Architecture
- Host/module split for hot reload: a tiny loader that never changes, and a feature module that can be
  rebuilt and swapped without restarting the game. Dispose must undo every hook, subscription and audio
  output, or reloads produce doubled speech. (Here: eject/re-inject of the whole DLL serves the same purpose
  for now; a stub/module split is the upgrade if DllMain teardown proves fragile.)
- Frame loop order is explicit and documented: localization poll, focus-mode reacquire, input tick, wizard
  check, screen manager, typeahead, control-state, world model, listener frame, overlays, spatial sources,
  event flush. Use unscaled time; the cursor must move while the game is paused.
- An engine-free core (nav graph, announcer, typeahead, room segmentation, DSP) with real unit tests; the
  boundary enforced by the build.

## Speech
- Never interrupt by default. Interrupt only on focus moves, synchronous state feedback after an action,
  buffer line navigation, facing announcements, text-entry echo.
- The user picks an output *method* (auto / a named screen reader / SAPI / clipboard), not a library.
- One live backend per configuration; switching is a lookup, never a teardown; acquisition failures are
  cached; never strand the user with no voice. F8 = panic reset to defaults.
- Rendered speech (PCM) goes through the mod's own mixer so combat readouts overlap rather than queue.
- Strip markup at the output boundary; drop empties before and after stripping.

## Input
- Every user-facing action is a registered, rebindable InputAction; raw key polling only in sanctioned spots.
- Polled per frame; press-driven handlers fire on JustPressed, poll-driven ones read Held (held diagonals
  combine into one vector).
- Modifiers match exactly (Ctrl+A does not fire bare A). Left/right variants OR'd. Modifier keys unbindable.
- Key repeat at the user's OS typematic settings (SPI_GETKEYBOARDDELAY/SPEED); only an action that was
  JustPressed in this hold may repeat; diagonals stretched by sqrt(2).
- Input categories with chord shadowing: each screen declares categories in priority order; the per-frame
  walk marks bindings live and a higher-ranked identical chord shadows a lower one. Cross-category duplicate
  chords are legal by design (same arrows navigate the HUD when focused and move the cursor when not).
- Focus mode holds a reversible first-party "mute game hotkeys" lever (here: `GetNumKeyEvents` returning 0),
  reacquired whenever the game rebuilds its input object. Starts ON at boot.
- Rebinding: conflicts within a category steal and announce; escape/modifiers reserved; migrate only
  bindings that still equal the old default.

## UI navigation
- A Screen is an activity predicate + layer + immediate-mode graph build + string-id actions + policy
  flags. Push/focus are split so covered screens keep their state. Child screens form a chain (dropdown
  lists, tooltip drill pages, key capture).
- Active screen resolved by polling IsActive() on all screens every frame and diffing a layer-sorted stack.
- Node identity is two-tier (reference, then structural key); equality is structural.
- GraphBuilder: rows (Left/Right within, Up/Down between), Tab-stops (arrows never cross), regions
  (Ctrl+Up/Down), non-focusable contexts announced on entry, expandable groups.
- KeyGraph reconcile ladder on rerender: suggested move, reference match, structural match, nearest survivor
  in the same land-group, nearest survivor in previous order, start node preferring the selected member.
- Keys: arrows (adjust on sliders; tree semantics at edges; bubble out when nothing moved), Tab/Shift+Tab
  stops, Enter activate + speak StateText interrupting, Backspace secondary, Escape = screen Back action,
  Space/F1 tooltip, Backslash drag, Home/End, Ctrl+Up/Down region, PageUp/Down reserved for the scanner.
- Typeahead: OS typed characters, scoped to the focused Tab-stop, six match tiers, list-order ranking,
  repeat letter cycles, Up/Down step results, Escape clears; space only with an existing buffer.
- Controls expose behaviour as data (NodeVtable): announcement parts (label, role, value, selected, enabled,
  tooltip, position), OnActivate/OnSecondary/OnTooltip/OnDrag/OnAdjust, StateText, Live parts re-spoken on change.
- Announcer: path diff against the previous focus (entered containers outermost-first, then the control),
  level dedupe, stable-sort parts by kind, per-kind/per-type user filters. Pull-based: speaks only when the
  focused identity changed, exactly once, whatever moved it. Throttle full rebuilds (~every 6 frames); the
  live watch runs every frame.
- Tables: one Tab-stop of regions; column headers as edge labels; the whole row is the primary cell's
  announcement; row identity is the domain object; every cell inherits the primary's actions.
- Text entry is not a graph control: hand the keyboard to the game's own field and echo diffs.
- Tooltips are documents resolved live, never cached; links become menu entries; drill pushes a screen.

## Localization
- Every spoken string is a deferred Message: localization lookup, {var} substitution, markup strip; resolve
  only at the output boundary. Flat JSON per language, dotted keys; enGB is the complete manifest; missing
  key speaks the key itself. Game content passes through untouched.
- Make the deferred type the only way to produce speech so the prose rules become compile errors.

## Exploration
- A virtual movement cursor glides/steps over the walkable surface while the character stands still; the
  audio listener is teleported onto it at a calibrated height. A separate review cursor enumerates a
  categorized, distance-sorted list of world objects without moving anything.
- Movement modes: continuous glide (feet/sec, traces along the surface, never leaves walkable ground,
  optional wall slide and direction priority) and tile step (snap to cell centres, typematic, never falls).
- One taxonomy tree drives scanner categories, sonar sounds, announcement part sets and settings. Each item
  has Nodes (membership), Primary (state-aware, what sounds), AnnounceNode (stable).
- Review cycles on single keys (party, enemies, neutrals, objects, room exits, unexplored frontier); the
  first press announces the current spot without moving.
- Detectability: currently seen -> always; remembered under fog -> only if standing at the cursor would
  reveal it (vision radius + line of sight).
- Overlay systems are pure providers (sonar sweep, wall tones, slope pitch, fog cue, grid readout, spatial
  readout, object cue, path info, AoE preview, log feed); they never move the cursor.
- Sonar: staggered sweep left-to-right, gap = clamp(0.75/count, 100ms, 200ms), rest 400ms, volume
  ref/(ref+dist) with ref 10ft, hard cull 40ft. Wall tones: four ear-fixed loops, quadratic within 15ft.
  Slope: sine whose pitch integrates elevation change, 2 semitones/ft, +-18 semitones, hold 250ms.
- Directions: 0 = north, sectors centred on their direction; seven styles (compass 8/16, short forms,
  relative 4/8, clock); distances exact in the game's units; "above/below" only past a threshold.
- Probes: navmesh sample (walkable? height? connected region?), path probe (LOS, straight walk, A* with an
  arrival gap), block probe (what stopped me, wall vs obstacle).
- Room segmentation from the navmesh: rasterize, furniture mask, clearance field, slope mask, persistence
  watershed (doorway = saddle 0.7m below both basins), merge small regions, stable numbering, classify;
  exits from portal edges clustered by shared endpoint. Prefer more rooms over fewer.
- Fog: use the game's own explored-state accumulator; fail open.
- Proxy abstraction: every consumer sees one interface (name, position, nodes, visibility, footprint,
  bounds, nearest point non-allocating, state parts, interact); one proxy instance per entity for life.

## Audio
- One mixer (44.1k stereo float) over one output; hand-written spatializer: capped ILD (12dB) + ITD
  (Woodworth, head 0.22m) + far-ear shelf (8dB at 1.5kHz) for left/right, rear high-shelf cut (10dB at 3kHz)
  for front/back, distance gain owned by callers. Tones: sine, 20ms linear gain slew, phase-continuous.
- Two threads: main thread does all game reads and placement updates via volatile targets; audio thread
  never allocates. In C++ a much smaller buffer than 100ms is fine.
- UI sounds belong at the ears at full volume.

## Dev tooling (build first)
- Loopback HTTP server in the mod (debug builds only): /speech?since=N (the spoken-line ring buffer — this
  is how an agent "hears"), /gui (interpreted dump of the focused nav graph), /input (fire an action as a
  real press would), /screenshot, /reload, /health, plus typed probes (/world, /rooms, /scan, /cursor).
- Main-thread job pump with timeouts; keep the frame loop running while unfocused.
- A floor-plan renderer for room segmentation tuning.

## Settings and wizard
- Dot-path keys are the serialization contract; flat JSON; every Set auto-saves (batch for resets);
  unknown keys preserved (that is the migration mechanism); nullable settings = per-thing override vs inherit,
  Backspace resets to inherit everywhere; Basic settings are views over the same objects as the full tree.
- First-run wizard reuses the real settings controls, speaks a sample through the chosen output, and is kept
  short: prefer changing a default over adding a step.

## Lessons
- Verify hooks actually attached; log the count.
- Read the view/render position, not the logical one.
- Mirror the game's own visibility rules; deliberate deviations live in an explicit "Enhancements" category.
- Describe the permanent stage, not transient actors; nothing invented; never sanitize mature content.
- Document rationale in class-level comments including the failed alternative and the dated repro.
