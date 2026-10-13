#pragma once
#include "../ECS/ECS.h"
#include "../Editor/SceneEditor.h"
#include "../AssetStore/AssetStore.h"
#include <SDL.h>

class RenderGUISystem : public System {
    char path[512]{};
    char filter[128]{};
    bool initialized = false;
    bool shortcutSaveRequested = false;
    bool dragging = false;
    Entity dragEntity;
    glm::vec2 dragOffset{0};
    void Toolbar(SceneEditor& editor, int width);
    void Hierarchy(SceneEditor& editor, Registry& registry, const SDL_Rect& camera, int height);
    void Inspector(SceneEditor& editor, Registry& registry, AssetStore& assets, SDL_Rect& camera, int width, int height);
    void Viewport(SceneEditor& editor, SDL_Rect& camera, int width, int height);
public:
    static void ConfigureUI();
    std::string CurrentPath() const { return path; }
    void SaveFromShortcut(SceneEditor&) { shortcutSaveRequested = true; }
    void Update(SceneEditor& editor, Registry& registry, AssetStore& assets, SDL_Rect& camera);
};
