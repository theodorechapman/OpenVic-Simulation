#pragma once

#include "openvic-simulation/scripts/Condition.hpp"
#include "openvic-simulation/scripts/Script.hpp"

namespace OpenVic {
	struct DefinitionManager;
	struct EvaluationContext;

	struct ConditionScript final : Script<DefinitionManager const&> {

	private:
		ConditionNode PROPERTY_REF(condition_root);
		scope_type_t PROPERTY(initial_scope);
		scope_type_t PROPERTY(this_scope);
		scope_type_t PROPERTY(from_scope);

	protected:
		bool _parse_script(std::span<const ast::NodeCPtr> nodes, DefinitionManager const& definition_manager) override;

	public:
		ConditionScript(scope_type_t new_initial_scope, scope_type_t new_this_scope, scope_type_t new_from_scope);

		/* Evaluate the parsed condition tree against the given game state context.
		 * Returns false if the script has not been (successfully) parsed. */
		bool evaluate(EvaluationContext const& context) const;
	};
}
