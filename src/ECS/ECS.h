#pragma once
#include "../Logger/Logger.h"
#include <algorithm>
#include <atomic>
#include <bitset>
#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <tuple>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

constexpr unsigned int MAX_COMPONENTS = 32;
using Signature = std::bitset<MAX_COMPONENTS>;

struct IComponent {
protected:
    static std::atomic<unsigned int> nextId;
    static unsigned int AllocateId();
};
template <typename T> class Component : public IComponent {
public:
    static unsigned int GetId() {
        static const unsigned int id = AllocateId();
        return id;
    }
};

class Registry;
// 生命周期标记只负责判断注册表是否仍存在，不延长注册表本身的寿命。
struct RegistryLifetime { Registry* owner; std::uint64_t identity; };

class Entity {
    int id = -1;
    std::uint64_t generation = 0;
    std::uint64_t ownerIdentity = 0;
    std::weak_ptr<RegistryLifetime> lifetime;
    Entity(int id, std::uint64_t generation, Registry* owner,
           const std::shared_ptr<RegistryLifetime>& lifetime);
    Registry& Owner() const;
    friend class Registry;
public:
    // 直接构造的实体没有所属注册表，只能作为无效句柄。
    explicit Entity(int id = -1) : id(id) {}
    Registry* registry = nullptr; // 保留旧调用方式；普通业务代码优先使用实体成员函数。
    int GetId() const { return id; }
    std::uint64_t GetGeneration() const { return generation; }
    bool IsValid() const;
    bool IsPendingRemoval() const;
    void Kill() const;
    void Tag(const std::string& tag) const;
    bool HasTag(const std::string& tag) const;
    void Group(const std::string& group) const;
    bool BelongsToGroup(const std::string& group) const;
    bool operator==(const Entity& other) const {
        return std::tie(ownerIdentity, id, generation) == std::tie(other.ownerIdentity, other.id, other.generation);
    }
    bool operator!=(const Entity& other) const { return !(*this == other); }
    bool operator<(const Entity& other) const {
        return std::tie(ownerIdentity, id, generation) < std::tie(other.ownerIdentity, other.id, other.generation);
    }
    bool operator>(const Entity& other) const { return other < *this; }
    std::size_t Hash() const {
        auto h = std::hash<std::uint64_t>{}(ownerIdentity);
        h ^= std::hash<int>{}(id) + 0x9e3779b9u + (h << 6) + (h >> 2);
        return h ^ (std::hash<std::uint64_t>{}(generation) + 0x9e3779b9u + (h << 6) + (h >> 2));
    }
    template<typename T, typename... Args> void AddComponent(Args&&... args) const;
    template<typename T> void RemoveComponent() const;
    template<typename T> bool HasComponent() const;
    template<typename T> T& GetComponent() const;
    template<typename T> T* TryGetComponent() const;
};
struct EntityHash { std::size_t operator()(const Entity& entity) const { return entity.Hash(); } };

class System {
    Signature componentSignature;
    std::vector<Entity> entities;
    std::unordered_map<Entity, std::size_t, EntityHash> indices;
    bool signatureFrozen = false;
    friend class Registry;
    void FreezeSignature() { signatureFrozen = true; }
    void AddEntityToSystem(Entity entity);
    void RemoveEntityFromSystem(Entity entity);
    void ClearEntities();
public:
    virtual ~System() = default;
    bool Contains(Entity entity) const { return indices.find(entity) != indices.end(); }
    // 视图仅在下一次 Registry::Update/Clear 前有效；遍历时使用 Defer 保护结构变更。
    const std::vector<Entity>& GetSystemEntities() const { return entities; }
    const Signature& GetComponentSignature() const { return componentSignature; }
    template<typename T> void RequireComponent() {
        if (signatureFrozen) throw std::logic_error("系统注册后不能修改所需的组件签名");
        componentSignature.set(Component<T>::GetId());
    }
};

class IPool {
public:
    virtual ~IPool() = default;
    virtual void RemoveEntityFromPool(int entityId) = 0;
    virtual void Clear() = 0;
};

// 稀疏索引定位实体，稠密数组只存活组件；删除时用末尾元素补位。
// 新增、删除和清空可能使组件引用失效，不应跨结构变更提交点保存引用。
template<typename T> class Pool : public IPool {
    static constexpr std::size_t Missing = (std::numeric_limits<std::size_t>::max)();
    std::vector<T> data;
    std::vector<int> denseEntities;
    std::vector<std::size_t> sparse;
public:
    explicit Pool(std::size_t capacity = 100) { Reserve(capacity); }
    bool IsEmpty() const { return data.empty(); }
    std::size_t GetSize() const { return data.size(); }
    void Reserve(std::size_t capacity) { data.reserve(capacity); denseEntities.reserve(capacity); }
    bool Contains(int id) const {
        return id >= 0 && static_cast<std::size_t>(id) < sparse.size() && sparse[id] != Missing;
    }
    void Set(int id, T object) {
        if (id < 0) throw std::invalid_argument("组件池不接受负数实体 ID");
        if (Contains(id)) { data[sparse[id]] = std::move(object); return; }
        if (static_cast<std::size_t>(id) >= sparse.size()) sparse.resize(static_cast<std::size_t>(id) + 1, Missing);
        data.push_back(std::move(object));
        try { denseEntities.push_back(id); }
        catch (...) { data.pop_back(); throw; }
        sparse[id] = data.size() - 1;
    }
    bool Remove(int id) {
        if (!Contains(id)) return false;
        const auto index = sparse[id], last = data.size() - 1;
        if (index != last) {
            data[index] = std::move(data[last]);
            const int movedEntity = denseEntities[last];
            denseEntities[index] = movedEntity;
            sparse[movedEntity] = index;
        }
        data.pop_back(); denseEntities.pop_back(); sparse[id] = Missing;
        return true;
    }
    void RemoveEntityFromPool(int id) override { Remove(id); }
    void Clear() override { data.clear(); denseEntities.clear(); sparse.clear(); }
    T* TryGet(int id) { return Contains(id) ? &data[sparse[id]] : nullptr; }
    const T* TryGet(int id) const { return Contains(id) ? &data[sparse[id]] : nullptr; }
    T& Get(int id) {
        auto* value = TryGet(id);
        if (!value) throw std::out_of_range("实体不存在该组件");
        return *value;
    }
    const T& Get(int id) const {
        auto* value = TryGet(id);
        if (!value) throw std::out_of_range("实体不存在该组件");
        return *value;
    }
    T& operator[](std::size_t index) { return data.at(index); }
    const T& operator[](std::size_t index) const { return data.at(index); }
};

class Registry {
    struct Slot {
        std::uint64_t generation = 0;
        bool alive = false;
        bool committed = false;
        Signature signature;
    };
    inline static std::atomic<std::uint64_t> nextIdentity{1};
    std::shared_ptr<RegistryLifetime> lifetime;
    std::vector<Slot> slots;
    std::vector<std::unique_ptr<IPool>> componentPools;
    std::unordered_map<std::type_index, std::shared_ptr<System>> systems;
    std::set<Entity> dirtyEntities;
    std::set<Entity> entitiesToBeKilled;
    std::deque<int> freeIds;
    std::unordered_map<std::string, Entity> entityPerTag;
    std::unordered_map<int, std::string> tagPerEntity;
    std::unordered_map<std::string, std::set<Entity>> entitiesPerGroup;
    std::unordered_map<int, std::string> groupPerEntity;
    std::deque<std::function<void()>> commands;
    unsigned int deferDepth = 0;
    bool updating = false;
    void RequireAlive(Entity entity) const;
    void RequireWritable(Entity entity) const;
    Entity Handle(int id) const;
    void DestroyEntity(Entity entity);
    template<typename T> void SetComponentNow(Entity entity, T value);
    template<typename T> void RemoveComponentNow(Entity entity);
public:
    // 嵌套保护区内只排队结构变更，析构不自动提交，避免抛异常和隐式迭代失效。
    class DeferredChanges {
        std::shared_ptr<RegistryLifetime> lifetime;
        friend class Registry;
        explicit DeferredChanges(Registry& registry) : lifetime(registry.lifetime) { ++registry.deferDepth; }
    public:
        DeferredChanges(const DeferredChanges&) = delete;
        DeferredChanges& operator=(const DeferredChanges&) = delete;
        DeferredChanges(DeferredChanges&& other) noexcept : lifetime(std::move(other.lifetime)) {}
        ~DeferredChanges() { if (lifetime && lifetime->owner) --lifetime->owner->deferDepth; }
    };
    Registry();
    ~Registry();
    Registry(const Registry&) = delete;
    Registry& operator=(const Registry&) = delete;
    Registry(Registry&&) = delete;
    Registry& operator=(Registry&&) = delete;
    DeferredChanges Defer() { return DeferredChanges(*this); }
    template<typename TSystem, typename F> void Each(F&& callback) {
        auto& system = GetSystem<TSystem>();
        auto deferred = Defer();
        for (auto entity : system.GetSystemEntities())
            if (IsValid(entity) && !IsPendingRemoval(entity)) callback(entity);
    }
    void Update();
    void Clear(); // 保留已注册系统和组件类型；旧实体句柄全部失效。
    Entity CreateEntity();
    bool IsValid(Entity entity) const;
    void KillEntity(Entity entity); // 失效句柄或重复销毁安全地忽略。
    bool IsPendingRemoval(Entity entity) const { return IsValid(entity) && entitiesToBeKilled.count(entity) != 0; }
    std::size_t GetEntityCount() const;
    std::vector<Entity> GetEntities() const;
    std::string GetEntityTag(Entity entity) const;
    std::string GetEntityGroup(Entity entity) const;
    void TagEntity(Entity entity, const std::string& tag);
    bool EntityHasTag(Entity entity, const std::string& tag) const;
    Entity GetEntityByTag(const std::string& tag) const;
    std::optional<Entity> TryGetEntityByTag(const std::string& tag) const;
    void RemoveEntityTag(Entity entity);
    void GroupEntity(Entity entity, const std::string& group);
    bool EntityBelongsToGroup(Entity entity, const std::string& group) const;
    std::vector<Entity> GetEntitiesByGroup(const std::string& group) const;
    void RemoveEntityGroup(Entity entity);
    template<typename T, typename... Args> void AddComponent(Entity entity, Args&&... args);
    template<typename T> void RemoveComponent(Entity entity);
    template<typename T> bool HasComponent(Entity entity) const;
    template<typename T> T& GetComponent(Entity entity);
    template<typename T> const T& GetComponent(Entity entity) const;
    template<typename T> T* TryGetComponent(Entity entity);
    template<typename T> const T* TryGetComponent(Entity entity) const;
    template<typename T, typename... Args> void AddSystem(Args&&... args);
    template<typename T> void RemoveSystem();
    template<typename T> bool HasSystem() const { return systems.find(std::type_index(typeid(T))) != systems.end(); }
    template<typename T> T& GetSystem() const;
    void AddEntityToSystems(Entity entity); // 重新匹配，不重复加入。
    void RemoveEntityFromSystems(Entity entity);
};

template<typename T> void Registry::SetComponentNow(Entity entity, T value) {
    dirtyEntities.insert(entity);
    const auto componentId = Component<T>::GetId();
    if (componentId >= componentPools.size()) componentPools.resize(componentId + 1);
    if (!componentPools[componentId]) componentPools[componentId] = std::make_unique<Pool<T>>();
    auto& pool = static_cast<Pool<T>&>(*componentPools[componentId]);
    pool.Set(entity.GetId(), std::move(value));
    slots[entity.GetId()].signature.set(componentId);
}
template<typename T, typename... Args> void Registry::AddComponent(Entity entity, Args&&... args) {
    RequireWritable(entity);
    Component<T>::GetId(); // 排队前先检查组件类型数量上限。
    if (deferDepth) {
        auto value = std::make_shared<T>(std::forward<Args>(args)...);
        commands.emplace_back([this, entity, value] {
            if (IsValid(entity) && !IsPendingRemoval(entity)) SetComponentNow(entity, std::move(*value));
        });
    } else SetComponentNow(entity, T(std::forward<Args>(args)...));
}
template<typename T> void Registry::RemoveComponentNow(Entity entity) {
    const auto id = Component<T>::GetId();
    if (!slots[entity.GetId()].signature.test(id)) return;
    dirtyEntities.insert(entity);
    static_cast<Pool<T>&>(*componentPools[id]).Remove(entity.GetId());
    slots[entity.GetId()].signature.reset(id);
}
template<typename T> void Registry::RemoveComponent(Entity entity) {
    RequireWritable(entity);
    Component<T>::GetId();
    if (deferDepth) {
        commands.emplace_back([this, entity] {
            if (IsValid(entity) && !IsPendingRemoval(entity)) RemoveComponentNow<T>(entity);
        });
    } else RemoveComponentNow<T>(entity);
}
template<typename T> bool Registry::HasComponent(Entity entity) const {
    if (!IsValid(entity)) return false;
    return slots[entity.GetId()].signature.test(Component<T>::GetId());
}
template<typename T> const T* Registry::TryGetComponent(Entity entity) const {
    if (!HasComponent<T>(entity)) return nullptr;
    return static_cast<const Pool<T>&>(*componentPools[Component<T>::GetId()]).TryGet(entity.GetId());
}
template<typename T> T* Registry::TryGetComponent(Entity entity) {
    return const_cast<T*>(std::as_const(*this).TryGetComponent<T>(entity));
}
template<typename T> const T& Registry::GetComponent(Entity entity) const {
    RequireAlive(entity);
    auto* value = TryGetComponent<T>(entity);
    if (!value) throw std::out_of_range("实体不存在请求的组件");
    return *value;
}
template<typename T> T& Registry::GetComponent(Entity entity) {
    return const_cast<T&>(std::as_const(*this).GetComponent<T>(entity));
}
template<typename T, typename... Args> void Registry::AddSystem(Args&&... args) {
    if (deferDepth || updating) throw std::logic_error("不能在遍历或提交期间注册系统");
    const auto key = std::type_index(typeid(T));
    if (systems.find(key) != systems.end()) throw std::logic_error("系统已经注册");
    auto system = std::make_shared<T>(std::forward<Args>(args)...);
    system->FreezeSignature();
    systems.emplace(key, system);
    // 后注册的系统也必须匹配已有实体；未提交的新实体仍等到 Update 才加入。
    for (std::size_t i = 0; i < slots.size(); ++i) {
        if (slots[i].alive && slots[i].committed && !IsPendingRemoval(Handle(static_cast<int>(i)))) {
            const auto& required = system->GetComponentSignature();
            if ((slots[i].signature & required) == required) system->AddEntityToSystem(Handle(static_cast<int>(i)));
        }
    }
}
template<typename T> void Registry::RemoveSystem() {
    if (deferDepth || updating) throw std::logic_error("不能在遍历或提交期间移除系统");
    systems.erase(std::type_index(typeid(T)));
}
template<typename T> T& Registry::GetSystem() const {
    auto found = systems.find(std::type_index(typeid(T)));
    if (found == systems.end()) throw std::out_of_range("请求的系统尚未注册");
    return static_cast<T&>(*found->second);
}
template<typename T, typename... Args> void Entity::AddComponent(Args&&... args) const { Owner().AddComponent<T>(*this, std::forward<Args>(args)...); }
template<typename T> void Entity::RemoveComponent() const { Owner().RemoveComponent<T>(*this); }
template<typename T> bool Entity::HasComponent() const { return IsValid() && registry->HasComponent<T>(*this); }
template<typename T> T& Entity::GetComponent() const { return Owner().GetComponent<T>(*this); }
template<typename T> T* Entity::TryGetComponent() const { return IsValid() ? registry->TryGetComponent<T>(*this) : nullptr; }
