#pragma once

#include <span>
#include <string_view>
#include <variant>

#include "openvic-simulation/core/memory/String.hpp"
#include "openvic-simulation/core/memory/Vector.hpp"
#include "openvic-simulation/scripts/Condition.hpp"
#include "openvic-simulation/scripts/ConditionScript.hpp"
#include "openvic-simulation/types/HasIdentifier.hpp"
#include "openvic-simulation/types/IdentifierRegistry.hpp"

namespace OpenVic {
	struct DefinitionManager;
	struct EffectManager;
	struct EffectNode;
	struct ExecutionContext;

	/* Effects reuse the condition value/scope/identifier type vocabulary (value_type_t,
	 * scope_type_t, identifier_type_t from Condition.hpp) - the script grammar is shared,
	 * only the semantics differ: conditions read game state, effects mutate it. */
	struct Effect : HasIdentifier {
		friend struct EffectManager;

	public:
		/* Executes a parsed instance of this effect against the current game state. Bound once at
		 * registration time so execution dispatches without string comparisons; effects without an
		 * implemented executor are bound to EffectExecutors::unimplemented. */
		using execute_fn_t = void (*)(ExecutionContext& context, EffectNode const& node);

		const value_type_t value_type;
		const scope_type_t scope;
		const scope_type_t scope_change;
		const identifier_type_t value_identifier_type;
		const execute_fn_t execute_fn;

		Effect(
			std::string_view new_identifier, value_type_t new_value_type, scope_type_t new_scope,
			scope_type_t new_scope_change, identifier_type_t new_value_identifier_type,
			execute_fn_t new_execute_fn
		);
		Effect(Effect&&) = default;
	};

	struct EffectNode {
		friend struct EffectManager;
		friend struct EffectScript;

		using string_t = memory::string;
		using boolean_t = bool;
		using integer_t = uint64_t;
		using real_t = fixed_point_t;
		/* Dict-form event effects: country_event = { id = X days = Y }. */
		using delayed_event_t = std::pair<integer_t, integer_t>;
		using effect_list_t = memory::vector<EffectNode>;
		using value_t = std::variant<string_t, boolean_t, integer_t, real_t, delayed_event_t, effect_list_t>;

	private:
		/* Null for the root node of a script, whose value is the list of top-level effects. */
		Effect const* PROPERTY(effect);
		value_t PROPERTY(value);
		HasIdentifier const* PROPERTY(effect_value_item);
		/* Condition gating this node's execution - only ever parsed for group effects
		 * containing a limit block; an unparsed limit gates nothing. */
		ConditionScript PROPERTY_REF(limit);
		bool PROPERTY_CUSTOM_PREFIX(has_limit, does);
		bool PROPERTY_CUSTOM_PREFIX(valid, is);

		EffectNode(
			Effect const* new_effect = nullptr, value_t&& new_value = 0, bool new_valid = false,
			HasIdentifier const* new_effect_value_item = nullptr,
			ConditionScript&& new_limit = { scope_type_t::NO_SCOPE, scope_type_t::NO_SCOPE, scope_type_t::NO_SCOPE },
			bool new_has_limit = false
		);

	public:
		EffectNode(EffectNode&&) = default;
		EffectNode& operator=(EffectNode&&) = default;

		/* Execute this effect against the given game state context. Invalid or unparsed nodes
		 * do nothing. The root node executes its children in order. */
		void execute(ExecutionContext& context) const;
	};

	struct EffectManager {
	private:
		CaseInsensitiveIdentifierRegistry<Effect> IDENTIFIER_REGISTRY(effect);

		bool add_effect(
			std::string_view identifier, value_type_t value_type, scope_type_t scope,
			scope_type_t scope_change = scope_type_t::NO_SCOPE,
			identifier_type_t value_identifier_type = identifier_type_t::NO_IDENTIFIER,
			Effect::execute_fn_t execute_fn = nullptr
		);

		NodeTools::node_callback_t expect_effect_node(
			DefinitionManager const& definition_manager, Effect const& effect, scope_type_t current_scope,
			scope_type_t this_scope, scope_type_t from_scope, NodeTools::callback_t<EffectNode&&> callback
		) const;

		/* limit_out/has_limit_out receive a parsed limit block if one is encountered - they are
		 * null at the top level of a script, where limit blocks are not allowed. */
		NodeTools::node_callback_t expect_effect_node_list(
			DefinitionManager const& definition_manager, scope_type_t current_scope, scope_type_t this_scope,
			scope_type_t from_scope, NodeTools::callback_t<EffectNode&&> callback,
			ConditionScript* limit_out, bool* has_limit_out
		) const;

	public:
		bool setup_effects(DefinitionManager const& definition_manager);

		bool expect_effect_script(
			DefinitionManager const& definition_manager, scope_type_t initial_scope, scope_type_t this_scope,
			scope_type_t from_scope, NodeTools::callback_t<EffectNode&&> callback, std::span<const ast::NodeCPtr> nodes
		) const;
	};
}
