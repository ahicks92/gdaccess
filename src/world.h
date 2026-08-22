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
bool player_position(Vec3& p);   // world-space position of the main player (feet)
std::string player_name();
std::string region_name();
double life();                   // current life (0 when unknown)
float life_max();
float camera_yaw();              // the game camera's yaw, as reported (units logged)

// Navmesh probes around the player. dir is a unit vector in the horizontal plane (x, z), distances in the
// game's world units. free_distance walks from the player along dir and returns how far the path mesh stays
// walkable (max_dist when nothing blocks). on_navmesh tests one world-space point near the player.
bool on_navmesh(const Vec3& world_point);
float free_distance(float dir_x, float dir_z, float max_dist, float step);

std::string debug_dump();        // for the dev server: pointers, raw coordinate bytes, probes

// What stopped a probe: an OBSTACLE has walkable navmesh a few units beyond it along the probe direction
// (the character walks round it), a WALL has none (cliffs, buildings, level geometry). The nearby-entity
// cache (sphere query, refreshed every ~200 ms; ABBox = centre + half-extents) names the blocker for /blocks.
enum class BlockKind { Wall, Obstacle };
BlockKind classify_block(const Vec3& stop_world, float dir_x, float dir_z);
std::string blocks_dump();       // the four probe stops with their blockers (dev)

// The game's own display label for an entity near the player (Monster::GetGameDescription /
// Npc::GetRolloverDescription / Player::GetRolloverDescription / Item::GetGameDescription, by class), colour
// codes stripped; empty when the class has no label export. Game text, verbatim.
std::string label_of(unsigned id);

// ---- the review cursor (wotr's scanner, adapted) ----
// Groups cycle nearest-first from the player; the landing is remembered by OBJECT ID (session-unique,
// from ObjectManager::CreateObjectID) and re-found in a fresh query on every step -- never by pointer --
// so a despawned target simply drops out and the next step enters at the nearest. The landing also
// parks the virtual cursor on the thing, so the game hovers / targets it natively.
enum class ScanGroup { Enemies, Neutrals, Bystanders, Objects };
struct ScanItem { unsigned id; std::string cls, label, record; Vec3 pos; float dist; };
std::vector<ScanItem> scan(ScanGroup group, float radius = 40.0f);  // nearest first
// Step the review cursor through a group (dir +1 / -1); returns the landing's spoken line, or the
// group's "nothing" text. Also locks the virtual cursor on it.
std::string cycle_review(ScanGroup group, int dir);
unsigned reviewed_id();
// Interact with the reviewed thing: a left click on it (attack for an enemy, talk / open / pick up for
// the rest -- the game decides by what is under the cursor).
bool interact_reviewed();
// Bearing of a world point from the player as a clock hour (12 = screen-up), and distance in units.
int clock_hour(const Vec3& p);
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
void unlock_target();
unsigned locked_target();
void tick();                                   // per frame while in the world
bool entity_screen_pos(unsigned id, float& x, float& y);  // client-area pixels via WorldCamera::Project
std::string project_dump(unsigned id);
}  // namespace gd::world
