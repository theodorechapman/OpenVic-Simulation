#include "DecisionInstanceManager.hpp"

#include "openvic-simulation/country/CountryInstance.hpp"
#include "openvic-simulation/country/CountryInstanceManager.hpp"
#include "openvic-simulation/DefinitionManager.hpp"
#include "openvic-simulation/InstanceManager.hpp"
#include "openvic-simulation/misc/Decision.hpp"
#include "openvic-simulation/scripts/EvaluationContext.hpp"
#include "openvic-simulation/scripts/ExecutionContext.hpp"

using namespace OpenVic;

void DecisionInstanceManager::ai_decisions_tick(InstanceManager& instance_manager) {
	for (CountryInstance& country : instance_manager.get_country_instance_manager().get_country_instances()) {
		if (!country.exists() || !country.is_ai()) {
			continue;
		}

		const EvaluationContext context { instance_manager, &country, &country };

		for (Decision const& decision : instance_manager.definition_manager.get_decision_manager().get_decisions()) {
			if (!decision.check_potential(context) || !decision.check_allow(context)) {
				continue;
			}
			if (decision.evaluate_ai_desire(context) <= 0) {
				continue;
			}

			SPDLOG_INFO("AI country {} takes decision {}", country.get_identifier(), decision.get_identifier());

			ExecutionContext execution_context { instance_manager, &country, &country };
			decision.take_decision(execution_context);
		}
	}
}
