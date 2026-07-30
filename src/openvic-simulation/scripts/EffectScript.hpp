#pragma once

#include "openvic-simulation/scripts/Effect.hpp"
#include "openvic-simulation/scripts/Script.hpp"

namespace OpenVic {
	struct DefinitionManager;
	struct ExecutionContext;

	struct EffectScript final : Script<DefinitionManager const&> {

	private:
		EffectNode PROPERTY_REF(effect_root);
		scope_type_t PROPERTY(initial_scope);
		scope_type_t PROPERTY(this_scope);
		scope_type_t PROPERTY(from_scope);

	protected:
		bool _parse_script(std::span<const ast::NodeCPtr> nodes, DefinitionManager const& definition_manager) override;

	public:
		/* TODO - existing owners (Event, Decision) default-construct their EffectScripts, so country
		 * scopes are assumed until they are updated to pass their actual scopes. */
		EffectScript(
			scope_type_t new_initial_scope = scope_type_t::COUNTRY, scope_type_t new_this_scope = scope_type_t::COUNTRY,
			scope_type_t new_from_scope = scope_type_t::NO_SCOPE
		);

		/* Execute the parsed effect list against the given game state context.
		 * Does nothing if the script has not been (successfully) parsed. */
		void execute(ExecutionContext& context) const;
	};
}
