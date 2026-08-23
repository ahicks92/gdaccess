#pragma once
// The in-game substrate: the main player, its controller, the navmesh and the camera, reached through the
// game's exports. GameEngine is not an exported singleton, so the instance pointers are captured from hooks on
// per-frame members (GameEngine::Update, ControllerPlayer::Update). All calls here are game-thread only.
#include <string>
#include <vector>

namespace gd::world {
struct Vec3 { float x = 0, y = 0, z = 0; };

bool install();   // attach the capture hooks (Game.dll/Engine.dll exports)
void remove();

bool in_world();                 // the engine, the main player and its controller have all been seen
void* game_engine();             // the GAME::GameEngine instance (captured from GameEngine::Update; null before)
void* controller();              // the main player's ControllerPlayer (captured from ControllerPlayer::Update; null before)
bool player_position(Vec3& p);   // world-space position of the main player (feet)
std::string player_name();
std::string region_name();
double life();                   // current life (0 when unknown)
float life_max();
float energy();                  // "mana" in the exports, "energy" in the UI
float energy_max();
float camera_yaw();              // the game camera's yaw, as reported (units logged)

// Navmesh probes around the player. dir is a unit vector in the horizontal plane (x, z), distances in the
// game's world units. free_distance walks from the player along dir and returns how far the path mesh stays
// walkable (max_dist when nothing blocks). on_navmesh tests one world-space point near the player.
bool on_navmesh(const Vec3& world_point);
float free_distance(float dir_x, float dir_z, float max_dist, float step);

std::string debug_dump();        // for the dev server: pointers, raw coordinate bytes, probes
std::string classinfo_dump();    // dev: the game's RTTI_ClassInfo layout (parent pointer?)

// What stopped a probe: an OBSTACLE has walkable navmesh a few units beyond it along the probe direction
// (the character walks round it), a WALL has none (cliffs, buildings, level geometry). The nearby-entity
// cache (sphere query, refreshed every ~200 ms; ABBox = centre + half-extents) names the blocker for /blocks.
enum class BlockKind { Wall, Obstacle };
BlockKind classify_block(const Vec3& stop_world, float dir_x, float dir_z);
std::string blocks_dump();       // the four probe stops with their blockers (dev)
std::string regions_dump(int max);  // engine Regions (chunks) 0..max-1: index, name, offset from world, loaded, portals (dev)
std::string portals_dump();      // the player's chunk's portals: connected chunk, choke point, open (dev)
std::string navprobe(float x0, float z0, float x1, float z1, float step);  // IsPointOnPathMesh over a grid (dev)
std::string teleport(float x, float z, bool check_only);
std::string set_paused(int want);                                      // dev: -1 = report, 0/1 = GAME::UnpauseGameTime/PauseGameTime (a hot reload in the world pauses the game)                 // dev: Entity::SetCoords on the player (floored); refuses unloaded chunks
std::string project_points(const std::vector<Vec3>& pts);                // dev: world ground points -> screen
std::string fog_reveal(float x, float z, int radius);                    // dev: FogOfWar::AddVisibility

// The game's own display label for an entity near the player (Monster::GetGameDescription /
// Npc::GetRolloverDescription / Player::GetRolloverDescription / Item::GetGameDescription, by class), colour
// codes stripped; empty when the class has no label export. Game text, verbatim.
std::string label_of(unsigned id);

// ---- the review cursor (wotr's scanner, adapted) ----
// Groups cycle nearest-first from the player; the landing is remembered by OBJECT ID (session-unique,
// from ObjectManager::CreateObjectID) and re-found in a fresh query on every step -- never by pointer --
// so a despawned target simply drops out and the next step enters at the nearest. The landing also
// parks the virtual cursor on the thing, so the game hovers / targets it natively.
enum class ScanGroup { Enemies, Neutrals, Bystanders, Objects, Exits, Loot, Transitions };   // Loot/Transitions: the sonar sweep only
// note: an extra spoken list item ("blocked" for an exit whose opening the live mesh refuses), normally empty.
struct ScanItem { unsigned id; std::string cls, label, record; Vec3 pos; float dist; std::string note; };
std::vector<ScanItem> scan(ScanGroup group, float radius = 40.0f);  // nearest first
// Point items (room exits) are not entities: ids from kPointIdBase up, positions carried by the item. The
// provider (src/rooms.cpp) returns the current room's exits; the scanner locks/pings/projects the point.
constexpr unsigned kPointIdBase = 0x40000000u;
inline bool is_point_id(unsigned id) { return id >= kPointIdBase; }
void set_exit_provider(std::vector<ScanItem> (*provider)());
// Step the review cursor through a group (dir +1 / -1); returns the landing's spoken line, or the
// group's "nothing" text. Also locks the virtual cursor on it.
std::string cycle_review(ScanGroup group, int dir, bool nearest = false);   // nearest: enter at the closest regardless of the current target (Alt+key)
unsigned reviewed_id();
// The mouse buttons as keys (J left, I right; hold = hold): the button goes down at the virtual cursor --
// the reviewed thing when one is locked, else the real cursor -- and the game decides what that means
// (attack, talk, open, pick up, move, skill). A locked thing the camera does not show cannot be clicked:
// "too far away", nothing happens. Per frame from the in-game screen with the key's held state.
void mouse_key(int button, bool held);
// Whether an entity projects inside the game window (the camera shows it).
bool on_screen(unsigned id);
// Camera lock (per frame from the in-game screen): zoom at the far end of its range, yaw 0 (north up).
void pin_camera();
// Bearing of a world point from the player as a clock hour (12 = screen-up), and distance in units.
int clock_hour(const Vec3& p);
// Pan (-1..1, by ear-frame bearing) and gain (ref/(ref+dist), >= 0.15) of a world point from the player: the
// one rule for positioned sounds and voices.
void ear_frame(const Vec3& p, float& pan, float& gain, float* ahead = nullptr);   // ahead: +1 straight up the screen .. -1 behind
// The rear shelf for a point: 0 dB across the front, -10 dB dead behind (wotr Spatializer), from ear_frame's ahead.
float rear_shelf_db(float ahead);
// The voices' own rolloff (the pings keep the sonar curve above): full level out to `near`, falling linearly
// to `floor` at `far`, flat beyond. Defaults from the game's range table (gameengine.dbr): near = moderateRange
// 9, far = bossRange 32 (the farthest a pet's hit can be), floor 0.4. Live-tunable: /voice?near=&far=&floor=.
float voice_gain(float dist);
void set_voice_rolloff(float near_d, float far_d, float floor_g);  // <0 = keep
std::string voice_rolloff();
// A raw WorldVec3 (Region* + Vec3, 24 bytes) to world space; false when it has no region.
bool world_point(const void* worldvec3, Vec3& out);
// The review ping (wotr's Semicolon, also played on every landing): one of three sounds for the route
// from the player to the reviewed thing -- straight walk, path around, unreachable -- positioned toward it
// (pan by bearing, volume ref/(ref+distance)). Returns the kind for the log; empty = nothing reviewed.
std::string ping_reviewed();

// ---- conversations (structured, from hooks on Conversation::GetText / GetSteps) ----
// The UI asks the conversation for each step's text as it draws the dialog; we keep what it asked for
// this frame: the steps, their type/availability and text, and the child lists it fetched. The screen
// reads this instead of crawling drawn text (which cannot tell a response from a line of speech).
struct ConvStep { void* step; int type; std::string type_tag; bool available, used; std::string text; void* parent; };
struct ConvState { void* conversation = nullptr; uint64_t frame = 0; std::vector<ConvStep> steps; std::vector<void*> children; void* children_of = nullptr; };
ConvState conversation_state();   // the most recent frame in which the UI fetched conversation text
bool in_conversation();           // text was fetched within the last few frames
std::string conversation_dump();  // /conv

// ---- targeting (measurement phase) ----
// Everything the game rendered last frame, nearest first: pointer, object id, record name, class, distance.
std::string entities_dump(float max_dist);
bool set_target(unsigned id);    // ControllerPlayer::SetCombatEnemy + FaceTarget
void clear_target();
unsigned current_target();       // ControllerPlayer::GetCombatEnemy
std::string target_dump();       // combat enemy + Character::GetCurrentAttackTarget

// ---- the virtual cursor ----
// The exe clears and re-resolves the combat enemy/ally from the cursor every frame (read 2026-08-21), so a
// target of ours is expressed as the cursor parked over the entity on screen: the game then hovers, attacks
// and interacts with it natively. lock_target keeps the cursor override on the entity each frame (tick).
bool lock_target(unsigned id);
bool lock_point(const Vec3& world_point);   // the same, for a bare world point (a room exit); unlock_target releases it too
void unlock_target();
unsigned locked_target();
void tick();                                   // per frame while in the world
bool entity_screen_pos(unsigned id, float& x, float& y);  // client-area pixels via WorldCamera::Project
std::string project_dump(unsigned id);
}  // namespace gd::world
