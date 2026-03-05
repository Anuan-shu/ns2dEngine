#pragma once
#ifndef ECS_H
#define ECS_H
#include"../Logger/Logger.h"
#include<bitset>
#include<memory>
#include<vector>
#include<deque>
#include<unordered_map>
#include<typeindex>
#include<set>
const unsigned int MAX_COMPONENTS = 32;
// 用bitset表示组件签名，
typedef std::bitset<MAX_COMPONENTS> Signature;

struct IComponent {
protected:
	static int nextId;
};
template <typename T>
class  Component :public IComponent {
public:
	static int GetId() {
		static auto id = nextId++;
		return id;
	}
};

class Entity {
private:
	int id;
public:
	Entity(int id) :id(id) {};
	Entity(const Entity& entity) = default;
	int GetId() const;
	void Kill();

	// 标签和分组
	void Tag(const std::string& tag);
	bool HasTag(const std::string& tag)const;
	void Group(const std::string& group);
	bool BelongsToGroup(const std::string& group)const;

	Entity& operator =(const Entity& other) = default;
	bool operator ==(const Entity& other)const {
		return id == other.id;
	}
	bool operator !=(const Entity& other)const {
		return id != other.id;
	}
	bool operator >(const Entity& other)const {
		return id > other.id;
	}
	bool operator <(const Entity& other)const {
		return id < other.id;
	}

	template<typename TComponent, typename ...TArgs>void AddComponent(TArgs&& ...args);
	template<typename TComponent> void RemoveComponent();
	template<typename TComponent> bool HasComponent()const;
	template<typename TComponent> TComponent& GetComponent()const;

	// 实体所属注册表
	class Registry* registry;

};

class System {
private:
	Signature componentSignature;
	std::vector<Entity> entities;

public:
	System() = default;
	~System() = default;

	void AddEntityToSystem(Entity entity);
	void RemoveEntityFromSystem(Entity entity);
	std::vector<Entity> GetSystemEntities() const;
	const Signature& GetComponentSignature()const;

	// 实体必须具备的组件类型，才能被系统识别
	template <typename TComponent> void RequireComponent();
};

class IPool {
public:
	virtual ~IPool() {}
};

// 组件池
template <typename T>
class Pool :public IPool {
private:
	std::vector<T> data;
public:
	Pool(int size = 100) {
		data.resize(size);
	}
	~Pool() = default;
	bool isEmpty()const {
		return data.empty();
	}
	int GetSize()const {
		return data.size();
	}
	void Resize(int n) {
		data.resize(n);
	}
	void Clear() {
		data.clear();
	}
	void Add(T object) {
		data.push_back(object);
	}
	void Set(int index, T obj) {
		data[index] = obj;
	}
	T& Get(int index) {
		return static_cast<T&>(data[index]);
	}
	T& operator [](unsigned int index) {
		return data[index];
	}
};

// 注册表 负责实体创建销毁，系统添加管理
class Registry {
private:
	int numEntities = 0;

	// 组件池数组 存储所有组件池
	// componentPools[i]表示组件i的组件池
	// 每个组件池中pool[i]表示实体i的组件
	std::vector<std::shared_ptr<IPool>>componentPools;

	// 标明每个实体使用哪些组件，实体id作为索引
	std::vector<Signature> entityComponentSignatures;

	std::unordered_map<std::type_index, std::shared_ptr<System>>systems;

	// 帧结束前统一创建销毁实体
	std::set<Entity>entitiesToBeAdded;
	std::set<Entity>entitiesToBeKilled;

	// 实体标签
	std::unordered_map<std::string, Entity>entityPerTag;
	std::unordered_map<int, std::string>tagPerEntity;

	// 实体分组
	std::unordered_map<std::string, std::set<Entity>>entitiesPerGroup;
	std::unordered_map<int, std::string>groupPerEntity;

	// 已销毁idf复用
	std::deque<int> freeIds;

public:
	Registry() {
		Logger::Log("Registry构造");
	}
	~Registry() {
		Logger::Log("Registry析构");
	}

	void Update();

	// 实体
	Entity CreateEntity();
	void KillEntity(Entity entity);

	// 标签
	void TagEntity(Entity entity, const std::string& tag);
	bool EntityHasTag(Entity entity, const std::string& tag)const;
	Entity GetEntityByTag(const std::string& tag)const;
	void RemoveEntityTag(Entity entity);

	// 分组
	void GroupEntity(Entity entity, const std::string& group);
	bool EntityBelongsToGroup(Entity entity, const std::string& group)const;
	std::vector<Entity>GetEntitiesByGroup(const std::string& group)const;
	void RemoveEntityGroup(Entity entity);

	// 组件
	template <typename TComponent, typename ...TArgs> void AddComponent(Entity entity, TArgs&& ...args);
	template <typename TComponent> void RemoveComponent(Entity entity);
	template <typename TComponent> bool HasComponent(Entity entity)const;
	template<typename TComponent> TComponent& GetComponent(Entity entity)const;

	// 系统
	template <typename TSystem, typename ...TArgs> void AddSystem(TArgs&& ...args);
	template <typename TSystem> void RemoveSystem();
	template <typename TSystem> bool HasSystem()const;
	template <typename TSystem> TSystem& GetSystem()const;

	// 检查实体组件签名，加入/删除符合系统要求的系统中
	void AddEntityToSystems(Entity entity);
	void RemoveEntityFromSystems(Entity entity);

};

// 设置系统需要的组件
template <typename TComponent>
void System::RequireComponent() {
	const auto componentId = Component<TComponent>::GetId();
	componentSignature.set(componentId);
}

template <typename TSystem, typename ...TArgs>
void Registry::AddSystem(TArgs&& ...args) {
	// TSystem* newSystem(new TSystem(std::forward<TArgs>(args)...));
	std::shared_ptr<TSystem> newSystem = std::make_shared<TSystem>(std::forward<TArgs>(args)...);
	systems.insert(std::make_pair(std::type_index(typeid(TSystem)), newSystem));
}

template <typename TSystem>
void Registry::RemoveSystem() {
	auto system = systems.find(std::type_index(typeid(TSystem)));
	systems.erase(system);
}

template <typename TSystem>
bool Registry::HasSystem()const {
	return systems.find(std::type_index(typeid(TSystem))) != systems.end();
}

template <typename TSystem>
TSystem& Registry::GetSystem()const {
	auto system = systems.find(std::type_index(typeid(TSystem)));

	return *(std::static_pointer_cast<TSystem>(system->second));
}


template <typename TComponent, typename ...TArgs>
void Registry::AddComponent(Entity entity, TArgs&&...args) {
	const auto componentId = Component<TComponent>::GetId();
	const auto entityId = entity.GetId();

	if (componentId >= componentPools.size()) {
		componentPools.resize(componentId + 1, nullptr);
	}

	if (!componentPools[componentId]) {
		//Pool <TComponent>* newComponentPool = new Pool<TComponent>();
		std::shared_ptr<Pool<TComponent>>newComponentPool = std::make_shared<Pool<TComponent>>();
		componentPools[componentId] = newComponentPool;
	}

	// Pool<TComponent>* componentPool = componentPools[componentId];
	std::shared_ptr<Pool<TComponent>> componentPool = std::static_pointer_cast<Pool<TComponent>> (componentPools[componentId]);

	if (entityId >= componentPool->GetSize()) {
		componentPool->Resize(numEntities);
	}

	TComponent newComponent(std::forward<TArgs>(args)...);

	componentPool->Set(entityId, newComponent);

	// 实体的组件签名对应的组件设为1
	entityComponentSignatures[entityId].set(componentId);

	Logger::Log("ComponentId = " + std::to_string(componentId) + "添加到entityId = " + std::to_string(entityId));
}

template <typename TComponent>
void Registry::RemoveComponent(Entity entity) {
	const auto componentId = Component<TComponent>::GetId();
	const auto entityId = entity.GetId();

	entityComponentSignatures[entityId].set(componentId, false);

	Logger::Log("ComponentId = " + std::to_string(componentId) + "从entityId = " + std::to_string(entityId) + " 中移除");

}

template <typename TComponent>
bool Registry::HasComponent(Entity entity)const {
	const auto componentId = Component<TComponent>::GetId();
	const auto entityId = entity.GetId();

	return entityComponentSignatures[entityId].test(componentId);
}
template<typename TComponent>
TComponent& Registry::GetComponent(Entity entity)const {
	const auto componentId = Component<TComponent>::GetId();
	const auto entityId = entity.GetId();
	auto componentPool = std::static_pointer_cast<Pool<TComponent>>(componentPools[componentId]);
	return componentPool->Get(entityId);
}

//////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////
// 实体类管理组件
template <typename TComponent, typename ...TArgs>
void Entity::AddComponent(TArgs&&...args) {
	registry->AddComponent<TComponent>(*this, std::forward<TArgs>(args)...);
}

template <typename TComponent>
void Entity::RemoveComponent() {
	registry->RemoveComponent<TComponent>(*this);
}

template<typename TComponent>
bool Entity::HasComponent()const {
	return registry->HasComponent<TComponent>(*this);
}

template<typename TComponent>
TComponent& Entity::GetComponent()const {
	return registry->GetComponent<TComponent>(*this);
}
#endif // !ECS_H
