#pragma once
#include "SceneData.h"
#include "../ECS/ECS.h"
#include "../AssetStore/AssetStore.h"
#include <limits>
#include <sol/sol.hpp>

class SceneEditor {
    Registry& registry;
    AssetStore& assets;
    sol::state& lua;
    int baseLevel;
    Entity selected;
    SceneData initial;
    bool paused = false, stepRequested = false;
    enum class Request { None, Save, Load, Reset } request = Request::None;
    std::string requestPath;
    SceneEntityData CaptureEntity(Entity entity) const;
    std::optional<sol::function> ScriptFor(const SceneEntityData& data) const;
    Entity Spawn(const SceneEntityData& data, const std::optional<sol::function>& script);
    void Restore(const SceneData& scene);
public:
    bool dirty = false;
    bool showColliders = true;
    bool snap = false;
    float grid = 16;
    std::string status = "在列表或画面中选择对象；拖动移动，中键平移视图。";
    SceneEditor(Registry& registry, AssetStore& assets, sol::state& lua, int level);
    std::vector<Entity> Entities() const;
    Entity Selected() const;
    void Select(Entity entity);
    bool IsPaused() const { return paused; }
    void SetPaused(bool value) { paused = value; if (!value) stepRequested = false; }
    void RequestStep() { paused = true; stepRequested = true; }
    bool ConsumeStep() { bool result = stepRequested; stepRequested = false; return result; }
    void MarkEdited() { paused = true; dirty = true; }
    Entity Create(ColliderShape shape, glm::vec2 position);
    Entity DuplicateSelected();
    void DeleteSelected();
    void RestoreOriginalScript();
    void SetTagAndGroup(const std::string& tag, const std::string& group);
    void MoveSelected(glm::vec2 position);
    void FocusSelected(SDL_Rect& camera) const;
    Entity Pick(glm::vec2 screen, const SDL_Rect& camera) const;
    SceneData Capture() const;
    bool Save(const std::string& path);
    bool Load(const std::string& path);
    bool Reset();
    void RequestSave(std::string path) { request = Request::Save; requestPath = std::move(path); }
    void RequestLoad(std::string path) { request = Request::Load; requestPath = std::move(path); }
    void RequestReset() { request = Request::Reset; }
    void ApplyRequests();
    int BaseLevel() const { return baseLevel; }
};
