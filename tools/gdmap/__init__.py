"""Offline readers for Grim Dawn's world data (resources/Levels.arc -> world001.map -> regions -> level bodies)
and the room segmentation built on them. Nothing here touches the running game.

Layout: arc.py (the .arc container), mapfile.py (the .map header: quests, regions, level bodies),
level.py (a region's level body: Detour tile-cache layers = the walkable grid, terrain texture layers = roads),
rooms.py (segmentation), render.py (floor plans)."""
