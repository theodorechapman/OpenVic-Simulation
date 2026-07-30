#pragma once

#include "openvic-simulation/scripts/Condition.hpp"
#include "openvic-simulation/scripts/Effect.hpp"

namespace OpenVic {
	struct ScriptManager {
	private:
		ConditionManager PROPERTY_REF(condition_manager);
		EffectManager PROPERTY_REF(effect_manager);
	};
}
