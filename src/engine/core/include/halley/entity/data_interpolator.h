#pragma once
#include "entity.h"
#include "halley/bytes/config_node_serializer.h"
#include "halley/time/halleytime.h"
#include "halley/support/logger.h"

namespace Halley {
	class DataInterpolatorSet {
	public:
		void setInterpolator(std::shared_ptr<IDataInterpolator> interpolator, EntityId entity, std::string_view componentName, std::string_view fieldName);
		IDataInterpolator* tryGetInterpolator(EntityId entity, std::string_view componentName, std::string_view fieldName);
		bool setInterpolatorEnabled(EntityId entity, std::string_view componentName, std::string_view fieldName, bool enabled);

		bool isReady() const;
		void markReady();
		
		void update(Time time, World& world) const;
		size_t count() const;

	private:
		using Key = std::tuple<EntityId, std::string_view, std::string_view>;

		Vector<std::pair<Key, std::shared_ptr<IDataInterpolator>>> interpolators; // Vector as hashing this is complex and we only expect a few interpolators per entity
		bool ready = false;

		Key makeKey(EntityId entity, std::string_view componentName, std::string_view fieldName) const;
	};

	class DataInterpolatorSetRetriever : public IDataInterpolatorSetRetriever {
	public:
		DataInterpolatorSetRetriever(EntityRef rootEntity, bool collectUUIDs);
		
		IDataInterpolator* tryGetInterpolator(const EntitySerializationContext& context, std::string_view componentName, std::string_view fieldName) const override;
		IDataInterpolator* tryGetInterpolator(EntityId entityId, std::string_view componentName, std::string_view fieldName) const;
		ConfigNode createComponentDelta(const UUID& instanceUUID, const String& componentName, const ConfigNode& from, const ConfigNode& to) const override;
	
	private:
		DataInterpolatorSet* dataInterpolatorSet = nullptr;
		HashMap<UUID, EntityId> uuids;

		void collectUUIDs(EntityRef entity);
	};

	template <typename T>
	class DataInterpolator : public IDataInterpolator {
	public:
		void deserialize(void* value, const void* defaultValue, const EntitySerializationContext& context, const ConfigNode& node) override
		{
			if (enabled) {
				doDeserialize(*static_cast<T*>(value), *static_cast<const T*>(defaultValue), context, node);
			}
		}
		
		void setEnabled(bool enabled) override { this->enabled = enabled; }
		bool isEnabled() const override { return enabled; }

	protected:
		virtual void doDeserialize(T& value, const T& defaultValue, const EntitySerializationContext& context, const ConfigNode& node)
		{
			ConfigNodeHelper<T>::deserialize(value, defaultValue, context, node);
		}

	private:
		bool enabled = true;
	};

	template <typename T, typename Intermediate = T>
	class QuantizingDataInterpolator : public DataInterpolator<T> {
	public:
		QuantizingDataInterpolator(std::optional<float> granularity = {})
			: granularity(granularity)
		{}

		std::optional<ConfigNode> prepareFieldForSerialization(const ConfigNode& fromValue, const ConfigNode& toValue) override
		{
			if (toValue.getType() != ConfigNodeType::Del && toValue.getType() != ConfigNodeType::Undefined && granularity) {
				auto from = quantize<T>(fromValue.asType<T>(), granularity.value());
				auto to = quantize<T>(toValue.asType<T>(), granularity.value());
				if (from == to) {
					return ConfigNode(fromValue);
				}
				return ConfigNode(to);
			} else {
				return {};
			}
		}
	
	private:
		std::optional<float> granularity;
	};

	template <typename T, typename Intermediate = T, typename Base = DataInterpolator<T>>
	class LerpDataInterpolator : public Base {
	public:
		template <typename ... Args>
		LerpDataInterpolator(Time length, Args... args)
			: Base(args...)
			, length(length)
		{}

		bool update(Time t) override
		{
			if (targetValue) {
				const Time stepT = std::min(t, timeLeft);
				if (stepT > 0.0000001) {
					if constexpr (std::is_same_v<T, Intermediate>) {
						*targetValue += static_cast<T>(delta * (stepT / length));
					} else {
						*targetValue = T(static_cast<Intermediate>(*targetValue) + static_cast<Intermediate>(delta * (stepT / length)));
					}
				}
				timeLeft -= stepT;
				if (timeLeft <= 0) {
					targetValue = nullptr;
				}
				return true;
			}
			return false;
		}

	protected:
		void doDeserialize(T& value, const T& defaultValue, const EntitySerializationContext& context, const ConfigNode& node) override
		{
			T newValue = value;
			ConfigNodeHelper<T>::deserialize(newValue, defaultValue, context, node);

			if (shouldApplyInstantly(value, newValue)) {
				value = newValue;
				
				delta = Intermediate();
				timeLeft = 0;
				targetValue = nullptr;
			} else {
				if constexpr (std::is_same_v<T, Intermediate>) {
					delta = newValue - value;
				} else {
					delta = Intermediate(newValue) - Intermediate(value);
				}

				timeLeft = length;
				targetValue = &value;
			}
		}

		virtual bool shouldApplyInstantly(const T& prevValue, const T& newValue)
		{
			return false;
		}

	private:
		const Time length;
		Time timeLeft = 0;
		Intermediate delta;
		T* targetValue = nullptr;
	};

	template <typename T, typename Intermediate = T>
	using QuantizingLerpDataInterpolator = LerpDataInterpolator<T, Intermediate, QuantizingDataInterpolator<T, Intermediate>>;



	class DeadReckoningInterpolator : public DataInterpolator<Vector2f> {
	public:
		bool update(Time t) override;
		std::optional<ConfigNode> prepareFieldForSerialization(const ConfigNode& fromValue, const ConfigNode& toValue) override;

		void setVelocity(Vector2f vel);
		void setVelocityRef(Vector2f& value);

	protected:
		void doDeserialize(Vector2f& value, const Vector2f& defaultValue, const EntitySerializationContext& context, const ConfigNode& node) override;

	private:
		Vector2f outboundVel;
		Vector2f* velRef = nullptr;
	};

	class DeadReckoningVelocityInterpolator : public DataInterpolator<Vector2f> {
	public:
		DeadReckoningVelocityInterpolator(std::shared_ptr<DeadReckoningInterpolator> parent);

		bool update(Time t) override;
		std::optional<ConfigNode> prepareFieldForSerialization(const ConfigNode& fromValue, const ConfigNode& toValue) override;

	protected:
		void doDeserialize(Vector2f& value, const Vector2f& defaultValue, const EntitySerializationContext& context, const ConfigNode& node) override;

	private:
		std::shared_ptr<DeadReckoningInterpolator> parent;
	};
}
