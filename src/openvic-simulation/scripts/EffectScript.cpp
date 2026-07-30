#include "EffectScript.hpp"

#include "openvic-simulation/DefinitionManager.hpp"

using namespace OpenVic;

EffectScript::EffectScript(
	scope_type_t new_initial_scope, scope_type_t new_this_scope, scope_type_t new_from_scope
) : initial_scope { new_initial_scope }, this_scope { new_this_scope }, from_scope { new_from_scope } {}

void EffectScript::execute(ExecutionContext& context) const {
	effect_root.execute(context);
}

bool EffectScript::_parse_script(std::span<const ast::NodeCPtr> nodes, DefinitionManager const& definition_manager) {
	return definition_manager.get_script_manager().get_effect_manager().expect_effect_script(
		definition_manager,
		initial_scope,
		this_scope,
		from_scope,
		NodeTools::move_variable_callback(effect_root),
		nodes
	);
}
