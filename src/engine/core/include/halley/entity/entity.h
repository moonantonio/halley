#pragma once

#include <array>
#include <memory>
#include "component.h"
#include "message.h"
#include "family_mask.h"
#include "entity_id.h"
#include "type_deleter.h"
#include <halley/data_structures/vector.h>

#include "prefab.h"
#include "halley/utils/type_traits.h"
#include "halley/maths/uuid.h"
#include <gsl/span>

namespace Halley {
	class DataInterpolatorSet;
	class World;
	class System;
	class EntityRef;
	class Prefab;

	// True if T::onAddedToEntity(EntityRef&) exists
	template <class, class = std::void_t<>> struct HasOnAddedToEntityMember : std::false_type {};
	template <class T> struct HasOnAddedToEntityMember<T, decltype(std::declval<T&>().onAddedToEntity(std::declval<EntityRef&>()))> : std::true_type { };
	
	class MessageEntry
	{
	public:
		std::unique_ptr<Message> msg;
		int type = -1;
		int age = -1;

		MessageEntry() {}
		MessageEntry(std::unique_ptr<Message> msg, int type, int age) : msg(std::move(msg)), type(type), age(age) {}
	};

	class EntityRef;
	class ConstEntityRef;

	using WorldPartitionId = uint16_t;

	class Entity
	{
		friend class World;
		friend class System;
		friend class EntityRef;
		friend class ConstEntityRef;

	public:
		~Entity();

		template <typename T>
		T* tryGetComponent(bool evenIfDisabled = false)
		{
			if (evenIfDisabled || (enabled && parentEnabled)) {
				constexpr int id = FamilyMask::RetrieveComponentIndex<T>::componentIndex;
				for (uint8_t i = 0; i < liveComponents; i++) {
					if (components[i].first == id) {
						return static_cast<T*>(components[i].second);
					}
				}
			}
			return nullptr;
		}

		template <typename T>
		const T* tryGetComponent(bool evenIfDisabled = false) const
		{
			if (evenIfDisabled || (enabled && parentEnabled)) {
				constexpr int id = FamilyMask::RetrieveComponentIndex<T>::componentIndex;
				for (uint8_t i = 0; i < liveComponents; i++) {
					if (components[i].first == id) {
						return static_cast<const T*>(components[i].second);
					}
				}
			}
			return nullptr;
		}

		template <typename T>
		T& getComponent()
		{
			auto value = tryGetComponent<T>();
			if (value) {
				return *value;
			} else {
				throw Exception("Component " + String(typeid(T).name()) + " does not exist in entity.", HalleyExceptions::Entity);
			}
		}

		template <typename T>
		const T& getComponent() const
		{
			auto value = tryGetComponent<T>();
			if (value) {
				return *value;
			} else {
				throw Exception("Component " + String(typeid(T).name()) + " does not exist in entity.", HalleyExceptions::Entity);
			}
		}

		template <typename T>
		bool hasComponent(const World& world) const
		{
			if (dirty) {
				return tryGetComponent<T>() != nullptr;
			} else {
				return hasBit(world, FamilyMask::RetrieveComponentIndex<T>::componentIndex);
			}
		}

		template <typename ... Ts>
		bool hasAnyComponent(const World& world) const
		{
			if (dirty) {
				return dirtyHasAnyComponents<Ts...>(world);
			} else {
				std::array<int, sizeof...(Ts)> result;
				auto span = gsl::span<int>(result.data(), result.size());
				getComponentIndices<Ts...>(result);
				return hasAnyBit(world, span);
			}
		}

		bool needsRefresh() const
		{
			return dirty;
		}

		bool isAlive() const
		{
			return alive;
		}

		bool isEnabled() const
		{
			return enabled;
		}

		void setEnabled(bool enabled);
		
		const UUID& getPrefabUUID() const
		{
			return prefabUUID;
		}

		const UUID& getInstanceUUID() const
		{
			return instanceUUID;
		}

		FamilyMaskType getMask() const;
		EntityId getEntityId() const;

		void refresh(MaskStorage* storage, ComponentDeleterTable& table);
		
		void sortChildrenByInstanceUUIDs(const Vector<UUID>& uuids);

		bool isEmpty() const;

		bool isRemote(const World& world) const;

		int getParentingDepth() const;

	private:
		// !!! WARNING !!!
		// The order of elements in this class was carefully chosen to maximise cache performance!
		// Be SURE to verify that no performance-critical fields get bumped to a worse cacheline if you change anything here

		// Cacheline 0
		Vector<std::pair<int, Component*>> components;
		uint8_t liveComponents = 0;
		bool dirty : 1;
		bool alive : 1;
		bool serializable : 1;
		bool reloaded : 1;
		bool enabled : 1;
		bool parentEnabled : 1;
		bool selectable : 1;
		bool fromNetwork : 1;
		
		uint8_t childrenRevision = 0;

		FamilyMaskType mask;
		Entity* parent = nullptr;
		EntityId entityId;
		Vector<Entity*> children; // Cacheline 1 starts 16 bytes into this

		// Cacheline 1
		Vector<MessageEntry> inbox;
		String name;

		// Cacheline 2
		UUID instanceUUID;
		UUID prefabUUID;
		std::shared_ptr<const Prefab> prefab;

		WorldPartitionId worldPartition = 0;
		uint8_t hierarchyRevision = 0;
		uint8_t componentRevision = 0;

		Entity();
		void destroyComponents(ComponentDeleterTable& storage);

		template <typename T>
		Entity& addComponent(World& world, T* component)
		{
			addComponent(component, T::componentIndex);
			TypeDeleter<T>::initialize(getComponentDeleterTable(world));

			markDirty(world);
			return *this;
		}

		template <typename T>
		Entity& removeComponent(World& world)
		{
			removeComponentById(world, T::componentIndex);
			return *this;
		}

		void addComponent(Component* component, int id);
		void removeComponentAt(int index);
		void removeComponentById(World& world, int id);
		void removeAllComponents(World& world);
		void deleteComponent(Component* component, int id, ComponentDeleterTable& table);
		void keepOnlyComponentsWithIds(const Vector<int>& ids, World& world);

		void onReady();

		void markDirty(World& world);
		ComponentDeleterTable& getComponentDeleterTable(World& world);

		Entity* getParent() const { return parent; }
		void setParent(Entity* parent, bool propagate = true, size_t childIdx = -1);
		const Vector<Entity*>& getChildren() const { return children; }
		void addChild(Entity& child);
		void detachChildren();
		void markHierarchyDirty();
		void propagateChildrenChange();
		void propagateChildWorldPartition(WorldPartitionId newWorldPartition);
		void propagateEnabled(bool enabled, bool parentEnabled);

		DataInterpolatorSet& setupNetwork(EntityRef& ref, uint8_t peerId);
		std::optional<uint8_t> getOwnerPeerId() const;
		void setFromNetwork(bool fromNetwork);

		void destroy(World& world);
		void doDestroy(World& world, bool updateParenting);

		bool hasBit(const World& world, int index) const;
		bool hasAnyBit(const World& world, gsl::span<const int> indices) const;
		
		template <typename T, typename ... Ts>
		constexpr static void getComponentIndices(gsl::span<int> dst)
		{
			dst[0] = FamilyMask::RetrieveComponentIndex<T>::componentIndex;
			if constexpr (sizeof...(Ts) > 0) {
				getComponentIndices<Ts...>(dst.subspan(1));
			}
		}

		template <typename T, typename ... Ts>
		bool dirtyHasAnyComponents(const World& world) const
		{
			if (tryGetComponent<T>() != nullptr) {
				return true;
			}
			if constexpr (sizeof...(Ts) > 0) {
				return dirtyHasAnyComponents<Ts...>(world);
			} else {
				return false;
			}
		}
	};

	class EntityRef;
	
	class EntityRefIterable {
	public:
		class Iterator {
		public:
			Iterator(Vector<Entity*>::const_iterator iter, World& world)
				: iter(iter)
				, world(world)
			{}

			Iterator& operator++()
			{
				++iter;
				return *this;
			}

			bool operator==(const Iterator& other)
			{
				return iter == other.iter;
			}

			bool operator!=(const Iterator& other)
			{
				return iter != other.iter;
			}

			EntityRef operator*() const;

		private:
			Vector<Entity*>::const_iterator iter;
			World& world;
		};
		
		EntityRefIterable(const Vector<Entity*>& entities, World& world)
			: entities(entities)
			, world(world)
		{}

		Iterator begin() const
		{
			return Iterator(entities.begin(), world);
		}

		Iterator end() const
		{
			return Iterator(entities.end(), world);
		}

	private:
		const Vector<Entity*>& entities;
		World& world;
	};

	class ConstEntityRef;

	class EntityRef
	{
		friend class ConstEntityRef;
	
	public:
		EntityRef() = default;
		EntityRef(const EntityRef& other) = default;
		EntityRef(EntityRef&& other) noexcept = default;

		EntityRef& operator=(const EntityRef& other) = default;
		EntityRef& operator=(EntityRef&& other) noexcept = default;

		EntityRef(Entity& e, World& w)
			: entity(&e)
			, world(&w)
		{
#ifdef _DEBUG
			entityId = entity->getEntityId();
#endif
		}

		~EntityRef() = default;

		template <typename T>
		EntityRef& addComponent(T&& component)
		{
			validateComponentType<T>();
			validate();
			
			auto c = new T(std::move(component));
			entity->addComponent(*world, c);

			if constexpr (HasOnAddedToEntityMember<T>::value) {
				c->onAddedToEntity(*this);
			}

			return *this;
		}

		template <typename T>
		EntityRef& removeComponent()
		{
			validateComponentType<T>();
			validate();
			entity->removeComponent<T>(*world);
			return *this;
		}

		EntityRef& removeComponentById(int id)
		{
			validate();
			entity->removeComponentById(*world, id);
			return *this;
		}

		EntityRef& removeAllComponents()
		{
			validate();
			entity->removeAllComponents(*world);
			return *this;
		}

		template <typename T>
		T& getComponent()
		{
			validateComponentType<T>();
			validate();
			return entity->getComponent<T>();
		}

		template <typename T>
		const T& getComponent() const
		{
			validateComponentType<T>();
			validate();
			return entity->getComponent<T>();
		}

		template <typename T>
		T* tryGetComponent(bool evenIfDisabled = false)
		{
			validateComponentType<T>();
			validate();
			return entity->tryGetComponent<T>(evenIfDisabled);
		}

		template <typename T>
		const T* tryGetComponent(bool evenIfDisabled = false) const
		{
			validateComponentType<T>();
			validate();
			return entity->tryGetComponent<T>(evenIfDisabled);
		}

		template <typename T>
		T* tryGetComponentInAncestors()
		{
			validateComponentType<T>();
			auto* c = tryGetComponent<T>();
			if (c) {
				return c;
			}
			if (auto parent = getParent(); parent.isValid()) {
				return parent.tryGetComponentInAncestors<T>();
			}
			return nullptr;
		}

		template <typename T>
		const T* tryGetComponentInAncestors() const
		{
			validateComponentType<T>();
			auto* c = tryGetComponent<T>();
			if (c) {
				return c;
			}
			if (const auto parent = getParent(); parent.isValid()) {
				return parent.tryGetComponentInAncestors<T>();
			}
			return nullptr;
		}

		template <typename T>
		EntityId tryGetEntityIdWithComponentInAncestors() const
		{
			validateComponentType<T>();
			auto* comp = tryGetComponent<T>();
			if (comp) {
				return getEntityId();
			}
			if (const auto parent = getParent(); parent.isValid()) {
				const auto parentId = parent.tryGetEntityIdWithComponentInAncestors<T>();
				if (parentId.isValid()) {
					return parentId;
				}
			}
			return {};
		}

		template <typename T>
		T* tryGetComponentInTree()
		{
			validateComponentType<T>();
			auto* comp = tryGetComponent<T>();
			if (comp) {
				return comp;
			}
			for (auto& child: getRawChildren()) {
				auto* childComp = EntityRef(*child, getWorld()).tryGetComponentInTree<T>();
				if (childComp) {
					return childComp;
				}
			}
			return nullptr;
		}

		template <typename T>
		const T* tryGetComponentInTree() const
		{
			validateComponentType<T>();
			auto* comp = tryGetComponent<T>();
			if (comp) {
				return comp;
			}
			for (auto& child: getRawChildren()) {
				auto* childComp = EntityRef(*child, getWorld()).tryGetComponentInTree<T>();
				if (childComp) {
					return childComp;
				}
			}
			return nullptr;
		}

		template <typename T>
		EntityId tryGetEntityIdWithComponentInTree() const
		{
			validateComponentType<T>();
			auto* comp = tryGetComponent<T>();
			if (comp) {
				return getEntityId();
			}
			for (auto& child : getRawChildren()) {
				auto childId = EntityRef(*child, getWorld()).tryGetEntityIdWithComponentInTree<T>();
				if (childId.isValid()) {
					return childId;
				}
			}
			return {};
		}

		EntityId getEntityId() const
		{
			if (!entity) {
				return EntityId();
			}
			validate();
			return entity->getEntityId();
		}

		template <typename T>
		bool hasComponent() const
		{
			validateComponentType<T>();
			validate();
			return entity->hasComponent<T>(*world);
		}

		template <typename ... Ts>
		bool hasAnyComponent() const
		{
			validate();
			return entity->hasAnyComponent<Ts...>(*world);
		}

		template <typename T>
		bool hasComponentInTree() const
		{
			validateComponentType<T>();
			if (hasComponent<T>()) {
				return true;
			}
			for (auto& c: getRawChildren()) {
				if (EntityRef(*c, getWorld()).hasComponentInTree<T>()) {
					return true;
				}
			}
			return false;
		}

		template <typename T>
		bool hasComponentInAncestors() const
		{
			validateComponentType<T>();
			if (hasComponent<T>()) {
				return true;
			}
			if (const auto parent = getParent(); parent.isValid()) {
				return parent.hasComponentInAncestors<T>();
			}
			return false;
		}

		bool hasEntityIdInAncestors(EntityId parentId) const
		{
			validate();
			for (auto parent = getParent(); parent.isValid(); parent = parent.getParent()) {
				if (parent.getEntityId() == parentId) {
					return true;
				}
			}

			return false;
		}

		const String& getName() const
		{
			validate();
			return entity->name;
		}

		void setName(String name)
		{
			validate();
			entity->name = std::move(name);
		}

		const UUID& getInstanceUUID() const
		{
			validate();
			return entity->instanceUUID;
		}

		const UUID& getPrefabUUID() const
		{
			validate();
			return entity->prefabUUID;
		}
		
		void keepOnlyComponentsWithIds(const Vector<int>& ids)
		{
			validate();
			entity->keepOnlyComponentsWithIds(ids, *world);
		}

		bool hasParent() const
		{
			validate();
			return entity->getParent() != nullptr;
		}
		
		EntityRef getParent() const
		{
			validate();
			auto parent = entity->getParent();
			return parent ? EntityRef(*parent, *world) : EntityRef();
		}

		std::optional<EntityRef> tryGetParent() const
		{
			validate();
			const auto parent = entity->getParent();
			return parent != nullptr ? EntityRef(*parent, *world) : std::optional<EntityRef>();
		}

		void setParent(const EntityRef& parent, size_t childIdx = -1)
		{
			validate();
			entity->setParent(parent.entity, true, childIdx);
		}

		void setParent()
		{
			validate();
			entity->setParent(nullptr);
		}

		const Vector<Entity*>& getRawChildren() const
		{
			validate();
			return entity->getChildren();
		}

		EntityRefIterable getChildren() const
		{
			validate();
			return EntityRefIterable(entity->getChildren(), *world);
		}

		EntityRef getChildWithName(const String& name) const
		{
			validate();

			for (const auto child : getChildren()) {
				if (child.getName() == name) {
					return child;
				}
			}

			return {};
		}

		bool hasChildren() const
		{
			validate();
			return !entity->getChildren().empty();
		}

		void addChild(EntityRef& child)
		{
			validate();
			entity->addChild(*child.entity);
		}

		void detachChildren()
		{
			validate();
			entity->detachChildren();
		}

		uint8_t getHierarchyRevision() const
		{
			validate();
			return entity->hierarchyRevision;
		}

		uint8_t getComponentRevision() const
		{
			validate();
			return entity->componentRevision;
		}

		uint8_t getChildrenRevision() const
		{
			validate();
			return entity->childrenRevision;
		}

		WorldPartitionId getWorldPartition() const
		{
			validate();
			return entity->worldPartition;
		}

		bool isValid() const
		{
			return entity != nullptr && world != nullptr;
		}

		bool isAlive() const
		{
			validate();
			return entity->isAlive();
		}

		bool isSelectable() const
		{
			validate();
			return entity->selectable;
		}

		void setSelectable(bool selectable)
		{
			validate();
			entity->selectable = selectable;
		}

		bool isEnabled() const
		{
			validate();
			return entity->isEnabled();
		}

		void setEnabled(bool enabled)
		{
			validate();
			entity->setEnabled(enabled);
		}

		bool operator==(const EntityRef& other) const
		{
			return entity == other.entity && world == other.world
#ifdef _DEBUG
			&& entityId == other.entityId
#endif
			;
		}

		bool operator!=(const EntityRef& other) const
		{
			return !(*this == other);
		}

		bool operator<(const EntityRef& other) const
		{
			return entity < other.entity;
		}

		int getParentingDepth() const
		{
			validate();
			return entity->getParentingDepth();
		}

		World& getWorld() const
		{
			validate();
			return *world;
		}

		size_t getNumComponents() const
		{
			validate();
			return static_cast<size_t>(entity->liveComponents);
		}

		std::pair<int, Component*> getRawComponent(size_t idx) const
		{
			validate();
			return entity->components[idx];
		}

		Vector<std::pair<int, Component*>>::iterator begin() const
		{
			validate();
			return entity->components.begin();
		}

		Vector<std::pair<int, Component*>>::iterator end() const
		{
			validate();
			return entity->components.begin() + entity->liveComponents;
		}

		EntityRef& setSerializable(bool serializable)
		{
			validate();
			entity->serializable = serializable;
			return *this;
		}

		bool isSerializable() const
		{
			validate();
			return entity->serializable;
		}

		void setReloaded();

		bool wasReloaded()
		{
			validate();
			return entity->reloaded;
		}

		void sortChildrenByInstanceUUIDs(const Vector<UUID>& uuids)
		{
			validate();
			entity->sortChildrenByInstanceUUIDs(uuids);
		}

		void setPrefab(std::shared_ptr<const Prefab> prefab, UUID prefabUUID)
		{
			validate();
			Expects(!prefab || prefabUUID.isValid());
			entity->prefab = std::move(prefab);
			entity->prefabUUID = prefabUUID;
		}

		const std::shared_ptr<const Prefab>& getPrefab() const
		{
			validate();
			return entity->prefab;
		}

		std::optional<String> getPrefabAssetId() const
		{
			return entity && entity->prefab ? entity->prefab->getAssetId() : std::optional<String>{};
		}

		DataInterpolatorSet& setupNetwork(uint8_t peerId)
		{
			Expects(entity);
			return entity->setupNetwork(*this, peerId);
		}

		std::optional<uint8_t> getOwnerPeerId() const
		{
			Expects(entity);
			return entity->getOwnerPeerId();
		}

		bool isRemote() const
		{
			Expects(entity);
			return entity->isRemote(*world);
		}

		bool isLocal() const
		{
			Expects(entity);
			return !entity->isRemote(*world);
		}

		void setFromNetwork(bool fromNetwork)
		{
			Expects(entity);
			entity->setFromNetwork(fromNetwork);
		}

		bool isEmpty() const
		{
			return !entity || entity->isEmpty();
		}

		void validate() const
		{
			Expects(isValid());
#ifdef _DEBUG
			Expects(entity->getEntityId() == entityId);
#endif
		}

	private:
		friend class World;

		Entity* entity = nullptr;
		World* world = nullptr;

#ifdef _DEBUG
		EntityId entityId;
#endif

		template <typename T>
		constexpr static void validateComponentType()
		{
			static_assert(!std::is_pointer<T>::value, "Cannot pass pointer to component");
			static_assert(!std::is_same<T, Component>::value, "Cannot add base class Component to entity, make sure type isn't being erased");
			static_assert(std::is_base_of<Component, T>::value, "Components must extend the Component class");
			static_assert(!std::is_polymorphic<T>::value, "Components cannot be polymorphic (i.e. they can't have virtual methods)");
			static_assert(std::is_default_constructible<T>::value, "Components must have a default constructor");
		}
	};
	
	class ConstEntityRef
	{
	public:
		ConstEntityRef() = default;
		ConstEntityRef(const ConstEntityRef& other) = default;
		ConstEntityRef(ConstEntityRef&& other) = default;

		ConstEntityRef& operator=(const ConstEntityRef& other) = default;
		ConstEntityRef& operator=(ConstEntityRef&& other) = default;

		ConstEntityRef(const Entity& e, const World& w)
			: entity(&e)
			, world(&w)
		{}

		ConstEntityRef(const EntityRef& e)
			: entity(e.entity)
			, world(e.world)
		{}
		
		template <typename T>
		const T& getComponent() const
		{
			return entity->getComponent<T>();
		}

		template <typename T>
		const T* tryGetComponent() const
		{
			return entity->tryGetComponent<T>();
		}

		EntityId getEntityId() const
		{
			return entity->getEntityId();
		}

		template <typename T>
		bool hasComponent() const
		{
			return entity->hasComponent<T>(*world);
		}

		const String& getName() const
		{
			return entity->name;
		}

		const UUID& getInstanceUUID() const
		{
			return entity->instanceUUID;
		}

		const UUID& getPrefabUUID() const
		{
			return entity->prefabUUID;
		}

		bool hasParent() const
		{
			return entity->getParent() != nullptr;
		}

		ConstEntityRef getParent() const
		{
			auto parent = entity->getParent();
			return parent != nullptr ? ConstEntityRef(*parent, *world) : ConstEntityRef();
		}

		std::optional<ConstEntityRef> tryGetParent() const
		{
			const auto parent = entity->getParent();
			return parent != nullptr ? ConstEntityRef(*parent, *world) : std::optional<ConstEntityRef>();
		}

		const Vector<Entity*>& getRawChildren() const
		{
			return entity->getChildren();
		}

		uint8_t getHierarchyRevision() const
		{
			return entity->hierarchyRevision;
		}

		uint8_t getComponentRevision() const
		{
			return entity->componentRevision;
		}

		uint8_t getChildrenRevision() const
		{
			return entity->childrenRevision;
		}

		size_t getNumComponents() const
		{
			Expects(entity);
			return size_t(entity->liveComponents);
		}

		std::pair<int, Component*> getRawComponent(size_t idx) const
		{
			Expects(entity);
			return entity->components[idx];
		}

		Vector<std::pair<int, Component*>>::const_iterator begin() const
		{
			Expects(entity);
			return entity->components.begin();
		}

		Vector<std::pair<int, Component*>>::const_iterator end() const
		{
			Expects(entity);
			return entity->components.begin() + entity->liveComponents;
		}

		bool isSerializable() const
		{
			Expects(entity);
			return entity->serializable;
		}

		bool isValid() const
		{
			return entity != nullptr;
		}

		std::optional<uint8_t> getOwnerPeerId() const
		{
			Expects(entity);
			return entity->getOwnerPeerId();
		}

		bool isRemote() const
		{
			Expects(entity);
			return entity->isRemote(*world);
		}

		bool isLocal() const
		{
			Expects(entity);
			return !entity->isRemote(*world);
		}

		template <typename T>
		const T* tryGetComponentInAncestors() const
		{
			auto* c = tryGetComponent<T>();
			if (c) {
				return c;
			}
			if (const auto parent = getParent(); parent.isValid()) {
				return parent.tryGetComponentInAncestors<T>();
			}
			return nullptr;
		}

		template <typename T>
		const T* tryGetComponentInTree() const
		{
			auto* comp = tryGetComponent<T>();
			if (comp) {
				return comp;
			}
			for (auto& child: getRawChildren()) {
				auto* childComp = ConstEntityRef(*child, *world).tryGetComponentInTree<T>();
				if (childComp) {
					return childComp;
				}
			}
			return nullptr;
		}

	private:
		friend class World;

		const Entity* entity;
		const World* world;
	};

	inline EntityRef EntityRefIterable::Iterator::operator*() const
	{
		return EntityRef(*(*iter), world);
	}
	
}
