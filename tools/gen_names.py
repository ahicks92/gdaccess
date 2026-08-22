"""Generate src/gd_names.h: decorated export names for the symbols we hook/call,
resolved by regex over the undecorated listings in tools/exports (run gen_exports.py first).
Each entry must match exactly one export; the build fails loudly otherwise."""
import re, os, sys
HERE = os.path.dirname(__file__)
ENTRIES = [
    # (identifier, dll, regex over undecorated signature)
    ("LocalizationManager_GetText", "Engine", r"GAME::LocalizationManager::GetText\(char const \*"),
    ("LocalizationManager_LocalizeWithoutParams", "Engine", r"GAME::LocalizationManager::LocalizeWithoutParams\(char const \*"),
    ("Display_Update", "Engine", r"GAME::Display::Update\(void\)"),
    ("Engine_Update", "Engine", r"GAME::Engine::Update\(class GAME::Sphere const"),
    # ---- in-game objects (src/world.cpp) ----
    ("GameEngine_Update", "Game", r"GAME::GameEngine::Update\(int\)"),
    ("GameEngine_GetMainPlayer", "Game", r"GAME::GameEngine::GetMainPlayer\(void\)"),
    ("GameEngine_GetCamera", "Game", r"GAME::GameEngine::GetCamera\(void\)"),
    ("GameEngine_GetAllTargetsInRadius", "Game", r"GAME::GameEngine::GetAllTargetsInRadius\("),
    ("ControllerPlayer_Update", "Game", r"GAME::ControllerPlayer::Update\(int\)"),
    ("ControllerPlayer_SetCombatEnemy", "Game", r"GAME::ControllerPlayer::SetCombatEnemy\("),
    ("ControllerPlayer_GetCombatEnemy", "Game", r"GAME::ControllerPlayer::GetCombatEnemy\("),
    ("ControllerPlayer_ClearTarget", "Game", r"GAME::ControllerPlayer::ClearTarget\("),
    ("ControllerPlayer_FaceTarget", "Game", r"GAME::ControllerPlayer::FaceTarget\("),
    ("ControllerPlayer_HandleActionFromJoystick", "Game", r"GAME::ControllerPlayer::HandleActionFromJoystick\("),
    ("ControllerPlayer_HandleActionFromMouse", "Game", r"GAME::ControllerPlayer::HandleActionFromMouse\("),
    ("ControllerPlayer_SetControllerDirection", "Game", r"GAME::ControllerPlayer::SetControllerDirection\("),
    ("ControllerPlayer_GetControllerDirection", "Game", r"GAME::ControllerPlayer::GetControllerDirection\("),
    ("ControllerPlayer_SetControllerMovementLength", "Game", r"GAME::ControllerPlayer::SetControllerMovementLength\("),
    ("Entity_GetCoords", "Engine", r"GAME::Entity::GetCoords\(void\)"),
    ("Entity_GetRegion", "Engine", r"GAME::Entity::GetRegion\(void\)"),
    ("Character_GetFootCoords", "Game", r"GAME::Character::GetFootCoords\(bool\)"),
    ("Character_GetCurrentLife", "Game", r"GAME::Character::GetCurrentLife\(void\)"),
    ("Character_GetLifeLimit", "Game", r"GAME::Character::GetLifeLimit\(void\)"),
    ("Character_GetCurrentMana", "Game", r"GAME::Character::GetCurrentMana\(void\)"),   # "Energy" in the UI
    ("Character_GetManaLimit", "Game", r"GAME::Character::GetManaLimit\(void\)"),
    # combat text: the floating numbers / Miss / Dodge / Block are GameEvents of type 0x1b (src/combat.cpp)
    ("EventManager_Send", "Engine", r"GAME::EventManager::Send\(struct GAME::GameEvent const"),
    ("Player_GetPlayerName", "Game", r"GAME::Player::GetPlayerName\(void\)"),
    ("Region_GetName", "Engine", r"GAME::Region::GetName\(void\)"),
    ("NavManager_Get", "Engine", r"GAME::Singleton<class GAME::NavManager>::Get\(void\)"),
    ("NavManager_IsPointOnPathMesh", "Engine", r"GAME::NavManager::IsPointOnPathMesh\("),
    ("NavManager_FindStraightMovePoint", "Engine", r"GAME::NavManager::FindStraightMovePoint\(class GAME::WorldVec3 const & __ptr64,class GAME::WorldVec3 const & __ptr64,class GAME::WorldVec3 & __ptr64\)"),
    ("NavManager_FindClosestPointOnPathMesh", "Engine", r"GAME::NavManager::FindClosestPointOnPathMesh\("),
    ("WorldVec3_ctor", "Engine", r"GAME::WorldVec3::WorldVec3\(class GAME::Region \* __ptr64,class GAME::Vec3 const & __ptr64\)"),
    ("WorldVec3_GetWorldPosition", "Engine", r"GAME::WorldVec3::GetWorldPosition\(void\)"),
    ("WorldVec3_GetRegion", "Engine", r"GAME::WorldVec3::GetRegion\(void\)"),
    ("WorldVec3_GetRegionPosition", "Engine", r"GAME::WorldVec3::GetRegionPosition\(void\)"),
    ("WorldVec3_PutOnFloor", "Engine", r"GAME::WorldVec3::PutOnFloor\(void\)"),
    ("WorldCoords_GetRegionCoords", "Engine", r"GAME::WorldCoords::GetRegionCoords\(void\)"),
    ("WorldCamera_GetCameraYaw", "Engine", r"GAME::WorldCamera::GetCameraYaw\(void\)"),
    ("Engine_GetAreaNameTag", "Engine", r"GAME::Engine::GetAreaNameTag\(void\)"),
    # ---- targeting / entities ----
    ("World_Update", "Engine", r"GAME::World::Update\(class mem::vector<class GAME::Sphere>"),
    ("World_GetEntitiesInSphere", "Engine", r"GAME::World::GetEntitiesInSphere\("),
    ("Engine_GetEntitiesInPriorFrameFrustum", "Engine", r"GAME::Engine::GetEntitiesInPriorFrameFrustum\("),
    ("Region_GetEntitiesInSphere", "Engine", r"GAME::Region::GetEntitiesInSphere\("),
    ("Object_GetObjectId", "Engine", r"GAME::Object::GetObjectId\(void\)"),
    ("Entity_GetRegionBoundingBox", "Engine", r"GAME::Entity::GetRegionBoundingBox\(bool\)"),
    ("Object_GetObjectName", "Engine", r"GAME::Object::GetObjectName\(void\)"),
    ("Object_GetRTTIClassInfo", "Engine", r"GAME::Object::GetRTTIClassInfo\(void\)"),
    ("Entity_GetStaticClassInfo", "Engine", r"GAME::Entity::GetStaticClassInfo\(void\)"),
    ("Character_GetStaticClassInfo", "Game", r"GAME::Character::GetStaticClassInfo\(void\)"),
    ("Monster_GetStaticClassInfo", "Game", r"GAME::Monster::GetStaticClassInfo\(void\)"),
    ("Npc_GetStaticClassInfo", "Game", r"GAME::Npc::GetStaticClassInfo\(void\)"),
    ("Player_GetStaticClassInfo", "Game", r"GAME::Player::GetStaticClassInfo\(void\)"),
    ("Character_GetCurrentAttackTarget", "Game", r"GAME::Character::GetCurrentAttackTarget\("),
    ("Character_IsAlive", "Game", r"GAME::Character::IsAlive\(void\)"),
    ("GameEngine_GetFactionManager", "Game", r"GAME::GameEngine::GetFactionManager\(void\)"),
    ("FactionManager_IsFoe", "Game", r"GAME::FactionManager::IsFoe\(unsigned int,unsigned int,bool\)"),
    # ---- conversations (structured source for the dialog screen) ----
    ("Conversation_GetText", "Game", r"GAME::Conversation::GetText\(class GAME::ConversationStep const"),
    ("Conversation_GetSteps", "Game", r"GAME::Conversation::GetSteps\(class GAME::ConversationStep \* __ptr64,class mem::vector"),
    ("Conversation_GetRoot", "Game", r"GAME::Conversation::GetRoot\(void\)"),
    ("ConversationStep_GetType", "Game", r"GAME::ConversationStep::GetType\(void\)"),
    ("ConversationStep_GetTypeTag", "Game", r"GAME::ConversationStep::GetTypeTag\(void\)"),
    ("ConversationStep_IsAvailable", "Game", r"GAME::ConversationStep::IsAvailable\(void\)"),
    ("ConversationStep_IsUsed", "Game", r"GAME::ConversationStep::IsUsed\(void\)"),
    ("ConversationStep_GetParent", "Game", r"GAME::ConversationStep::GetParent\(void\)"),
    ("ConversationStep_GetSteps", "Game", r"GAME::ConversationStep::GetSteps\(void\)"),
    ("ConversationStep_GetFlags", "Game", r"GAME::ConversationStep::GetFlags\(void\)"),
    ("Character_GetConversation", "Game", r"GAME::Character::GetConversation\(void\)"),
    # ---- review cursor classification (src/world.cpp): the Interact key's own filter ----
    ("FixedActor_GetStaticClassInfo", "Game", r"GAME::FixedActor::GetStaticClassInfo\(void\)"),
    ("Item_GetStaticClassInfo", "Game", r"GAME::Item::GetStaticClassInfo\(void\)"),
    ("FixedActor_IsOfInterest", "Game", r"GAME::FixedActor::IsOfInterest\(void\)"),
    ("Item_IsOfInterest", "Game", r"GAME::Item::IsOfInterest\(void\)"),
    ("FixedActor_vftable", "Game", r"GAME::FixedActor::`vftable'\{for `GAME::Object'\}"),
    ("Item_vftable", "Game", r"GAME::Item::`vftable'\{for `GAME::Object'\}"),
    ("Npc_HasConversation", "Game", r"GAME::Npc::HasConversation\(void\)"),
    # ---- camera zoom presets (src/world.cpp) ----
    ("GameCamera_SetZoom", "Game", r"GAME::GameCamera::SetZoom\(float\)"),
    ("GameCamera_ResetZoom", "Game", r"GAME::GameCamera::ResetZoom\(void\)"),
    ("GameCamera_SetCameraYaw", "Game", r"GAME::GameCamera::SetCameraYaw\(float\)"),
    # ---- message boxes (src/exe_ui.cpp): the exported DialogManager behind the exe's prompt box ----
    ("GameEngine_GetDialogManager", "Game", r"GAME::GameEngine::GetDialogManager\(void\)"),
    ("DialogManager_GetNumDialog", "Game", r"GAME::DialogManager::GetNumDialog\(void\)"),
    ("DialogManager_PeekTopDialog", "Game", r"GAME::DialogManager::PeekTopDialog\(void\)"),
    ("DialogManager_AddResponse", "Game", r"GAME::DialogManager::AddResponse\("),
    ("DialogManager_RemoveTopDialog", "Game", r"GAME::DialogManager::RemoveTopDialog\(void\)"),
    # ---- labels (u16 strings by value -> hidden return pointer) ----
    ("Monster_GetGameDescription", "Game", r"GAME::Monster::GetGameDescription\(bool,bool\)"),
    ("Npc_GetRolloverDescription", "Game", r"GAME::Npc::GetRolloverDescription\(void\)"),
    ("Player_GetRolloverDescription", "Game", r"GAME::Player::GetRolloverDescription\(void\)"),
    ("Item_GetGameDescription", "Game", r"GAME::Item::GetGameDescription\(bool,bool\)"),
    ("Object_vftable", "Engine", r"const GAME::Object::`vftable'"),
    ("WorldCamera_Project", "Engine", r"GAME::WorldCamera::Project\("),
    ("Viewport_ctor", "Engine", r"GAME::Viewport::Viewport\(int,int,int,int\)"),
    ("Engine_ProcessUserInput", "Engine", r"GAME::Engine::ProcessUserInput\("),
    ("Display_Render", "Engine", r"GAME::Display::Render\("),
    ("Display_HandleKeyEvent", "Engine", r"GAME::Display::HandleKeyEvent\("),
    ("Display_HandleMouseEvent", "Engine", r"GAME::Display::HandleMouseEvent\("),
    ("ButtonEvent_GetText", "Engine", r"GAME::InputDevice::ButtonEvent::GetText\(void\)"),
    ("InputDevice_GetDefaultButtonName", "Engine", r"GAME::InputDevice::GetDefaultButtonName\("),
    ("DirectInputDevice_GetButtonName", "DirectInput", r"DirectInputDevice::GetButtonName\("),
    ("RenderText2d_XY_C_U16", "Engine", r"RenderText2d\(int,int,class GAME::Color const & __ptr64,unsigned short const \* __ptr64,class GAME::GraphicsFont2"),
    ("RenderText2d_XY_CC_U16", "Engine", r"RenderText2d\(int,int,class GAME::Color const & __ptr64,class GAME::Color const & __ptr64,unsigned short const \*"),
    ("RenderText2d_Rect_C_U16", "Engine", r"RenderText2d\((class|struct) GAME::Rect,class GAME::Color const & __ptr64,unsigned short const \*"),
    ("RenderText2d_Rect_CC_U16", "Engine", r"RenderText2d\((class|struct) GAME::Rect,class GAME::Color const & __ptr64,class GAME::Color const & __ptr64,unsigned short const \*"),
    ("RenderText2d_XY_U16_FontName", "Engine", r"RenderText2d\(int,int,unsigned short const \* __ptr64,class std::basic_string<char,[^)]*__ptr64,float,enum GAME::GraphicsXAlign"),
    ("RenderText2d_XY_U16_FontName2", "Engine", r"RenderText2d\(int,int,unsigned short const \* __ptr64,class std::basic_string<char,[^)]*float,float,enum"),
    ("RenderText2d_Rect_U16_FontName", "Engine", r"RenderText2d\((class|struct) GAME::Rect,unsigned short const \* __ptr64,class std::basic_string<char"),
    ("RenderText2dBox_XY_C_U16_Font", "Engine", r"RenderText2dBox\(int,int,class GAME::Color const & __ptr64,unsigned short const \* __ptr64,class GAME::GraphicsFont2"),
    ("RenderText2dBox_XY_C_U16_FontName", "Engine", r"RenderText2dBox\(int,int,class GAME::Color const & __ptr64,unsigned short const \* __ptr64,class std::basic_string<char"),
    ("RenderColoredText2d_U16", "Engine", r"RenderColoredText2d\(int,int,class std::basic_string<unsigned short"),
    ("RenderText2dParagraph", "Engine", r"RenderText2dParagraph\("),
    ("DirectInputDevice_AllowShortcutKeys", "DirectInput", r"DirectInputDevice::AllowShortcutKeys\(bool\)"),
    ("DirectInputDevice_keyboardHook", "DirectInput", r"DirectInputDevice::keyboardHook$"),
    ("DirectInputDevice_GetNumKeyEvents", "DirectInput", r"DirectInputDevice::GetNumKeyEvents\(void\)"),
    ("DirectInputDevice_GetKeyEvent", "DirectInput", r"DirectInputDevice::GetKeyEvent\(int\)"),
    ("DirectInputDevice_GetNumMouseEvents", "DirectInput", r"DirectInputDevice::GetNumMouseEvents\(void\)"),
    ("ButtonEvent_dtor", "Engine", r"GAME::InputDevice::ButtonEvent::~ButtonEvent\(void\)"),
    ("ButtonEvent_ctor", "Engine", r"GAME::InputDevice::ButtonEvent::ButtonEvent\(void\)"),
    ("MouseEvent_ctor", "Engine", r"GAME::InputDevice::MouseEvent::MouseEvent\(void\)"),
    ("DirectInputDevice_IsButtonDown", "DirectInput", r"DirectInputDevice::IsButtonDown\("),
    ("DirectInputDevice_GetCursorPosition", "DirectInput", r"DirectInputDevice::GetCursorPosition\("),
    ("DirectInputDevice_GetMouseEvent", "DirectInput", r"DirectInputDevice::GetMouseEvent\(int\)"),
    ("DirectInputDevice_keyboardHookRefCount", "DirectInput", r"DirectInputDevice::keyboardHookRefCount$"),
]
tables = {}
for dll in ("Engine", "Game", "DirectInput"):
    rows = []
    with open(os.path.join(HERE, "exports", f"{dll}.x64.txt"), encoding="utf-8") as f:
        for line in f:
            und, _, dec = line.rstrip("\n").partition("\t")
            rows.append((und, dec))
    tables[dll] = rows
out = ["// GENERATED by tools/gen_names.py -- do not edit", "#pragma once", "namespace gd::names {"]
ok = True
for ident, dll, rx in ENTRIES:
    m = [(u, d) for u, d in tables[dll] if re.search(rx, u)]
    if len(m) != 1:
        ok = False
        print(f"!! {ident}: {len(m)} matches", file=sys.stderr)
        for u, d in m[:6]: print("     ", u[:200], file=sys.stderr)
        continue
    out.append(f'// {m[0][0][:220]}')
    out.append(f'inline constexpr const char* {ident}_DLL = "{dll}.dll";')
    out.append(f'inline constexpr const char* {ident} = "{m[0][1]}";')
out.append("}  // namespace gd::names")
if not ok: sys.exit(1)
with open(os.path.join(HERE, "..", "src", "gd_names.h"), "w", encoding="utf-8") as f: f.write("\n".join(out) + "\n")
print(f"wrote src/gd_names.h with {len(ENTRIES)} names")
