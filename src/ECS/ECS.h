#pragma once
#ifndef ECS_H
#define ECS_H
#include<bitset>
#include<vector>
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
	std::vector<IPool*>componentPools;

	// 标明每个实体使用哪些组件，实体id作为索引
	std::vector<Signature> entityComponentSignatures;

	std::unordered_map<std::type_index, System*>systems;

	// 帧结束前统一创建销毁实体
	std::set<Entity>entitiesToBeAdd;
	std::set<Entity>entitiesToBeKilled;

public:
	Registry() = default;

	void Update();

	// 实体
	Entity CreateEntitty();

	// 组件
	template <typename TComponent, typename ...TArgs> void AddComponent(Entity entity, TArgs&& ...args);
	template <typename TComponent> void RemoveComponent(Entity entity);
	template <typename TComponent> bool HasComponent(Entity entity)const;

	// 系统
	template <typename TSystem, typename ...TArgs> void AddSystem(TArgs&& ...args);
	template <typename TSystem> void RemoveSystem();
	template <typename TSystem> bool HasSystem()const;
	template <typename TSystem> TSystem& GetSystem()const;

};

// 设置系统需要的组件
template <typename TComponent>
void System::RequireComponent() {
	const auto componentId = Component<TComponent>::GetId();
	componentSignature.set(componentId);
}

template <typename TComponent, typename ...TArgs>
void Registry::AddComponent(Entity entity, TArgs&&...args) {
	const auto componentId = Component<TComponent>::GetId();
	const auto entityId = entity.GetId();

	if (componentId >= componentPools.size()) {
		componentPools.resize(componentId + 1, nullptr);
	}

	if (!componentPools[componentId]) {
		Pool <TComponent>* newComponentPool = new Pool<TComponent>();
		componentPools[componentId] = newComponentPool;
	}

	Pool<TComponent>* componentPool = componentPools[componentId];

	if (entityId >= componentPool->GetSize()) {
		componentPool->Resize(numEntities);
	}

	TComponent newComponent(std::forward<TArgs>(args)...);

	componentPool->Set(entityId, newComponent);

	// 实体的组件签名对应的组件设为1
	entityComponentSignatures[entityId].set(componentId);

}

template <typename TComponent>
void Registry::RemoveComponent(Entity entity) {
	const auto componentId = Component<TComponent>::GetId();
	const auto entityId = entity.GetId();

	entityComponentSignatures[entityId].set(componentId, false);
}

template <typename TComponent>
bool Registry::HasComponent(Entity entity)const {
	const auto componentId = Component<TComponent>::GetId();
	const auto entityId = entity.GetId();

	return entityComponentSignatures[entityId].test(componentId);
}


#endif // !ECS_H
