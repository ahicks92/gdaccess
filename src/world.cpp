#include "world.h"
#include <windows.h>
#include <intrin.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <format>
#include <mutex>
#include <vector>
#include "gd_names.h"
#include "hooks.h"
#include "speech.h"
#include "audio.h"
#include "core/message_builder.h"
#include "core/strings.h"
#include "log.h"
#include "msvc_string.h"
#include "textcap.h"

namespace gd::world {
using namespace gd::names;
namespace {
// ---- instances captured from per-frame members ----
void* g_game_engine = nullptr;
void* g_controller = nullptr;
void* g_world = nullptr;
uint64_t g_engine_ticks = 0, g_controller_ticks = 0;

std::string caller_module(void* ret) {
  HMODULE m = nullptr;
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)ret, &m);
  if (!m) return "?";
  char path[MAX_PATH]; GetModuleFileNameA(m, path, MAX_PATH);
  const char* base = strrchr(path, '\\'); base = base ? base + 1 : path;
  return std::format("{}+{:#x}", base, (uintptr_t)ret - (uintptr_t)m);
}

typedef void (*Update_t)(void*, int);
Update_t GameEngineUpdate_orig, ControllerPlayerUpdate_orig;
void GameEngineUpdate(void* self, int dt) { g_game_engine = self; ++g_engine_ticks; GameEngineUpdate_orig(self, dt); }
void ControllerPlayerUpdate(void* self, int dt) { g_controller = self; ++g_controller_ticks; ControllerPlayerUpdate_orig(self, dt); }
typedef void (*WorldUpdate_t)(void*, const void*, const void*);
WorldUpdate_t WorldUpdate_orig;
void WorldUpdate(void* self, const void* spheres, const void* frusta) { g_world = self; WorldUpdate_orig(self, spheres, frusta); }

// ---- targeting instrumentation: who sets the combat enemy / sends actions, and with what ----
typedef void (*SetId_t)(void*, unsigned);
SetId_t SetCombatEnemy_hook_orig, FaceTarget_hook_orig;
typedef void (*ClearTarget_t)(void*);
ClearTarget_t ClearTarget_hook_orig;
typedef bool (*HandleJoystick_t)(void*, const void*, bool);
HandleJoystick_t HandleActionFromJoystick_hook_orig;
typedef bool (*HandleMouse_t)(void*, bool, bool, bool, bool, const void*, unsigned*, bool*);
HandleMouse_t HandleActionFromMouse_hook_orig;
unsigned g_last_enemy_logged = ~0u;
uint64_t g_c_joystick = 0, g_c_mouse = 0;
void SetCombatEnemy_hook(void* self, unsigned id) {
  if (id != g_last_enemy_logged) { g_last_enemy_logged = id; log::writef("target: SetCombatEnemy({}) <- {}", id, caller_module(_ReturnAddress())); }
  SetCombatEnemy_hook_orig(self, id);
}
void FaceTarget_hook(void* self, unsigned id) { log::writef("target: FaceTarget({}) <- {}", id, caller_module(_ReturnAddress())); FaceTarget_hook_orig(self, id); }
void ClearTarget_hook(void* self) {
  if (g_last_enemy_logged != 0) { g_last_enemy_logged = 0; log::writef("target: ClearTarget <- {}", caller_module(_ReturnAddress())); }
  ClearTarget_hook_orig(self);
}
std::string wv_text(const void* wv);
bool HandleActionFromJoystick_hook(void* self, const void* wv, bool b) {
  bool r = HandleActionFromJoystick_hook_orig(self, wv, b);
  if (++g_c_joystick % 60 == 1 || b) log::writef("target: HandleActionFromJoystick({}, {}) -> {} <- {} [#{}]", wv_text(wv), b, r, caller_module(_ReturnAddress()), g_c_joystick);
  return r;
}
bool HandleActionFromMouse_hook(void* self, bool a, bool b, bool c, bool d, const void* wv, unsigned* id, bool* flag) {
  unsigned before = id ? *id : 0;
  bool r = HandleActionFromMouse_hook_orig(self, a, b, c, d, wv, id, flag);
  if (++g_c_mouse % 60 == 1 || a || b)
    log::writef("target: HandleActionFromMouse({},{},{},{}, {}, id {}->{}, flag {}) -> {} <- {} [#{}]", a, b, c, d, wv_text(wv), before, id ? *id : 0,
                flag ? (int)*flag : -1, r, caller_module(_ReturnAddress()), g_c_mouse);
  return r;
}

struct alignas(16) Buf { unsigned char b[256]; };
struct MemVec { void* begin; void* end; void* cap; };  // mem::vector<T>: std-like {begin, end, cap} (read from Engine.dll)

// ---- conversation capture ----
// The UI fetches each displayed step's text through Conversation::GetText while the dialog is up, and a
// step's children through Conversation::GetSteps (private, but exported). Both are hooked; the per-frame
// fetches are the structured picture of the dialog.
std::mutex g_conv_mu;
ConvState g_conv_cur, g_conv_last;   // cur = being filled this frame; last = the latest complete frame
uint64_t g_conv_cur_frame = ~0ull;
typedef void (*ConvGetText_t)(void*, const void*, int, MsvcStringW*);
ConvGetText_t ConvGetText_hook_orig;
typedef void (*ConvGetSteps_t)(void*, void*, void*);
ConvGetSteps_t ConvGetSteps_hook_orig;
struct ConvApi {
  int (*GetType)(const void*) = nullptr;
  MsvcStringA* (*GetTypeTag)(const void*, MsvcStringA*) = nullptr;  // std::string by value -> hidden pointer
  bool (*IsAvailable)(const void*) = nullptr;
  bool (*IsUsed)(const void*) = nullptr;
  void* (*GetParent)(const void*) = nullptr;
} g_conv_api;
void conv_frame_begin(void* conv) {
  // Called under g_conv_mu. A new frame (per our tick counter) rotates cur -> last.
  uint64_t f = gd::hooks::frame();
  if (f != g_conv_cur_frame) {
    if (g_conv_cur_frame != ~0ull) g_conv_last = g_conv_cur;
    g_conv_cur = ConvState{}; g_conv_cur.frame = f; g_conv_cur_frame = f;
  }
  g_conv_cur.conversation = conv;
}
// Plain-data step readout under an SEH guard (no C++ objects with destructors may live in a __try frame).
struct StepRaw { int type; bool available, used; void* parent; char tag[64]; };
void read_step_raw(const void* step, StepRaw& r) {
  __try {
    if (g_conv_api.GetType) r.type = g_conv_api.GetType(step);
    if (g_conv_api.IsAvailable) r.available = g_conv_api.IsAvailable(step);
    if (g_conv_api.IsUsed) r.used = g_conv_api.IsUsed(step);
    if (g_conv_api.GetParent) r.parent = g_conv_api.GetParent(step);
    if (g_conv_api.GetTypeTag) {
      alignas(16) unsigned char sb[64] = {}; MsvcStringA* t = (MsvcStringA*)sb; t->capacity = 15;
      if (g_conv_api.GetTypeTag(step, t)) {
        std::string_view v = t->view();
        size_t n = v.size() < sizeof r.tag - 1 ? v.size() : sizeof r.tag - 1;
        memcpy(r.tag, v.data(), n); r.tag[n] = 0;
        if (t->capacity > 15 && t->u.ptr) free(t->u.ptr);
      }
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
void ConvGetText_hook(void* self, const void* step, int gender, MsvcStringW* out) {
  ConvGetText_hook_orig(self, step, gender, out);
  StepRaw raw{-1, false, false, nullptr, {}};
  read_step_raw(step, raw);
  ConvStep s{(void*)step, raw.type, raw.tag, raw.available, raw.used, out ? textcap::speakable(out->view()) : std::string(), raw.parent};
  std::lock_guard lk(g_conv_mu);
  conv_frame_begin(self);
  for (ConvStep& e : g_conv_cur.steps) if (e.step == step) { e = s; return; }
  g_conv_cur.steps.push_back(s);
}
void ConvGetSteps_hook(void* self, void* parent, void* out_vec) {
  ConvGetSteps_hook_orig(self, parent, out_vec);
  std::lock_guard lk(g_conv_mu);
  conv_frame_begin(self);
  MemVec* v = (MemVec*)out_vec;
  g_conv_cur.children.clear(); g_conv_cur.children_of = parent;
  if (v && v->begin && v->end && (uintptr_t)v->end > (uintptr_t)v->begin && (uintptr_t)v->end - (uintptr_t)v->begin < (1u << 16)) {
    size_t n = (size_t)((char*)v->end - (char*)v->begin) / sizeof(void*);
    for (size_t i = 0; i < n; ++i) { void* c; memcpy(&c, (char*)v->begin + i * sizeof(void*), sizeof c); g_conv_cur.children.push_back(c); }
  }
}

std::vector<gd::hooks::Hook> g_hooks;

// ---- exports called directly (resolved once) ----
// Class-by-value returns (WorldCoords, Coords, Vec3) use the hidden return pointer as the 2nd argument.
struct Api {
  void* (*GetMainPlayer)(const void*) = nullptr;
  void* (*GetCamera)(void*) = nullptr;
  void* (*Entity_GetCoords)(const void*, void*) = nullptr;
  void* (*Entity_GetRegion)(const void*) = nullptr;
  void* (*Character_GetFootCoords)(void*, void*, bool) = nullptr;
  double (*GetCurrentLife)(const void*) = nullptr;
  float (*GetLifeLimit)(const void*) = nullptr;
  float (*GetCurrentMana)(const void*) = nullptr;   // "energy" in the UI; float (GetCurrentLife is a double)
  float (*GetManaLimit)(const void*) = nullptr;
  const char16_t* (*GetPlayerName)(const void*) = nullptr;
  const MsvcString<char>* (*Region_GetName)(const void*) = nullptr;
  void* (*NavManager_Get)() = nullptr;
  bool (*IsPointOnPathMesh)(void*, const void*) = nullptr;
  int (*FindStraightMovePoint)(void*, const void*, const void*, void*) = nullptr;
  int (*FindClosestPointOnPathMesh)(void*, const void*, void*, float) = nullptr;
  void* (*WorldVec3_ctor)(void*, void*, const void*) = nullptr;
  void* (*WorldVec3_GetWorldPosition)(const void*, void*) = nullptr;
  void* (*WorldVec3_GetRegion)(const void*) = nullptr;
  const Vec3* (*WorldVec3_GetRegionPosition)(const void*) = nullptr;
  bool (*WorldVec3_PutOnFloor)(void*) = nullptr;
  void* (*WorldCoords_GetRegionCoords)(const void*, void*) = nullptr;
  float (*GetCameraYaw)(const void*) = nullptr;
  // targeting / entities
  bool (*GetEntitiesInPriorFrameFrustum)(void*, void*) = nullptr;
  void (*Region_GetEntitiesInSphere)(void*, void*, const void*, bool, int) = nullptr;
  unsigned (*Object_GetObjectId)(const void*) = nullptr;
  const float* (*Entity_GetRegionBoundingBox)(const void*, bool) = nullptr;  // const ABBox&: {min, max} Vec3s (verified via /blocks dump)
  const char* (*Object_GetObjectName)(const void*) = nullptr;  // `char const*` per the export (not a std::string)
  const void* (*Object_GetRTTIClassInfo)(const void*) = nullptr;
  const void* (*Entity_StaticClassInfo)() = nullptr;
  const void* (*Character_StaticClassInfo)() = nullptr;
  const void* (*Monster_StaticClassInfo)() = nullptr;
  const void* (*Npc_StaticClassInfo)() = nullptr;
  const void* (*Player_StaticClassInfo)() = nullptr;
  void (*GetCurrentAttackTarget)(void*, unsigned*, void*, unsigned*) = nullptr;
  void* (*GetFactionManager)(void*) = nullptr;
  bool (*FactionManager_IsFoe)(void*, unsigned, unsigned, bool) = nullptr;  // by object ids
  // labels: basic_string<unsigned short> by value -> hidden return pointer (2nd arg)
  MsvcStringW* (*Monster_GetGameDescription)(const void*, MsvcStringW*, bool, bool) = nullptr;
  MsvcStringW* (*Npc_GetRolloverDescription)(const void*, MsvcStringW*) = nullptr;
  MsvcStringW* (*Player_GetRolloverDescription)(const void*, MsvcStringW*) = nullptr;
  MsvcStringW* (*Item_GetGameDescription)(const void*, MsvcStringW*, bool, bool) = nullptr;
  void (*SetCombatEnemy)(void*, unsigned) = nullptr;
  unsigned (*GetCombatEnemy)(const void*) = nullptr;
  void (*ClearTarget)(void*) = nullptr;
  void (*FaceTarget)(void*, unsigned) = nullptr;
  void** Object_vftable = nullptr;                       // to find GetRTTIClassInfo's slot (virtual dispatch)
  // Review classification = the Interact key's own filter (docs: re_interact_key.md): FixedActor/Item that
  // say IsOfInterest() (virtual; slot found in each class's vftable), Npc with a conversation.
  const void* (*FixedActor_StaticClassInfo)() = nullptr;
  const void* (*Item_StaticClassInfo)() = nullptr;
  bool (*FixedActor_IsOfInterest)(const void*) = nullptr;
  bool (*Item_IsOfInterest)(const void*) = nullptr;
  void** FixedActor_vftable = nullptr;
  void** Item_vftable = nullptr;
  bool (*Npc_HasConversation)(const void*) = nullptr;
  void* (*Project)(const void*, void*, const void*, const void*) = nullptr;  // WorldCamera::Project: hidden Vec2 return
  void (*SetZoom)(void*, float) = nullptr;               // GameCamera::SetZoom(value in the camera's zoom range)
  void (*ResetZoom)(void*) = nullptr;
  void (*SetCameraYaw)(void*, float) = nullptr;          // GameCamera::SetCameraYaw (radians)
  void* (*Viewport_ctor)(void*, int, int, int, int) = nullptr;
  bool loaded = false;
} g_api;

template <class F> F fn(const char* dll, const char* name) {
  HMODULE m = GetModuleHandleA(dll);
  return m ? (F)GetProcAddress(m, name) : nullptr;
}
void load_api() {
  if (g_api.loaded) return;
  g_api.loaded = true;
#define LOAD(field, ID) \
  g_api.field = fn<decltype(g_api.field)>(ID##_DLL, ID); \
  if (!g_api.field) log::writef("world: export {} not found", #ID)
  LOAD(GetMainPlayer, GameEngine_GetMainPlayer);
  LOAD(GetCamera, GameEngine_GetCamera);
  LOAD(Entity_GetCoords, Entity_GetCoords);
  LOAD(Entity_GetRegion, Entity_GetRegion);
  LOAD(Character_GetFootCoords, Character_GetFootCoords);
  LOAD(GetCurrentLife, Character_GetCurrentLife);
  LOAD(GetLifeLimit, Character_GetLifeLimit);
  LOAD(GetCurrentMana, Character_GetCurrentMana);
  LOAD(GetManaLimit, Character_GetManaLimit);
  LOAD(GetPlayerName, Player_GetPlayerName);
  LOAD(Region_GetName, Region_GetName);
  LOAD(NavManager_Get, NavManager_Get);
  LOAD(IsPointOnPathMesh, NavManager_IsPointOnPathMesh);
  LOAD(FindStraightMovePoint, NavManager_FindStraightMovePoint);
  LOAD(FindClosestPointOnPathMesh, NavManager_FindClosestPointOnPathMesh);
  LOAD(WorldVec3_ctor, WorldVec3_ctor);
  LOAD(WorldVec3_GetWorldPosition, WorldVec3_GetWorldPosition);
  LOAD(WorldVec3_GetRegion, WorldVec3_GetRegion);
  LOAD(WorldVec3_GetRegionPosition, WorldVec3_GetRegionPosition);
  LOAD(WorldVec3_PutOnFloor, WorldVec3_PutOnFloor);
  LOAD(WorldCoords_GetRegionCoords, WorldCoords_GetRegionCoords);
  LOAD(GetCameraYaw, WorldCamera_GetCameraYaw);
  LOAD(GetEntitiesInPriorFrameFrustum, Engine_GetEntitiesInPriorFrameFrustum);
  LOAD(Region_GetEntitiesInSphere, Region_GetEntitiesInSphere);
  LOAD(Object_GetObjectId, Object_GetObjectId);
  LOAD(Entity_GetRegionBoundingBox, Entity_GetRegionBoundingBox);
  LOAD(Object_GetObjectName, Object_GetObjectName);
  LOAD(Object_GetRTTIClassInfo, Object_GetRTTIClassInfo);
  LOAD(Entity_StaticClassInfo, Entity_GetStaticClassInfo);
  LOAD(Character_StaticClassInfo, Character_GetStaticClassInfo);
  LOAD(Monster_StaticClassInfo, Monster_GetStaticClassInfo);
  LOAD(Npc_StaticClassInfo, Npc_GetStaticClassInfo);
  LOAD(Player_StaticClassInfo, Player_GetStaticClassInfo);
  LOAD(GetCurrentAttackTarget, Character_GetCurrentAttackTarget);
  LOAD(GetFactionManager, GameEngine_GetFactionManager);
  LOAD(FactionManager_IsFoe, FactionManager_IsFoe);
  LOAD(Monster_GetGameDescription, Monster_GetGameDescription);
  LOAD(Npc_GetRolloverDescription, Npc_GetRolloverDescription);
  LOAD(Player_GetRolloverDescription, Player_GetRolloverDescription);
  LOAD(Item_GetGameDescription, Item_GetGameDescription);
  LOAD(SetCombatEnemy, ControllerPlayer_SetCombatEnemy);
  LOAD(GetCombatEnemy, ControllerPlayer_GetCombatEnemy);
  LOAD(ClearTarget, ControllerPlayer_ClearTarget);
  LOAD(FaceTarget, ControllerPlayer_FaceTarget);
  LOAD(Object_vftable, Object_vftable);
  LOAD(FixedActor_StaticClassInfo, FixedActor_GetStaticClassInfo);
  LOAD(Item_StaticClassInfo, Item_GetStaticClassInfo);
  LOAD(FixedActor_IsOfInterest, FixedActor_IsOfInterest);
  LOAD(Item_IsOfInterest, Item_IsOfInterest);
  LOAD(FixedActor_vftable, FixedActor_vftable);
  LOAD(Item_vftable, Item_vftable);
  LOAD(Npc_HasConversation, Npc_HasConversation);
  LOAD(Project, WorldCamera_Project);
  LOAD(SetZoom, GameCamera_SetZoom);
  LOAD(ResetZoom, GameCamera_ResetZoom);
  LOAD(SetCameraYaw, GameCamera_SetCameraYaw);
  LOAD(Viewport_ctor, Viewport_ctor);
#undef LOAD
}

void* player() { return g_game_engine && g_api.GetMainPlayer ? g_api.GetMainPlayer(g_game_engine) : nullptr; }

// Layouts measured 2026-08-21 (see CLAUDE.md): WorldCoords = Region* (8) + origin Vec3 (12) + 3x3 axes;
// Coords = 3x3 axes then origin (offset 36); WorldVec3 = Region* + Vec3.
constexpr size_t kWorldCoordsOriginOffset = 8;
constexpr size_t kCoordsOriginOffset = 36;

Vec3 world_pos_of(const Buf& wv) {
  Vec3 v;
  if (g_api.WorldVec3_GetWorldPosition) { Buf o{}; g_api.WorldVec3_GetWorldPosition(&wv, &o); memcpy(&v, o.b, sizeof v); }
  return v;
}
std::string wv_text(const void* wv) {
  if (!wv) return "null";
  Buf b{}; memcpy(b.b, wv, 32);
  void* region; memcpy(&region, b.b, sizeof region);
  Vec3 r; memcpy(&r, b.b + 8, sizeof r);
  // GetWorldPosition on a WorldVec3 without a region hung the game thread once (2026-08-21): report the
  // region-relative part only.
  if (!region) return std::format("(no region; {:.1f}, {:.1f}, {:.1f})", r.x, r.y, r.z);
  Vec3 p = world_pos_of(b);
  return std::format("({:.1f}, {:.1f}, {:.1f})", p.x, p.y, p.z);
}

// Any entity's position as a WorldVec3 built through the game's own ctor from its WorldCoords.
bool entity_world_vec(const void* entity, Buf& out_wv, void** out_region = nullptr) {
  if (!entity || !g_api.Entity_GetCoords || !g_api.WorldVec3_ctor) return false;
  Buf wc{};
  g_api.Entity_GetCoords(entity, &wc);
  void* region; memcpy(&region, wc.b, sizeof region);
  if (!region) return false;
  Vec3 origin; memcpy(&origin, wc.b + kWorldCoordsOriginOffset, sizeof origin);
  memset(&out_wv, 0, sizeof out_wv);
  g_api.WorldVec3_ctor(&out_wv, region, &origin);
  if (out_region) *out_region = region;
  return true;
}
// The player's feet.
bool player_world_vec(Buf& out_wv, void** out_region = nullptr) {
  void* p = player();
  if (!p || !g_api.Character_GetFootCoords || !g_api.WorldCoords_GetRegionCoords || !g_api.WorldVec3_ctor || !g_api.Entity_GetRegion) return false;
  Buf wc{}, coords{};
  g_api.Character_GetFootCoords(p, &wc, false);
  g_api.WorldCoords_GetRegionCoords(&wc, &coords);
  void* region = g_api.Entity_GetRegion(p);
  if (!region) return false;
  Vec3 origin; memcpy(&origin, coords.b + kCoordsOriginOffset, sizeof origin);
  memset(&out_wv, 0, sizeof out_wv);
  g_api.WorldVec3_ctor(&out_wv, region, &origin);
  if (out_region) *out_region = region;
  return true;
}

// The object's dynamic RTTI_ClassInfo: the exported Object::GetRTTIClassInfo is the BASE implementation, so
// dispatch through the vtable instead -- its slot is where Object's own vftable holds that export.
// RTTI_ClassInfo (measured 2026-08-21): +0 vptr, +8 const char* name ("Player", "Monster", ...).
int g_rtti_slot = -1;
const void* rtti_of(const void* obj) {
  if (!obj || !g_api.Object_GetRTTIClassInfo) return nullptr;
  if (g_rtti_slot == -1) {
    g_rtti_slot = -2;
    if (g_api.Object_vftable)
      for (int i = 0; i < 64; ++i)
        if (g_api.Object_vftable[i] == (void*)g_api.Object_GetRTTIClassInfo) { g_rtti_slot = i; break; }
    log::writef("world: Object::GetRTTIClassInfo vtable slot = {}", g_rtti_slot);
  }
  if (g_rtti_slot < 0) return g_api.Object_GetRTTIClassInfo(obj);
  void** vt; memcpy(&vt, obj, sizeof vt);
  return ((const void* (*)(const void*))vt[g_rtti_slot])(obj);
}
std::string rtti_name(const void* ci) {
  if (!ci) return "?";
  const char* n; memcpy(&n, (const char*)ci + 8, sizeof n);
  return n && !IsBadStringPtrA(n, 64) ? std::string(n) : std::string("?");
}
std::string class_name(const void* obj) {
  const void* ci = rtti_of(obj);
  if (!ci) return "?";
  const char* n; memcpy(&n, (const char*)ci + 8, sizeof n);
  return n && !IsBadStringPtrA(n, 64) ? std::string(n) : std::string("?");
}

// One object's readout, taken under a structured-exception guard: the sphere query returns every Object
// in range, not only Entities, so any of these calls may fault on a foreign object (that killed the game
// once). Plain data only -- no C++ objects with destructors may live in a __try frame.
struct EntityRaw { unsigned id; const void* ci; Vec3 pos; bool has_pos; char name[160]; };
bool read_entity(void* e, EntityRaw& r) {
  __try {
    r.id = g_api.Object_GetObjectId ? g_api.Object_GetObjectId(e) : 0;
    r.ci = rtti_of(e);  // dynamic class (vtable dispatch), not the exported base implementation
    const char* name = g_api.Object_GetObjectName ? g_api.Object_GetObjectName(e) : nullptr;
    r.name[0] = 0;
    if (name && !IsBadStringPtrA(name, 512)) { strncpy_s(r.name, name, sizeof r.name - 1); }
    Buf wv;
    r.has_pos = entity_world_vec(e, wv);
    if (r.has_pos) r.pos = world_pos_of(wv);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}
}  // namespace

bool install() {
  load_api();
  g_hooks = {GD_HOOK(GameEngine_Update, GameEngineUpdate), GD_HOOK(ControllerPlayer_Update, ControllerPlayerUpdate), GD_HOOK(World_Update, WorldUpdate),
             GD_HOOK(ControllerPlayer_SetCombatEnemy, SetCombatEnemy_hook), GD_HOOK(ControllerPlayer_FaceTarget, FaceTarget_hook),
             GD_HOOK(ControllerPlayer_ClearTarget, ClearTarget_hook), GD_HOOK(ControllerPlayer_HandleActionFromJoystick, HandleActionFromJoystick_hook),
             GD_HOOK(ControllerPlayer_HandleActionFromMouse, HandleActionFromMouse_hook),
             GD_HOOK(Conversation_GetText, ConvGetText_hook), GD_HOOK(Conversation_GetSteps, ConvGetSteps_hook)};
  g_conv_api.GetType = fn<decltype(g_conv_api.GetType)>(ConversationStep_GetType_DLL, ConversationStep_GetType);
  g_conv_api.GetTypeTag = fn<decltype(g_conv_api.GetTypeTag)>(ConversationStep_GetTypeTag_DLL, ConversationStep_GetTypeTag);
  g_conv_api.IsAvailable = fn<decltype(g_conv_api.IsAvailable)>(ConversationStep_IsAvailable_DLL, ConversationStep_IsAvailable);
  g_conv_api.IsUsed = fn<decltype(g_conv_api.IsUsed)>(ConversationStep_IsUsed_DLL, ConversationStep_IsUsed);
  g_conv_api.GetParent = fn<decltype(g_conv_api.GetParent)>(ConversationStep_GetParent_DLL, ConversationStep_GetParent);
  return gd::hooks::attach_hooks(g_hooks) == 0;
}
void remove() { gd::hooks::detach_hooks(g_hooks); g_game_engine = nullptr; g_controller = nullptr; g_world = nullptr; }

bool in_world() { return g_game_engine && g_controller && player() != nullptr; }
void* game_engine() { return g_game_engine; }

bool player_position(Vec3& p) {
  Buf wv;
  if (!player_world_vec(wv)) return false;
  p = world_pos_of(wv);
  return true;
}
std::string player_name() {
  void* p = player();
  const char16_t* n = p && g_api.GetPlayerName ? g_api.GetPlayerName(p) : nullptr;
  return n ? log::utf8(n) : std::string();
}
std::string region_name() {
  void* p = player();
  void* r = p && g_api.Entity_GetRegion ? g_api.Entity_GetRegion(p) : nullptr;
  if (!r || !g_api.Region_GetName) return {};
  const MsvcString<char>* s = g_api.Region_GetName(r);
  return s ? std::string(s->view()) : std::string();
}
double life() { void* p = player(); return p && g_api.GetCurrentLife ? g_api.GetCurrentLife(p) : 0.0; }
float life_max() { void* p = player(); return p && g_api.GetLifeLimit ? g_api.GetLifeLimit(p) : 0.0f; }
float energy() { void* p = player(); return p && g_api.GetCurrentMana ? g_api.GetCurrentMana(p) : 0.0f; }
float energy_max() { void* p = player(); return p && g_api.GetManaLimit ? g_api.GetManaLimit(p) : 0.0f; }
float camera_yaw() {
  void* cam = g_game_engine && g_api.GetCamera ? g_api.GetCamera(g_game_engine) : nullptr;
  return cam && g_api.GetCameraYaw ? g_api.GetCameraYaw(cam) : 0.0f;
}

bool on_navmesh(const Vec3& world_point) {
  void* nav = g_api.NavManager_Get ? g_api.NavManager_Get() : nullptr;
  Buf base; void* region = nullptr;
  if (!nav || !g_api.IsPointOnPathMesh || !player_world_vec(base, &region)) return false;
  // region-relative = world - (world_of_base - regionpos_of_base)
  Vec3 wb = world_pos_of(base);
  const Vec3* rb = g_api.WorldVec3_GetRegionPosition(&base);
  Vec3 rel{world_point.x - wb.x + rb->x, world_point.y - wb.y + rb->y, world_point.z - wb.z + rb->z};
  Buf wv{};
  g_api.WorldVec3_ctor(&wv, region, &rel);
  if (g_api.WorldVec3_PutOnFloor) g_api.WorldVec3_PutOnFloor(&wv);
  return g_api.IsPointOnPathMesh(nav, &wv);
}

float free_distance(float dir_x, float dir_z, float max_dist, float step) {
  Vec3 p;
  if (!player_position(p) || step <= 0) return 0;
  for (float d = step; d <= max_dist + 1e-4f; d += step) {
    Vec3 q{p.x + dir_x * d, p.y, p.z + dir_z * d};
    if (!on_navmesh(q)) return d - step;
  }
  return max_dist;
}

// Dev: the layout of RTTI_ClassInfo beyond the name -- is there a parent pointer? Dumps the static infos.
std::string classinfo_dump() {
  std::string out;
  struct { const char* n; const void* (*f)(); } infos[] = {{"Entity", g_api.Entity_StaticClassInfo}, {"Character", g_api.Character_StaticClassInfo}, {"Monster", g_api.Monster_StaticClassInfo},
                                                         {"Npc", g_api.Npc_StaticClassInfo}, {"Player", g_api.Player_StaticClassInfo}};
  for (auto& i : infos) {
    const void* ci = i.f ? i.f() : nullptr;
    out += std::format("{} ci={} name='{}'", i.n, ci, rtti_name(ci));
    for (size_t off = 0x10; off <= 0x30 && ci; off += 8) {
      const void* q; memcpy(&q, (const char*)ci + off, sizeof q);
      out += std::format(" +{:#x}={}", off, q);
      if (q && !IsBadReadPtr(q, 16)) { std::string n = rtti_name(q); if (n != "?") out += std::format("('{}')", n); }
    }
    out += '\n';
  }
  return out;
}
std::string debug_dump() {
  load_api();
  std::string s = std::format("game_engine={} controller={} world={} engine_ticks={} controller_ticks={} nav={} engine={}\n", g_game_engine, g_controller, g_world,
                              g_engine_ticks, g_controller_ticks, g_api.NavManager_Get ? g_api.NavManager_Get() : nullptr, gd::hooks::engine_object());
  void* p = player();
  s += std::format("player={} name='{}' region='{}' life={:.1f}/{:.1f} camera_yaw={:.4f}\n", p, player_name(), region_name(), life(), life_max(), camera_yaw());
  if (!p) return s;
  Vec3 w;
  if (player_position(w)) s += std::format("player world=({:.2f}, {:.2f}, {:.2f}) on_navmesh={} id={} class={}\n", w.x, w.y, w.z, on_navmesh(w),
                                           g_api.Object_GetObjectId ? g_api.Object_GetObjectId(p) : 0, class_name(p));
  const char* names[] = {"+x", "-x", "+z", "-z"};
  const float dirs[][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
  for (int i = 0; i < 4; ++i) s += std::format("free {}: {:.1f}\n", names[i], free_distance(dirs[i][0], dirs[i][1], 15.0f, 0.5f));
  // RTTI_ClassInfo layout discovery: the player's class info vs the exported static infos, word by word.
  if (g_api.Object_GetRTTIClassInfo) {
    const void* ci = g_api.Object_GetRTTIClassInfo(p);
    s += std::format("rtti: player={} Player::static={} Character::static={} Entity::static={}\n", ci, g_api.Player_StaticClassInfo ? g_api.Player_StaticClassInfo() : nullptr,
                     g_api.Character_StaticClassInfo ? g_api.Character_StaticClassInfo() : nullptr, g_api.Entity_StaticClassInfo ? g_api.Entity_StaticClassInfo() : nullptr);
    for (const void* info : {ci, g_api.Player_StaticClassInfo ? g_api.Player_StaticClassInfo() : nullptr}) {
      if (!info) continue;
      s += std::format("  {}:", info);
      for (int i = 0; i < 8; ++i) {
        uint64_t w; memcpy(&w, (const char*)info + i * 8, 8);
        const char* str = (const char*)w;
        bool text = w > 0x10000 && !IsBadStringPtrA(str, 64) && str[0] > 0x20 && str[0] < 0x7f;
        s += text ? std::format(" '{}'", std::string(str).substr(0, 40)) : std::format(" {:#x}", w);
      }
      s += "\n";
    }
  }
  return s;
}

// ---- targeting ----
namespace { std::string entity_label(void* e, const std::string& cls); }
namespace { bool is_of_interest(const void* e, const void* ci); bool is_kind_of(const void* ci, const void* base); }
std::string entities_dump(float max_dist) {
  load_api();
  Vec3 me; if (!player_position(me)) return "no player\n";
  alignas(16) unsigned char vb[64] = {};
  MemVec* v = (MemVec*)vb;
  bool ok = false;
  std::string how;
  // Source 1: the player's region asked for everything in a sphere around the player. Read from
  // Engine.dll (tools/dll_dis.py, 2026-08-21): Region::GetEntitiesInSphere forwards to World:: over all
  // regions, Sphere = {Vec3 centre, float radius} in the calling region's coordinates, and mem::vector is
  // std-like {begin, end, cap} (appended to). EntityListType 0 for now.
  Buf base; void* region = nullptr;
  if (g_api.Region_GetEntitiesInSphere && player_world_vec(base, &region)) {
    const Vec3* rp = g_api.WorldVec3_GetRegionPosition(&base);
    struct { Vec3 c; float r; } sphere{*rp, max_dist};
    g_api.Region_GetEntitiesInSphere(region, v, &sphere, false, 0);
    ok = true; how = "Region::GetEntitiesInSphere";
  } else if (void* eng = gd::hooks::engine_object(); eng && g_api.GetEntitiesInPriorFrameFrustum) {
    ok = g_api.GetEntitiesInPriorFrameFrustum(eng, v);
    how = "Engine::GetEntitiesInPriorFrameFrustum";
  } else return "no entity source\n";
  // mem::vector layout is not exported; the raw words tell which of {begin,end,cap} / {data,size,cap} it is.
  uint64_t raw[4]; memcpy(raw, vb, sizeof raw);
  log::writef("entities: {} raw vector words {:#x} {:#x} {:#x} {:#x}", how, raw[0], raw[1], raw[2], raw[3]);
  size_t n = 0;
  if (v->begin && v->end && (uintptr_t)v->end > (uintptr_t)v->begin && (uintptr_t)v->end - (uintptr_t)v->begin < (1u << 20))
    n = (size_t)((char*)v->end - (char*)v->begin) / sizeof(void*);  // {begin, end, cap}
  struct Row { float d; std::string text; };
  std::vector<Row> rows;
  size_t faulted = 0;
  for (size_t i = 0; i < n && i < 4096; ++i) {
    void* e; memcpy(&e, (char*)v->begin + i * sizeof(void*), sizeof e);
    if (!e) continue;
    EntityRaw r{};
    if (!read_entity(e, r)) { ++faulted; log::writef("entities: #{} {} faulted while being read", i, e); continue; }
    if (!r.has_pos) continue;
    float d = std::sqrt((r.pos.x - me.x) * (r.pos.x - me.x) + (r.pos.z - me.z) * (r.pos.z - me.z));
    if (d > max_dist) continue;
    std::string cls = rtti_name(r.ci);
    // Classification readout for the review cursor's object group (FixedActor / Item + IsOfInterest).
    std::string kind;
    if (g_api.FixedActor_StaticClassInfo && is_kind_of(r.ci, g_api.FixedActor_StaticClassInfo())) kind = " [FixedActor";
    else if (g_api.Item_StaticClassInfo && is_kind_of(r.ci, g_api.Item_StaticClassInfo())) kind = " [Item";
    if (!kind.empty()) kind += is_of_interest(e, r.ci) ? " interest]" : " no-interest]";
    rows.push_back({d, std::format("{:6.1f}  id={:<8} {:<12} label='{}' at ({:.1f}, {:.1f}, {:.1f}) {} '{}'{}", d, r.id, cls, entity_label(e, cls),
                                   r.pos.x, r.pos.y, r.pos.z, e, r.name, kind)});
  }
  if (faulted) log::writef("entities: {} objects faulted while being read (skipped)", faulted);
  std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.d < b.d; });
  std::string s = std::format("ok={} rendered={} within {:.0f}: {}\n", ok, n, max_dist, rows.size());
  for (auto& r : rows) s += r.text + "\n";
  return s;  // the vector's storage is leaked on purpose (dev route; the game's allocator owns it)
}
bool set_target(unsigned id) {
  if (!g_controller || !g_api.SetCombatEnemy) return false;
  g_api.SetCombatEnemy(g_controller, id);
  if (g_api.FaceTarget) g_api.FaceTarget(g_controller, id);
  return true;
}
void clear_target() { if (g_controller && g_api.ClearTarget) g_api.ClearTarget(g_controller); }
unsigned current_target() { return g_controller && g_api.GetCombatEnemy ? g_api.GetCombatEnemy(g_controller) : 0; }
std::string target_dump() {
  std::string s = std::format("combat_enemy={}\n", current_target());
  void* p = player();
  if (p && g_api.GetCurrentAttackTarget) {
    unsigned id = 0, extra = 0; Buf wv{};
    g_api.GetCurrentAttackTarget(p, &id, &wv, &extra);
    s += std::format("attack_target id={} at {} extra={}\n", id, wv_text(&wv), extra);
  }
  return s;
}

// ---- the virtual cursor ----
// A target of ours is the cursor parked over the entity on screen each frame (the exe re-resolves its
// combat enemy/ally from the cursor every frame). The entity is re-found by id in a sphere query around the
// player on every tick, so a despawned target simply unlocks.
namespace {
unsigned g_locked_id = 0;
void* g_locked_entity = nullptr;
uint64_t g_lock_frames = 0;

void* find_entity(unsigned id) {
  Buf base; void* region = nullptr;
  if (!g_api.Region_GetEntitiesInSphere || !player_world_vec(base, &region)) return nullptr;
  alignas(16) unsigned char vb[64] = {};
  MemVec* v = (MemVec*)vb;
  const Vec3* rp = g_api.WorldVec3_GetRegionPosition(&base);
  struct { Vec3 c; float r; } sphere{*rp, 40.0f};
  g_api.Region_GetEntitiesInSphere(region, v, &sphere, false, 0);
  if (!(v->begin && v->end && (uintptr_t)v->end > (uintptr_t)v->begin && (uintptr_t)v->end - (uintptr_t)v->begin < (1u << 20))) return nullptr;
  size_t n = (size_t)((char*)v->end - (char*)v->begin) / sizeof(void*);
  for (size_t i = 0; i < n && i < 4096; ++i) {
    void* e; memcpy(&e, (char*)v->begin + i * sizeof(void*), sizeof e);
    if (e && g_api.Object_GetObjectId && g_api.Object_GetObjectId(e) == id) return e;
  }
  return nullptr;
}
bool project(void* entity, float& x, float& y) {
  void* cam = g_game_engine && g_api.GetCamera ? g_api.GetCamera(g_game_engine) : nullptr;
  if (!cam || !g_api.Project || !g_api.Viewport_ctor) return false;
  Buf wv; if (!entity_world_vec(entity, wv)) return false;
  // Lift the point to roughly chest height so the cursor lands on the body, not the feet.
  Vec3 pos; memcpy(&pos, wv.b + 8, sizeof pos); pos.y += 1.0f; memcpy(wv.b + 8, &pos, sizeof pos);
  RECT rc{};
  HWND w = FindWindowA("Grim Dawn", nullptr);
  if (!w || !GetClientRect(w, &rc)) return false;
  Buf vp{};
  g_api.Viewport_ctor(&vp, 0, 0, rc.right, rc.bottom);
  alignas(16) float out[4] = {};
  g_api.Project(cam, out, &wv, &vp);
  x = out[0]; y = out[1];
  return true;
}
}  // namespace

bool lock_target(unsigned id) {
  void* e = find_entity(id);
  if (!e) return false;
  g_locked_id = id; g_locked_entity = e; g_lock_frames = 0;
  return true;
}
void unlock_target() {
  if (g_locked_id) gd::hooks::set_cursor_override(false, 0, 0);
  g_locked_id = 0; g_locked_entity = nullptr;
}
unsigned locked_target() { return g_locked_id; }
void tick() {
  if (!g_locked_id) return;
  if (!in_world()) { unlock_target(); return; }
  // Re-find every 30 frames (the pointer may die); project every frame.
  if (g_lock_frames++ % 30 == 0) { g_locked_entity = find_entity(g_locked_id); if (!g_locked_entity) { unlock_target(); return; } }
  float x, y;
  RECT rc{};
  HWND w = FindWindowA("Grim Dawn", nullptr);
  bool visible = project(g_locked_entity, x, y) && w && GetClientRect(w, &rc) && x >= 0 && y >= 0 && x < (float)rc.right && y < (float)rc.bottom;
  // Only park the cursor on something the camera shows; off-window the override would only confuse the
  // game's hover (the lock itself stays, the readouts say "distant" / "too far away").
  gd::hooks::set_cursor_override(visible, x, y);
}
bool entity_screen_pos(unsigned id, float& x, float& y) {
  void* e = find_entity(id);
  return e && project(e, x, y);
}
// ---- blockage classification ----
namespace {
struct Nearby { void* e; unsigned id; Vec3 pos; float radius; float height; float miny, maxy; std::string cls; std::string name; };
std::vector<Nearby> g_nearby;
ULONGLONG g_nearby_at = 0;
constexpr float kNearbyRadius = 14.0f;
constexpr ULONGLONG kNearbyRefreshMs = 200;
constexpr float kWallHeight = 1.6f;   // a blocker shorter than this (units ~ metres) is an obstacle the character walks round

struct BBoxRaw { float v[6]; bool ok; };
bool read_bbox(void* e, BBoxRaw& b) {
  __try {
    const float* bb = g_api.Entity_GetRegionBoundingBox ? g_api.Entity_GetRegionBoundingBox(e, false) : nullptr;
    if (!bb || IsBadReadPtr(bb, 24)) { b.ok = false; return true; }
    memcpy(b.v, bb, sizeof b.v); b.ok = true;
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) { b.ok = false; return false; }
}
void refresh_nearby() {
  ULONGLONG now = GetTickCount64();
  if (now - g_nearby_at < kNearbyRefreshMs) return;
  g_nearby_at = now;
  g_nearby.clear();
  Buf base; void* region = nullptr;
  if (!g_api.Region_GetEntitiesInSphere || !player_world_vec(base, &region)) return;
  alignas(16) unsigned char vb[64] = {};
  MemVec* v = (MemVec*)vb;
  const Vec3* rp = g_api.WorldVec3_GetRegionPosition(&base);
  struct { Vec3 c; float r; } sphere{*rp, kNearbyRadius};
  g_api.Region_GetEntitiesInSphere(region, v, &sphere, false, 0);
  if (!(v->begin && v->end && (uintptr_t)v->end > (uintptr_t)v->begin && (uintptr_t)v->end - (uintptr_t)v->begin < (1u << 20))) return;
  size_t n = (size_t)((char*)v->end - (char*)v->begin) / sizeof(void*);
  for (size_t i = 0; i < n && i < 4096; ++i) {
    void* e; memcpy(&e, (char*)v->begin + i * sizeof(void*), sizeof e);
    if (!e) continue;
    EntityRaw r{};
    if (!read_entity(e, r) || !r.has_pos) continue;
    std::string cls = rtti_name(r.ci);
    std::string name = r.name;
    if (cls == "Player" || cls == "PlayerSpawnPoint" || name.find("records/fx/") == 0 || name.find("records/triggervolumes/") == 0) continue;
    BBoxRaw bb{};
    if (!read_bbox(e, bb) || !bb.ok) continue;
    // ABBox (measured 2026-08-21): centre Vec3 then half-extents Vec3. Lights report a cube of their radius.
    if (name.find("light") != std::string::npos) continue;
    float hx = bb.v[3], hy = bb.v[4], hz = bb.v[5];
    g_nearby.push_back({e, r.id, r.pos, std::sqrt(hx * hx + hz * hz), hy * 2.0f, bb.v[1] - hy, bb.v[1] + hy, cls, name});
  }
}
const Nearby* blocker_at(const Vec3& stop) {
  refresh_nearby();
  // Render boxes of big decorations (a 20-unit rock) cover everything; among the entities whose footprint
  // reaches the stop, name the SMALLEST one -- it is the likeliest actual blocker.
  const Nearby* best = nullptr;
  for (const Nearby& nb : g_nearby) {
    float dx = nb.pos.x - stop.x, dz = nb.pos.z - stop.z;
    float d = std::sqrt(dx * dx + dz * dz) - nb.radius;  // distance to the footprint's edge
    if (d < 1.0f && (!best || nb.radius < best->radius)) best = &nb;
  }
  return best;
}
}  // namespace

// Obstacle = walkable navmesh exists a little beyond the stop along the probe direction (the character
// walks round it); wall = nothing walkable behind it within kBeyond. Tested at two distances so a thin
// post and a fat rock both read as obstacles.
constexpr float kBeyond1 = 2.0f, kBeyond2 = 4.0f;
BlockKind classify_block(const Vec3& stop, float dir_x, float dir_z) {
  Vec3 b1{stop.x + dir_x * kBeyond1, stop.y, stop.z + dir_z * kBeyond1};
  Vec3 b2{stop.x + dir_x * kBeyond2, stop.y, stop.z + dir_z * kBeyond2};
  return on_navmesh(b1) || on_navmesh(b2) ? BlockKind::Obstacle : BlockKind::Wall;
}
std::string blocks_dump() {
  Vec3 p; if (!player_position(p)) return "no player\n";
  refresh_nearby();
  std::string s = std::format("nearby={} (radius {:.0f})\n", g_nearby.size(), kNearbyRadius);
  const char* names[] = {"+x", "-x", "+z", "-z"};
  const float dirs[][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
  for (int i = 0; i < 4; ++i) {
    float d = free_distance(dirs[i][0], dirs[i][1], 15.0f, 0.5f);
    Vec3 stop{p.x + dirs[i][0] * (d + 0.25f), p.y, p.z + dirs[i][1] * (d + 0.25f)};
    const Nearby* nb = d >= 15.0f ? nullptr : blocker_at(stop);
    const char* kind = d >= 15.0f ? "open" : classify_block(stop, dirs[i][0], dirs[i][1]) == BlockKind::Obstacle ? "OBSTACLE" : "WALL";
    s += std::format("{}: free {:.1f} {}{}", names[i], d, kind, nb ? "" : d >= 15.0f ? "" : " (no entity: level geometry)");
    if (nb) s += std::format("  <- {} h={:.2f} r={:.2f} y[{:.1f}..{:.1f}] '{}'", nb->cls, nb->height, nb->radius, nb->miny, nb->maxy, nb->name);
    s += "\n";
  }
  s += "nearest entities with boxes:\n";
  std::vector<const Nearby*> v; for (auto& nb : g_nearby) v.push_back(&nb);
  std::sort(v.begin(), v.end(), [&](const Nearby* a, const Nearby* b) { return std::hypot(a->pos.x - p.x, a->pos.z - p.z) < std::hypot(b->pos.x - p.x, b->pos.z - p.z); });
  for (size_t i = 0; i < v.size() && i < 12; ++i) {
    BBoxRaw bb{}; read_bbox(v[i]->e, bb);
    s += std::format("  {:5.1f} {:<10} at ({:.1f}, {:.1f}, {:.1f}) raw[{:.2f} {:.2f} {:.2f} | {:.2f} {:.2f} {:.2f}] '{}'\n", std::hypot(v[i]->pos.x - p.x, v[i]->pos.z - p.z), v[i]->cls,
                     v[i]->pos.x, v[i]->pos.y, v[i]->pos.z, bb.v[0], bb.v[1], bb.v[2], bb.v[3], bb.v[4], bb.v[5], v[i]->name);
  }
  return s;
}

// ---- labels ----
namespace {
// The u16 result lands in a 32-byte SSO string the callee constructs; heap storage (capacity > 7) is the
// game's CRT allocation (same MSVC CRT) and is freed here.
bool read_label(void* e, const std::string& cls, char16_t* out, size_t cap) {
  __try {
    alignas(16) unsigned char sb[64] = {};
    MsvcStringW* s = (MsvcStringW*)sb;
    s->capacity = 7;
    MsvcStringW* r = nullptr;
    if (cls == "Monster" && g_api.Monster_GetGameDescription) r = g_api.Monster_GetGameDescription(e, s, false, false);
    else if (cls == "Npc" && g_api.Npc_GetRolloverDescription) r = g_api.Npc_GetRolloverDescription(e, s);
    else if (cls == "Player" && g_api.Player_GetRolloverDescription) r = g_api.Player_GetRolloverDescription(e, s);
    else if (cls == "Item" && g_api.Item_GetGameDescription) r = g_api.Item_GetGameDescription(e, s, false, false);
    if (!r) return false;
    std::u16string_view v = s->view();
    size_t n = v.size() < cap - 1 ? v.size() : cap - 1;
    memcpy(out, v.data(), n * sizeof(char16_t)); out[n] = 0;
    if (s->capacity > 7 && s->u.ptr) free(s->u.ptr);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
std::string entity_label(void* e, const std::string& cls) {
  char16_t buf[512];
  if (!read_label(e, cls, buf, 512)) return {};
  return textcap::speakable(std::u16string_view(buf));
}
}  // namespace

std::string label_of(unsigned id) {
  void* e = find_entity(id);
  if (!e) return {};
  EntityRaw r{};
  if (!read_entity(e, r)) return {};
  return entity_label(e, rtti_name(r.ci));
}

// ---- the review cursor ----
namespace {
unsigned g_reviewed_id = 0;
// Screen-up in world xz (measured against WASD, see in_game.cpp): (-sin yaw, -cos yaw).
void screen_axes(float& fx, float& fz, float& rx, float& rz) {
  float yaw = camera_yaw();
  fx = -std::sin(yaw); fz = -std::cos(yaw); rx = std::cos(yaw); rz = -std::sin(yaw);
}
// RTTI_ClassInfo: +0 vptr, +8 name, +0x10 parent (measured live 2026-08-22: Monster/Npc/Player -> Character
// -> Actor, Entity -> Object). is-a = walk the parents.
bool is_kind_of(const void* ci, const void* base) {
  for (int depth = 0; ci && depth < 16; ++depth) {
    if (ci == base) return true;
    if (IsBadReadPtr(ci, 0x18)) return false;
    memcpy(&ci, (const char*)ci + 0x10, sizeof ci);
  }
  return false;
}
// The slot of a class's IsOfInterest in its own vftable (the export is that class's implementation).
int vt_slot(void** vt, const void* fn) {
  if (!vt || !fn) return -1;
  for (int i = 0; i < 160; ++i) if (vt[i] == fn) return i;
  return -1;
}
bool call_bool_slot(const void* obj, int slot, bool* out) {
  __try { void** vt; memcpy(&vt, obj, sizeof vt); *out = ((bool (*)(const void*))vt[slot])(obj); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool call_bool_fn(const void* obj, bool (*fn)(const void*), bool* out) {
  __try { *out = fn(obj); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// What the game's Interact key would consider: a FixedActor (door, chest, shrine, lever ...) or an Item
// whose IsOfInterest() says so -- virtual, so dispatched through the object's vtable at the slot the class's
// exported implementation occupies in that class's vftable.
bool is_of_interest(const void* e, const void* ci) {
  static int fa_slot = -2, item_slot = -2;
  if (fa_slot == -2) {
    fa_slot = vt_slot(g_api.FixedActor_vftable, (const void*)g_api.FixedActor_IsOfInterest);
    item_slot = vt_slot(g_api.Item_vftable, (const void*)g_api.Item_IsOfInterest);
    log::writef("world: IsOfInterest slots: FixedActor {} Item {}", fa_slot, item_slot);
  }
  bool v = false;
  if (g_api.FixedActor_StaticClassInfo && is_kind_of(ci, g_api.FixedActor_StaticClassInfo())) return fa_slot >= 0 && call_bool_slot(e, fa_slot, &v) && v;
  if (g_api.Item_StaticClassInfo && is_kind_of(ci, g_api.Item_StaticClassInfo())) return item_slot >= 0 && call_bool_slot(e, item_slot, &v) && v;
  return false;
}
bool npc_has_conversation(const void* e) { bool v = false; return g_api.Npc_HasConversation && call_bool_fn(e, g_api.Npc_HasConversation, &v) && v; }
bool in_group(const void* e, const void* ci, const std::string& cls, ScanGroup g) {
  switch (g) {
    case ScanGroup::Enemies: return cls == "Monster";
    case ScanGroup::Neutrals: return cls == "Npc" && npc_has_conversation(e);     // people you can talk to
    case ScanGroup::Bystanders: return cls == "Npc" && !npc_has_conversation(e);  // flavour NPCs
    case ScanGroup::Objects: return is_of_interest(e, ci);
  }
  return false;
}
}  // namespace

int clock_hour(const Vec3& p) {
  Vec3 me; if (!player_position(me)) return 12;
  float fx, fz, rx, rz; screen_axes(fx, fz, rx, rz);
  float dx = p.x - me.x, dz = p.z - me.z;
  float ahead = dx * fx + dz * fz, right = dx * rx + dz * rz;
  float ang = std::atan2(right, ahead);  // 0 = ahead, +pi/2 = right
  int hour = (int)std::lround(ang / (2.0f * 3.14159265f) * 12.0f);
  hour = ((hour % 12) + 12) % 12;
  return hour == 0 ? 12 : hour;
}

std::vector<ScanItem> scan(ScanGroup group, float radius) {
  std::vector<ScanItem> out;
  Vec3 me; Buf base; void* region = nullptr;
  if (!player_position(me) || !g_api.Region_GetEntitiesInSphere || !player_world_vec(base, &region)) return out;
  alignas(16) unsigned char vb[64] = {};
  MemVec* v = (MemVec*)vb;
  const Vec3* rp = g_api.WorldVec3_GetRegionPosition(&base);
  struct { Vec3 c; float r; } sphere{*rp, radius};
  g_api.Region_GetEntitiesInSphere(region, v, &sphere, false, 0);
  if (!(v->begin && v->end && (uintptr_t)v->end > (uintptr_t)v->begin && (uintptr_t)v->end - (uintptr_t)v->begin < (1u << 20))) return out;
  size_t n = (size_t)((char*)v->end - (char*)v->begin) / sizeof(void*);
  for (size_t i = 0; i < n && i < 4096; ++i) {
    void* e; memcpy(&e, (char*)v->begin + i * sizeof(void*), sizeof e);
    if (!e) continue;
    EntityRaw r{};
    if (!read_entity(e, r) || !r.has_pos) continue;
    std::string cls = rtti_name(r.ci), record = r.name;
    if (!in_group(e, r.ci, cls, group)) continue;
    float d = std::sqrt((r.pos.x - me.x) * (r.pos.x - me.x) + (r.pos.z - me.z) * (r.pos.z - me.z));
    if (d > radius) continue;
    // Enemies are Monsters the game's faction manager calls foes of the player (guards are Monsters too).
    if (group == ScanGroup::Enemies) {
      void* fm = g_game_engine && g_api.GetFactionManager ? g_api.GetFactionManager(g_game_engine) : nullptr;
      void* p = player();
      unsigned pid = p && g_api.Object_GetObjectId ? g_api.Object_GetObjectId(p) : 0;
      if (fm && g_api.FactionManager_IsFoe && pid && !g_api.FactionManager_IsFoe(fm, pid, r.id, false)) continue;
    }
    std::string label = entity_label(e, cls);  // an unlabelled object is read by its class name (cycle_review)
    out.push_back({r.id, cls, label, record, r.pos, d});
  }
  std::sort(out.begin(), out.end(), [](const ScanItem& a, const ScanItem& b) { return a.dist < b.dist; });
  return out;
}

static std::string_view group_label(ScanGroup g) {
  switch (g) {
    case ScanGroup::Enemies: return gd::strings::kEnemies;
    case ScanGroup::Neutrals: return gd::strings::kNeutrals;
    case ScanGroup::Bystanders: return gd::strings::kBystanders;
    default: return gd::strings::kObjects;
  }
}

std::string cycle_review(ScanGroup group, int dir) {
  std::vector<ScanItem> items = scan(group);
  gd::core::MessageBuilder m;
  if (items.empty()) { unlock_target(); g_reviewed_id = 0; gd::strings::push_nothing_nearby(m, group_label(group)); return m.build(); }
  // Continue from the current target when it is in this group; otherwise enter at the nearest (or,
  // cycling backward into a fresh group, the farthest). Distances are live, so the order self-heals.
  int idx = -1;
  for (size_t i = 0; i < items.size(); ++i) if (items[i].id == g_reviewed_id) { idx = (int)i; break; }
  int count = (int)items.size();
  idx = idx < 0 ? (dir >= 0 ? 0 : count - 1) : ((idx + dir) % count + count) % count;
  const ScanItem& it = items[(size_t)idx];
  g_reviewed_id = it.id;
  lock_target(it.id);
  ping_reviewed();  // every landing plays the route ping, like wotr
  std::string label = it.label.empty() ? it.cls : it.label;
  gd::strings::push_scan_item(m, label, it.dist, clock_hour(it.pos), idx + 1, count, !on_screen(it.id));
  return m.build();
}
unsigned reviewed_id() { return g_reviewed_id; }

// The one pan/gain rule for positioned sounds and voices (wotr's sonar): pan by the ear-frame bearing of the
// point from the player, gain ref/(ref+dist) with ref = 10 ft in units, never below 0.15.
void ear_frame(const Vec3& p, float& pan, float& gain) {
  Vec3 me; pan = 0.0f; gain = 1.0f;
  if (!player_position(me)) return;
  float fx, fz, rx, rz; screen_axes(fx, fz, rx, rz);
  float dx = p.x - me.x, dz = p.z - me.z;
  float dist = std::sqrt(dx * dx + dz * dz);
  float right = dx * rx + dz * rz;
  pan = dist > 0.01f ? right / dist : 0.0f;
  constexpr float kRef = 10.0f * 0.3048f / 0.67f;  // 10 ft in units (~0.67 m per unit)
  gain = kRef / (kRef + dist);
  if (gain < 0.15f) gain = 0.15f;
}
static float g_voice_near = 9.0f, g_voice_far = 32.0f, g_voice_floor = 0.4f;
float voice_gain(float dist) {
  if (dist <= g_voice_near) return 1.0f;
  if (dist >= g_voice_far) return g_voice_floor;
  return 1.0f - (1.0f - g_voice_floor) * (dist - g_voice_near) / (g_voice_far - g_voice_near);
}
void set_voice_rolloff(float near_d, float far_d, float floor_g) {  // (near/far are windows.h macros)
  if (near_d >= 0) g_voice_near = near_d;
  if (far_d > g_voice_near) g_voice_far = far_d;
  if (floor_g >= 0 && floor_g <= 1) g_voice_floor = floor_g;
}
std::string voice_rolloff() { return std::format("near={:.1f} far={:.1f} floor={:.2f}", g_voice_near, g_voice_far, g_voice_floor); }
// A WorldVec3 (Region* + region-relative Vec3) to world space; false without a region (GetWorldPosition on a
// region-less WorldVec3 hung the game thread once).
bool world_point(const void* worldvec3, Vec3& out) {
  if (!worldvec3) return false;
  Buf b{}; memcpy(b.b, worldvec3, 32);
  void* region; memcpy(&region, b.b, sizeof region);
  if (!region) return false;
  out = world_pos_of(b);
  return true;
}

std::string ping_reviewed() {
  if (!g_reviewed_id) return {};
  void* e = find_entity(g_reviewed_id);
  Vec3 me, target;
  Buf wv;
  if (!e || !player_position(me) || !entity_world_vec(e, wv)) return {};
  target = world_pos_of(wv);
  float dx = target.x - me.x, dz = target.z - me.z;
  float dist = std::sqrt(dx * dx + dz * dz);
  // Route kind: every half unit of the straight line on the navmesh = straight walk; the target's own
  // spot walkable but the line not = path around; the target off the mesh = unreachable (a cliff, water,
  // inside a wall). Sight rays are a later refinement.
  const char* kind = "unreachable";
  Vec3 near_target{target.x - (dist > 0.5f ? dx / dist * 0.5f : 0), target.y, target.z - (dist > 0.5f ? dz / dist * 0.5f : 0)};
  if (on_navmesh(near_target) || on_navmesh(target)) {
    bool straight = true;
    for (float d = 0.5f; d < dist - 0.5f; d += 0.5f)
      if (!on_navmesh(Vec3{me.x + dx / dist * d, me.y, me.z + dz / dist * d})) { straight = false; break; }
    kind = straight ? "straight" : "path";
  }
  float pan, vol; ear_frame(target, pan, vol);
  gd::audio::play_sample(gd::audio::module_dir() + "assets\\audio\\review_" + kind + ".wav", vol, pan);
  return kind;
}

bool on_screen(unsigned id) {
  float x, y;
  if (!entity_screen_pos(id, x, y)) return false;
  RECT rc{};
  HWND w = FindWindowA("Grim Dawn", nullptr);
  if (!w || !GetClientRect(w, &rc)) return false;
  return x >= 0 && y >= 0 && x < (float)rc.right && y < (float)rc.bottom;
}
void mouse_key(int button, bool held) {
  static bool key_down[3] = {};
  bool& prev = key_down[button == 2 ? 2 : 1];
  bool edge = held && !prev;
  prev = held;
  if (held && g_locked_id && !on_screen(g_locked_id)) {
    if (edge) gd::speech::speak(gd::strings::kTooFarAway, true);  // the lock is on something the camera does not show
    gd::hooks::set_mouse_hold(button, false);
    return;
  }
  gd::hooks::set_mouse_hold(button, held);
}

// ---- camera lock ----
// Decided 2026-08-22 (zoom does not change what the player hears, so nothing is lost): the camera sits at the
// far end of its zoom range and at yaw 0 (axis-aligned, "north" up), re-applied whenever the game drifts
// them. GameCamera layout (Game.dll, read from SetZoom/ResetZoom): zoom range +0x590..+0x594, current
// fraction +0x584; SetZoom(value) clamps into the range.
namespace {
constexpr size_t kCam_ZoomMin = 0x590, kCam_ZoomMax = 0x594, kCam_ZoomT = 0x584;
bool read_cam(const void* cam, size_t off, float* out) {
  __try { *out = *(const float*)((const char*)cam + off); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
float cam_float(void* cam, size_t off) { float v = 0; return read_cam(cam, off, &v) ? v : 0; }
}  // namespace
void pin_camera() {
  void* cam = g_game_engine && g_api.GetCamera ? g_api.GetCamera(g_game_engine) : nullptr;
  if (!cam) return;
  if (g_api.SetZoom && cam_float(cam, kCam_ZoomT) < 0.999f) g_api.SetZoom(cam, cam_float(cam, kCam_ZoomMax));
  if (g_api.SetCameraYaw && g_api.GetCameraYaw && std::fabs(g_api.GetCameraYaw(cam)) > 0.001f) g_api.SetCameraYaw(cam, 0.0f);
}

// ---- conversations ----
ConvState conversation_state() {
  std::lock_guard lk(g_conv_mu);
  // The frame being filled is more current than the last complete one once it has any text.
  return g_conv_cur.steps.empty() ? g_conv_last : g_conv_cur;
}
bool in_conversation() {
  std::lock_guard lk(g_conv_mu);
  uint64_t f = gd::hooks::frame();
  const ConvState& s = g_conv_cur.steps.empty() ? g_conv_last : g_conv_cur;
  return !s.steps.empty() && f - s.frame <= 3;
}
std::string conversation_dump() {
  ConvState s = conversation_state();
  std::string out = std::format("conversation={} frame={} now={} in_conversation={} steps={} children_of={} children={}\n", s.conversation, s.frame, gd::hooks::frame(),
                                in_conversation(), s.steps.size(), s.children_of, s.children.size());
  for (const ConvStep& st : s.steps)
    out += std::format("  step={} type={} tag='{}' avail={} used={} parent={} text='{}'\n", st.step, st.type, st.type_tag, st.available, st.used, st.parent,
                       st.text.size() > 90 ? st.text.substr(0, 90) + "..." : st.text);
  for (void* c : s.children) out += std::format("  child {}\n", c);
  return out;
}

std::string project_dump(unsigned id) {
  float x = 0, y = 0;
  void* e = find_entity(id);
  if (!e) return std::format("id {} not found near the player\n", id);
  Vec3 p; Buf wv; if (entity_world_vec(e, wv)) p = world_pos_of(wv);
  bool ok = project(e, x, y);
  return std::format("id {} at world ({:.1f}, {:.1f}, {:.1f}) -> screen ({:.0f}, {:.0f}) ok={} locked={}\n", id, p.x, p.y, p.z, x, y, ok, g_locked_id);
}
}  // namespace gd::world
