#include "ECS.h"

std::atomic<unsigned int> IComponent::nextId{0};
unsigned int IComponent::AllocateId() {
    auto id = nextId.load();
    while (id < MAX_COMPONENTS) {
        if (nextId.compare_exchange_weak(id, id + 1)) return id;
    }
    throw std::length_error("组件类型数量超过签名容量");
}

Entity::Entity(int id, std::uint64_t generation, Registry* owner,
               const std::shared_ptr<RegistryLifetime>& lifetime)
    : id(id), generation(generation), ownerIdentity(lifetime->identity), lifetime(lifetime), registry(owner) {}
Registry& Entity::Owner() const {
    const auto token = lifetime.lock();
    if (!token || !token->owner || token->owner != registry || !registry->IsValid(*this))
        throw std::invalid_argument("实体句柄无效或所属注册表已销毁");
    return *registry;
}
bool Entity::IsValid() const {
    const auto token = lifetime.lock();
    return token && token->owner && token->owner == registry && registry->IsValid(*this);
}
bool Entity::IsPendingRemoval() const { return IsValid() && registry->IsPendingRemoval(*this); }
void Entity::Kill() const { if (IsValid()) registry->KillEntity(*this); }
void Entity::Tag(const std::string& tag) const { Owner().TagEntity(*this, tag); }
bool Entity::HasTag(const std::string& tag) const { return IsValid() && registry->EntityHasTag(*this, tag); }
void Entity::Group(const std::string& group) const { Owner().GroupEntity(*this, group); }
bool Entity::BelongsToGroup(const std::string& group) const { return IsValid() && registry->EntityBelongsToGroup(*this, group); }

void System::AddEntityToSystem(Entity entity) {
    if (Contains(entity)) return;
    entities.push_back(entity);
    try { indices.emplace(entity, entities.size() - 1); }
    catch (...) { entities.pop_back(); throw; }
}
void System::RemoveEntityFromSystem(Entity entity) {
    const auto found = indices.find(entity);
    if (found == indices.end()) return;
    const auto index = found->second;
    if (index != entities.size() - 1) {
        entities[index] = entities.back();
        indices.at(entities[index]) = index;
    }
    entities.pop_back();
    indices.erase(found);
}
void System::ClearEntities() { entities.clear(); indices.clear(); }

Registry::Registry()
    : lifetime(std::make_shared<RegistryLifetime>(RegistryLifetime{this, nextIdentity.fetch_add(1)})) {}
Registry::~Registry() {
    // 即使外部仍保存保护区或实体句柄，也不能再访问已经销毁的注册表。
    lifetime->owner = nullptr;
}
Entity Registry::Handle(int id) const { return Entity(id, slots[id].generation, const_cast<Registry*>(this), lifetime); }
bool Registry::IsValid(Entity entity) const {
    return entity.registry == this && entity.ownerIdentity == lifetime->identity && entity.GetId() >= 0
        && static_cast<std::size_t>(entity.GetId()) < slots.size() && slots[entity.GetId()].alive
        && slots[entity.GetId()].generation == entity.GetGeneration();
}
void Registry::RequireAlive(Entity entity) const {
    if (!IsValid(entity)) throw std::invalid_argument("实体句柄已失效或属于另一个注册表");
}
void Registry::RequireWritable(Entity entity) const {
    RequireAlive(entity);
    if (IsPendingRemoval(entity)) throw std::logic_error("不能修改待销毁实体的结构");
}
std::size_t Registry::GetEntityCount() const {
    return static_cast<std::size_t>(std::count_if(slots.begin(), slots.end(), [](const Slot& slot) { return slot.alive; }));
}
Entity Registry::CreateEntity() {
    int id;
    if (freeIds.empty()) {
        if (slots.size() >= static_cast<std::size_t>((std::numeric_limits<int>::max)()))
            throw std::length_error("实体 ID 空间已耗尽");
        id = static_cast<int>(slots.size());
        slots.emplace_back();
    } else { id = freeIds.front(); freeIds.pop_front(); }
    auto& slot = slots[id];
    slot.alive = true; slot.committed = false; slot.signature.reset();
    const auto entity = Handle(id);
    dirtyEntities.insert(entity);
    return entity;
}
void Registry::KillEntity(Entity entity) {
    if (IsValid(entity)) entitiesToBeKilled.insert(entity);
}
void Registry::AddEntityToSystems(Entity entity) {
    if (deferDepth) throw std::logic_error("不能在遍历期间修改系统成员");
    RequireAlive(entity);
    const auto& signature = slots[entity.GetId()].signature;
    for (auto& item : systems) {
        const auto& required = item.second->GetComponentSignature();
        if (!IsPendingRemoval(entity) && (signature & required) == required) item.second->AddEntityToSystem(entity);
        else item.second->RemoveEntityFromSystem(entity);
    }
}
void Registry::RemoveEntityFromSystems(Entity entity) {
    if (deferDepth) throw std::logic_error("不能在遍历期间修改系统成员");
    for (auto& item : systems) item.second->RemoveEntityFromSystem(entity);
}
void Registry::DestroyEntity(Entity entity) {
    if (!IsValid(entity)) return;
    RemoveEntityFromSystems(entity);
    // 销毁提交不再经过公开的排队接口，直接清理所有索引。
    const int id = entity.GetId();
    const auto tag = tagPerEntity.find(id);
    if (tag != tagPerEntity.end()) { entityPerTag.erase(tag->second); tagPerEntity.erase(tag); }
    const auto group = groupPerEntity.find(id);
    if (group != groupPerEntity.end()) {
        auto& members = entitiesPerGroup.at(group->second);
        members.erase(entity);
        if (members.empty()) entitiesPerGroup.erase(group->second);
        groupPerEntity.erase(group);
    }
    for (auto& pool : componentPools) if (pool) pool->RemoveEntityFromPool(id);
    auto& slot = slots[id];
    slot.signature.reset(); slot.alive = false; slot.committed = false;
    dirtyEntities.erase(entity);
    // 版本号达到上限后永久停用该槽位，避免回绕使旧句柄重新有效。
    if (slot.generation != (std::numeric_limits<std::uint64_t>::max)()) {
        ++slot.generation;
        freeIds.push_back(id);
    }
}
void Registry::Update() {
    if (deferDepth || updating) throw std::logic_error("不能在遍历期间或递归调用时提交结构变更");
    updating = true;
    struct ResetFlag { bool& flag; ~ResetFlag() { flag = false; } } reset{updating};
    // 销毁优先；指向已销毁版本的排队命令会自动跳过。
    while (!entitiesToBeKilled.empty()) {
        const auto entity = *entitiesToBeKilled.begin();
        DestroyEntity(entity);
        entitiesToBeKilled.erase(entity);
    }
    while (!commands.empty()) {
        auto command = std::move(commands.front());
        commands.pop_front();
        command();
    }
    while (!dirtyEntities.empty()) {
        const auto entity = *dirtyEntities.begin();
        if (IsValid(entity)) {
            AddEntityToSystems(entity);
            slots[entity.GetId()].committed = true;
        }
        dirtyEntities.erase(entity);
    }
}
void Registry::Clear() {
    if (deferDepth || updating) throw std::logic_error("不能在遍历或提交期间清空注册表");
    commands.clear();
    for (std::size_t id = 0; id < slots.size(); ++id) if (slots[id].alive) DestroyEntity(Handle(static_cast<int>(id)));
    for (auto& pool : componentPools) if (pool) pool->Clear();
    for (auto& item : systems) item.second->ClearEntities();
    dirtyEntities.clear(); entitiesToBeKilled.clear();
    entityPerTag.clear(); tagPerEntity.clear(); entitiesPerGroup.clear(); groupPerEntity.clear();
    freeIds.clear();
    for (std::size_t id = 0; id < slots.size(); ++id)
        if (slots[id].generation != (std::numeric_limits<std::uint64_t>::max)()) freeIds.push_back(static_cast<int>(id));
}

void Registry::TagEntity(Entity entity, const std::string& tag) {
    RequireWritable(entity);
    if (tag.empty()) throw std::invalid_argument("标签不能为空");
    if (deferDepth) {
        commands.emplace_back([this, entity, tag] { if (IsValid(entity) && !IsPendingRemoval(entity)) TagEntity(entity, tag); });
        return;
    }
    const auto existing = entityPerTag.find(tag);
    if (existing != entityPerTag.end() && existing->second != entity) throw std::invalid_argument("标签已被其他实体占用");
    const auto previous = tagPerEntity.find(entity.GetId());
    if (previous != tagPerEntity.end() && previous->second == tag) return;
    if (previous != tagPerEntity.end()) entityPerTag.erase(previous->second);
    entityPerTag.insert_or_assign(tag, entity);
    tagPerEntity.insert_or_assign(entity.GetId(), tag);
}
bool Registry::EntityHasTag(Entity entity, const std::string& tag) const {
    if (!IsValid(entity)) return false;
    const auto found = tagPerEntity.find(entity.GetId());
    return found != tagPerEntity.end() && found->second == tag;
}
std::optional<Entity> Registry::TryGetEntityByTag(const std::string& tag) const {
    const auto found = entityPerTag.find(tag);
    return found != entityPerTag.end() && IsValid(found->second) ? std::optional<Entity>(found->second) : std::nullopt;
}
Entity Registry::GetEntityByTag(const std::string& tag) const {
    const auto entity = TryGetEntityByTag(tag);
    if (!entity) throw std::out_of_range("不存在指定标签的实体");
    return *entity;
}
void Registry::RemoveEntityTag(Entity entity) {
    RequireWritable(entity);
    if (deferDepth) {
        commands.emplace_back([this, entity] { if (IsValid(entity) && !IsPendingRemoval(entity)) RemoveEntityTag(entity); });
        return;
    }
    const auto found = tagPerEntity.find(entity.GetId());
    if (found != tagPerEntity.end()) { entityPerTag.erase(found->second); tagPerEntity.erase(found); }
}
void Registry::GroupEntity(Entity entity, const std::string& group) {
    RequireWritable(entity);
    if (group.empty()) throw std::invalid_argument("分组不能为空");
    if (deferDepth) {
        commands.emplace_back([this, entity, group] { if (IsValid(entity) && !IsPendingRemoval(entity)) GroupEntity(entity, group); });
        return;
    }
    const auto previous = groupPerEntity.find(entity.GetId());
    if (previous != groupPerEntity.end() && previous->second == group) return;
    RemoveEntityGroup(entity);
    entitiesPerGroup[group].insert(entity);
    groupPerEntity.insert_or_assign(entity.GetId(), group);
}
bool Registry::EntityBelongsToGroup(Entity entity, const std::string& group) const {
    if (!IsValid(entity)) return false;
    const auto found = groupPerEntity.find(entity.GetId());
    return found != groupPerEntity.end() && found->second == group;
}
std::vector<Entity> Registry::GetEntitiesByGroup(const std::string& group) const {
    const auto found = entitiesPerGroup.find(group);
    if (found == entitiesPerGroup.end()) return {};
    return {found->second.begin(), found->second.end()};
}
void Registry::RemoveEntityGroup(Entity entity) {
    RequireWritable(entity);
    if (deferDepth) {
        commands.emplace_back([this, entity] { if (IsValid(entity) && !IsPendingRemoval(entity)) RemoveEntityGroup(entity); });
        return;
    }
    const auto found = groupPerEntity.find(entity.GetId());
    if (found == groupPerEntity.end()) return;
    auto& members = entitiesPerGroup.at(found->second);
    members.erase(entity);
    if (members.empty()) entitiesPerGroup.erase(found->second);
    groupPerEntity.erase(found);
}

std::vector<Entity> Registry::GetEntities() const {
    std::vector<Entity> result;
    for (std::size_t id = 0; id < slots.size(); ++id)
        if (slots[id].alive && !IsPendingRemoval(Handle(static_cast<int>(id)))) result.push_back(Handle(static_cast<int>(id)));
    return result;
}
std::string Registry::GetEntityTag(Entity entity) const {
    if (!IsValid(entity)) return {};
    const auto found = tagPerEntity.find(entity.GetId());
    return found == tagPerEntity.end() ? std::string{} : found->second;
}
std::string Registry::GetEntityGroup(Entity entity) const {
    if (!IsValid(entity)) return {};
    const auto found = groupPerEntity.find(entity.GetId());
    return found == groupPerEntity.end() ? std::string{} : found->second;
}
