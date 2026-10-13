#include "SceneEditor.h"
#include "SceneSerializer.h"
#include "../Components/CameraFollowComponent.h"
#include "../Components/ScriptComponent.h"
#include "../Physics/EntityShape.h"
#include "../Time/SimulationClock.h"
#include <filesystem>

namespace {
template<typename T> std::optional<T> Copy(Entity entity) {
    auto* value = entity.TryGetComponent<T>();
    return value ? std::optional<T>(*value) : std::nullopt;
}
template<typename T> void Add(Entity entity, const std::optional<T>& value) { if (value) entity.AddComponent<T>(*value); }
}
SceneEditor::SceneEditor(Registry& registry, AssetStore& assets, sol::state& lua, int level)
    : registry(registry), assets(assets), lua(lua), baseLevel(level) { initial = Capture(); }
std::vector<Entity> SceneEditor::Entities() const {
    std::vector<Entity> result;
    for (auto entity : registry.GetEntities()) if (entity.HasComponent<SceneObjectComponent>()) result.push_back(entity);
    return result;
}
Entity SceneEditor::Selected() const { return selected.IsValid() && !selected.IsPendingRemoval() ? selected : Entity(); }
void SceneEditor::Select(Entity entity) { selected = entity.HasComponent<SceneObjectComponent>() ? entity : Entity(); }
SceneEntityData SceneEditor::CaptureEntity(Entity entity) const {
    SceneEntityData data;
    data.object = entity.GetComponent<SceneObjectComponent>();
    data.tag = registry.GetEntityTag(entity); data.group = registry.GetEntityGroup(entity);
    data.transform = Copy<TransformComponent>(entity); data.body = Copy<RigidBodyComponent>(entity);
    data.sprite = Copy<SpriteComponent>(entity); data.collider = Copy<ColliderComponent>(entity);
    data.animation = Copy<AnimationComponent>(entity); data.health = Copy<HealthComponent>(entity);
    data.emitter = Copy<ProjectileEmitterComponent>(entity); data.keyboard = Copy<KeyboardControlledComponent>(entity);
    data.text = Copy<TextLabelComponent>(entity); data.cameraFollow = entity.HasComponent<CameraFollowComponent>();
    data.primitive = Copy<PrimitiveRenderComponent>(entity);
    if (auto* script = entity.TryGetComponent<ScriptComponent>()) { data.hasScript = true; data.scriptEnabled = script->enabled; }
    return data;
}
SceneData SceneEditor::Capture() const {
    SceneData data; data.baseLevel = baseLevel; data.simulationSeconds = SimulationClock::Seconds();
    for (auto entity : Entities()) data.entities.push_back(CaptureEntity(entity));
    return data;
}
std::optional<sol::function> SceneEditor::ScriptFor(const SceneEntityData& data) const {
    if (!data.hasScript) return std::nullopt;
    if (data.object.scriptIndex < 0) throw std::runtime_error("此脚本没有可保存的原关卡来源");
    sol::table level = lua["Level"];
    sol::table entities = level["entities"];
    sol::optional<sol::table> source = entities[data.object.scriptIndex];
    if (!source) throw std::runtime_error("原关卡中不存在对应脚本对象");
    sol::table components = source.value()["components"];
    sol::optional<sol::table> scripts = components["on_update_script"];
    if (!scripts) throw std::runtime_error("原关卡中不存在对应更新脚本");
    sol::object function = scripts.value()[0];
    if (function.get_type() != sol::type::function) throw std::runtime_error("脚本来源不是有效函数");
    return function.as<sol::function>();
}
Entity SceneEditor::Spawn(const SceneEntityData& data, const std::optional<sol::function>& script) {
    auto entity = registry.CreateEntity();
    entity.AddComponent<SceneObjectComponent>(data.object);
    if (!data.tag.empty()) entity.Tag(data.tag);
    if (!data.group.empty()) entity.Group(data.group);
    Add(entity, data.transform); Add(entity, data.body); Add(entity, data.sprite); Add(entity, data.collider);
    Add(entity, data.animation); Add(entity, data.health); Add(entity, data.emitter); Add(entity, data.keyboard); Add(entity, data.text);
    Add(entity, data.primitive);
    if (data.cameraFollow) entity.AddComponent<CameraFollowComponent>();
    if (script) {
        ScriptComponent component(*script); component.enabled = data.scriptEnabled;
        entity.AddComponent<ScriptComponent>(component);
    }
    return entity;
}
void SceneEditor::Restore(const SceneData& input) {
    registry.Update();
    // 先完整验证文件、资源和脚本，再替换对象；错误文件不能先清掉当前场景。
    auto scene = SceneSerializer::Decode(SceneSerializer::Encode(input));
    if (scene.baseLevel != baseLevel) throw std::runtime_error("场景属于另一个基础关卡，请从对应关卡启动后再加载");
    std::vector<std::optional<sol::function>> scripts;
    for (const auto& entity : scene.entities) {
        if (entity.sprite && !assets.HasTexture(entity.sprite->assetId)) throw std::runtime_error("找不到精灵资源：" + entity.sprite->assetId);
        if (entity.text && !assets.HasFont(entity.text->assetId)) throw std::runtime_error("找不到字体资源：" + entity.text->assetId);
        scripts.push_back(ScriptFor(entity));
    }
    for (auto entity : registry.GetEntities()) if (!entity.BelongsToGroup("tiles")) entity.Kill();
    registry.Update();
    SimulationClock::Set(scene.simulationSeconds);
    for (std::size_t i = 0; i < scene.entities.size(); ++i) Spawn(scene.entities[i], scripts[i]);
    registry.Update();
    selected = Entity(); paused = true; stepRequested = false; dirty = false;
}
Entity SceneEditor::Create(ColliderShape shape, glm::vec2 position) {
    MarkEdited();
    SceneEntityData data;
    data.object.name = shape == ColliderShape::Box ? "新建矩形" : "新建圆形";
    data.group = "objects"; data.transform = TransformComponent(position);
    data.body = RigidBodyComponent();
    data.collider = shape == ColliderShape::Box ? ColliderComponent(40, 40) : ColliderComponent::Circle(20);
    data.primitive = PrimitiveRenderComponent();
    if (shape == ColliderShape::Circle) data.primitive->color = {235, 180, 75, 255};
    selected = Spawn(data, std::nullopt);
    return selected;
}
Entity SceneEditor::DuplicateSelected() {
    auto entity = Selected();
    if (!entity.IsValid()) return Entity();
    auto data = CaptureEntity(entity);
    auto script = ScriptFor(data);
    data.object.name += " 副本"; data.tag.clear(); data.cameraFollow = false;
    if (data.transform) data.transform->position += glm::vec2(24);
    if (data.text) data.text->position += glm::vec2(24);
    MarkEdited(); selected = Spawn(data, script); return selected;
}
void SceneEditor::DeleteSelected() {
    auto entity = Selected();
    if (entity.IsValid()) { MarkEdited(); entity.Kill(); selected = Entity(); }
}
void SceneEditor::RestoreOriginalScript() {
    auto entity = Selected(); if (!entity.IsValid()) return;
    auto data = CaptureEntity(entity); data.hasScript = true;
    const auto script = ScriptFor(data);
    entity.AddComponent<ScriptComponent>(*script); MarkEdited();
}
void SceneEditor::SetTagAndGroup(const std::string& tag, const std::string& group) {
    auto entity = Selected(); if (!entity.IsValid()) return;
    if (group == "tiles" || group == "projectiles") throw std::invalid_argument("不能使用地图或子弹的保留分组");
    const auto owner = registry.TryGetEntityByTag(tag);
    if (owner && *owner != entity) throw std::invalid_argument("此标签已被其他对象使用");
    if (tag.empty()) registry.RemoveEntityTag(entity); else entity.Tag(tag);
    if (group.empty()) registry.RemoveEntityGroup(entity); else entity.Group(group);
    MarkEdited();
}
void SceneEditor::MoveSelected(glm::vec2 position) {
    auto entity = Selected(); if (!entity.IsValid()) return;
    if (snap && grid > 0) position = glm::round(position / grid) * grid;
    if (auto* transform = entity.TryGetComponent<TransformComponent>()) { transform->position = position; MarkEdited(); }
    else if (auto* text = entity.TryGetComponent<TextLabelComponent>()) { text->position = position; MarkEdited(); }
}
void SceneEditor::FocusSelected(SDL_Rect& camera) const {
    auto entity = Selected(); if (!entity.IsValid()) return;
    if (auto* transform = entity.TryGetComponent<TransformComponent>()) {
        camera.x = static_cast<int>(transform->position.x) - camera.w / 2;
        camera.y = static_cast<int>(transform->position.y) - camera.h / 2;
    }
}
Entity SceneEditor::Pick(glm::vec2 screen, const SDL_Rect& camera) const {
    Entity result; int bestZ = (std::numeric_limits<int>::min)();
    for (auto entity : Entities()) {
        auto* transform = entity.TryGetComponent<TransformComponent>();
        if (!transform) continue;
        auto* sprite = entity.TryGetComponent<SpriteComponent>();
        const auto point = screen + (sprite && sprite->isFixed ? glm::vec2(0) : glm::vec2(camera.x, camera.y));
        Physics2D::WorldShape shape;
        if (sprite) shape = Physics2D::MakeShape(*transform, ColliderComponent(sprite->width, sprite->height));
        else if (entity.HasComponent<ColliderComponent>()) shape = Physics2D::ShapeOf(entity);
        else shape = Physics2D::MakeShape(*transform, ColliderComponent(16, 16));
        const auto delta = point - shape.center;
        const bool hit = shape.type == ColliderShape::Circle ? glm::dot(delta, delta) <= shape.radius * shape.radius
            : std::abs(glm::dot(delta, shape.xAxis)) <= shape.half.x && std::abs(glm::dot(delta, shape.yAxis)) <= shape.half.y;
        const int z = sprite ? sprite->zIndex : entity.HasComponent<PrimitiveRenderComponent>() ? 100001 : 0;
        if (hit && z >= bestZ) { result = entity; bestZ = z; }
    }
    return result;
}
bool SceneEditor::Save(const std::string& path) {
    try {
        registry.Update();
        paused = true;
        auto scene = Capture();
        for (const auto& data : scene.entities) ScriptFor(data);
        SceneSerializer::Save(std::filesystem::u8path(path), scene);
        dirty = false; status = "已保存：" + path; return true;
    } catch (const std::exception& error) { status = "保存失败：" + std::string(error.what()); return false; }
}
bool SceneEditor::Load(const std::string& path) {
    try { Restore(SceneSerializer::Load(std::filesystem::u8path(path))); status = "已加载：" + path; return true; }
    catch (const std::exception& error) { status = "加载失败，当前场景保留：" + std::string(error.what()); return false; }
}
bool SceneEditor::Reset() {
    try { Restore(initial); status = "已恢复启动时的场景"; return true; }
    catch (const std::exception& error) { status = "恢复失败：" + std::string(error.what()); return false; }
}
void SceneEditor::ApplyRequests() {
    const auto action = request; request = Request::None;
    if (action == Request::Save) Save(requestPath);
    if (action == Request::Load) Load(requestPath);
    if (action == Request::Reset) Reset();
}
