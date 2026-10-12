#pragma once
#ifndef EVENTBUS_H
#define EVENTBUS_H

#include<map>
#include"../Logger/Logger.h"
#include"./Event.h"
#include<typeindex>
#include<memory>
#include<list>
#include<functional>



class IEventCallBack {
private:
	virtual void Call(Event& e) = 0;
public:
	virtual ~IEventCallBack() = default;

	void Execute(Event& e) {
		Call(e);
	}
};

template <typename TOwner, typename TEvent>
class EventCallBack :public IEventCallBack {
private:

	typedef void (TOwner::* CallbackFunction)(TEvent&);
	TOwner* ownerInstance;
	CallbackFunction callbackFunction;

	virtual void Call(Event& e)override {
		std::invoke(callbackFunction, ownerInstance, static_cast<TEvent&>(e));
	}

public:
	EventCallBack(TOwner* ownerInstance, CallbackFunction callbackFunction) {
		this->ownerInstance = ownerInstance;
		this->callbackFunction = callbackFunction;
	}
	~EventCallBack() override = default;
};

typedef std::list<std::unique_ptr<IEventCallBack>>HandlerList;

class EventBus {
private:
	std::map<std::type_index, std::unique_ptr<HandlerList>>subscribers;
public:
	EventBus() {
		Logger::Log("EventBus 构造");
	}
	~EventBus() {
		Logger::Log("EventBus 析构");
	}

	void Reset() {
		subscribers.clear();
	}

	template <typename TEvent, typename TOwner>
	void SubscribeToEvent(TOwner* ownerInstance, void (TOwner::* callbackFunction)(TEvent&)) {
		if (!subscribers[typeid(TEvent)].get()) {
			subscribers[typeid(TEvent)] = std::make_unique<HandlerList>();
		}
		auto subscriber = std::make_unique<EventCallBack<TOwner, TEvent>>(ownerInstance, callbackFunction);
		subscribers[typeid(TEvent)]->push_back(std::move(subscriber));
	}

	// 触发事件
	template <typename TEvent, typename ...TArgs>
	void EmitEvent(TArgs&& ...args) {
		auto handlers = subscribers[typeid(TEvent)].get();

		if (handlers) {
			for (auto it = handlers->begin(); it != handlers->end(); it++) {
				auto handler = it->get();
				TEvent event(std::forward<TArgs>(args)...);
				handler->Execute(event);
			}
		}
	}
};

#endif // !EVENTBUS_H
