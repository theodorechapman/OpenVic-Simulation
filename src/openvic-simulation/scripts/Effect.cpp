#include "Effect.hpp"

#include <mutex>

#include "openvic-simulation/dataloader/NodeTools.hpp"
#include "openvic-simulation/DefinitionManager.hpp"
#include "openvic-simulation/scripts/EffectExecutors.hpp"
#include "openvic-simulation/scripts/ExecutionContext.hpp"

using namespace OpenVic;
using namespace OpenVic::NodeTools;

using enum value_type_t;
using enum scope_type_t;
using enum identifier_type_t;

Effect::Effect(
	std::string_view new_identifier, value_type_t new_value_type, scope_type_t new_scope,
	scope_type_t new_scope_change, identifier_type_t new_value_identifier_type, execute_fn_t new_execute_fn
) : HasIdentifier { new_identifier }, value_type { new_value_type }, scope { new_scope },
	scope_change { new_scope_change }, value_identifier_type { new_value_identifier_type },
	execute_fn { new_execute_fn } {}

EffectNode::EffectNode(
	Effect const* new_effect, value_t&& new_value, bool new_valid,
	HasIdentifier const* new_effect_value_item, ConditionScript&& new_limit, bool new_has_limit
) : effect { new_effect }, value { std::move(new_value) }, valid { new_valid },
	effect_value_item { new_effect_value_item }, limit { std::move(new_limit) }, has_limit { new_has_limit } {}

void EffectNode::execute(ExecutionContext& context) const {
	if (!valid) {
		return;
	}

	if (effect == nullptr) {
		/* Root node - execute the top-level effect list in order. */
		if (effect_list_t const* children = std::get_if<effect_list_t>(&value)) {
			for (EffectNode const& child : *children) {
				child.execute(context);
			}
		}
		return;
	}

	effect->execute_fn(context, *this);
}

bool EffectManager::add_effect(
	std::string_view identifier, value_type_t value_type, scope_type_t scope, scope_type_t scope_change,
	identifier_type_t value_identifier_type, Effect::execute_fn_t execute_fn
) {
	if (identifier.empty()) {
		spdlog::error_s("Invalid effect identifier - empty!");
		return false;
	}

	if (value_type == NO_TYPE || value_type > MAX_VALUE) {
		spdlog::error_s("Effect {} has invalid value type: {}", identifier, static_cast<uint64_t>(value_type));
		return false;
	}
	if (scope == NO_SCOPE || scope > MAX_SCOPE) {
		spdlog::error_s("Effect {} has invalid scope: {}", identifier, static_cast<uint64_t>(scope));
		return false;
	}

	if (share_value_type(value_type, IDENTIFIER) && value_identifier_type == NO_IDENTIFIER) {
		spdlog::error_s("Effect {} has no identifier type!", identifier);
		return false;
	}

	if (execute_fn == nullptr) {
		execute_fn = EffectExecutors::unimplemented;
	}

	return effects.emplace_item(
		identifier,
		identifier, value_type, scope, scope_change, value_identifier_type, execute_fn
	);
}

bool EffectManager::setup_effects(DefinitionManager const& definition_manager) {
	bool ret = true;

	/* Scope-changing effects */
	ret &= add_effect("THIS", GROUP, MAX_SCOPE, THIS, NO_IDENTIFIER, EffectExecutors::redirect_this);
	ret &= add_effect("FROM", GROUP, MAX_SCOPE, FROM, NO_IDENTIFIER, EffectExecutors::redirect_from);
	ret &= add_effect("capital_scope", GROUP, COUNTRY, PROVINCE, NO_IDENTIFIER, EffectExecutors::capital_scope);
	ret &= add_effect("owner", GROUP, PROVINCE, COUNTRY, NO_IDENTIFIER, EffectExecutors::province_owner);
	ret &= add_effect("any_owned", GROUP, COUNTRY, PROVINCE, NO_IDENTIFIER, EffectExecutors::any_owned_province);

	/* Flag effects */
	ret &= add_effect("set_global_flag", IDENTIFIER, MAX_SCOPE, NO_SCOPE, GLOBAL_FLAG, EffectExecutors::set_global_flag);
	ret &= add_effect("clr_global_flag", IDENTIFIER, MAX_SCOPE, NO_SCOPE, GLOBAL_FLAG, EffectExecutors::clr_global_flag);
	ret &= add_effect(
		"set_country_flag", IDENTIFIER, COUNTRY, NO_SCOPE, COUNTRY_FLAG, EffectExecutors::set_country_flag
	);
	ret &= add_effect(
		"clr_country_flag", IDENTIFIER, COUNTRY, NO_SCOPE, COUNTRY_FLAG, EffectExecutors::clr_country_flag
	);

	ret &= add_effect(
		"set_province_flag", IDENTIFIER, PROVINCE, NO_SCOPE, PROVINCE_FLAG, EffectExecutors::set_province_flag
	);
	ret &= add_effect(
		"clr_province_flag", IDENTIFIER, PROVINCE, NO_SCOPE, PROVINCE_FLAG, EffectExecutors::clr_province_flag
	);

	ret &= add_effect("trade_goods", IDENTIFIER, PROVINCE, NO_SCOPE, TRADE_GOOD, EffectExecutors::change_rgo_good);

	/* Event chain effects */
	ret &= add_effect(
		"country_event", INTEGER | COMPLEX, COUNTRY, NO_SCOPE, NO_IDENTIFIER, EffectExecutors::country_event
	);
	ret &= add_effect(
		"province_event", INTEGER | COMPLEX, PROVINCE, NO_SCOPE, NO_IDENTIFIER, EffectExecutors::province_event
	);

	/* Core effects */
	ret &= add_effect(
		"add_core", IDENTIFIER, PROVINCE | COUNTRY, NO_SCOPE, COUNTRY_TAG | PROVINCE_ID, EffectExecutors::add_core
	);
	ret &= add_effect(
		"remove_core", IDENTIFIER, PROVINCE | COUNTRY, NO_SCOPE, COUNTRY_TAG | PROVINCE_ID, EffectExecutors::remove_core
	);

	lock_effects();

	return ret;
}

node_callback_t EffectManager::expect_effect_node(
	DefinitionManager const& definition_manager, Effect const& effect, scope_type_t current_scope,
	scope_type_t this_scope, scope_type_t from_scope, callback_t<EffectNode&&> callback
) const {
	return [this, &definition_manager, &effect, callback, current_scope, this_scope, from_scope](
		ast::NodeCPtr node
	) mutable -> bool {
		bool ret = false;
		EffectNode::value_t value;
		HasIdentifier const* value_item = nullptr;
		ConditionScript limit { NO_SCOPE, NO_SCOPE, NO_SCOPE };
		bool has_limit = false;

		const std::string_view identifier = effect.get_identifier();
		const value_type_t value_type = effect.value_type;

		/* Magic-syntax dictionary effects - checked first (gated on the node actually being a
		 * dictionary) so dual-typed effects like country_event don't attempt a scalar parse on
		 * dictionary nodes or vice versa. */
		if (!ret && share_value_type(value_type, COMPLEX) && dryad::node_try_cast<ast::ListValue>(node) != nullptr) {
			if (identifier == "country_event" || identifier == "province_event") {
				/* country_event = { id = X days = Y } - fire event X for the current scope in Y days. */
				EffectNode::integer_t event_id = 0;
				EffectNode::integer_t days = 0;
				ret |= expect_dictionary_keys(
					"id", ONE_EXACTLY, expect_uint64(assign_variable_callback(event_id)),
					"days", ZERO_OR_ONE, expect_uint64(assign_variable_callback(days))
				)(node);
				if (ret) {
					value = EffectNode::delayed_event_t { event_id, days };
				}
			} else {
				spdlog::error_s("Attempted to parse unknown complex effect {}!", identifier);
			}
		}

		if (!ret && share_value_type(value_type, IDENTIFIER)) {
			std::string_view value_identifier {};
			ret |= expect_identifier_or_string(assign_variable_callback(value_identifier))(node);
			if (ret) {
				value = EffectNode::string_t { value_identifier };

				ret |= definition_manager.get_script_manager().get_condition_manager().expect_parse_identifier(
					definition_manager, effect.value_identifier_type, assign_variable_callback(value_item)
				)(value_identifier);
				/* Flags and variables are free-form names rather than registered identifiers,
				 * so a null value_item is expected for them. */
				const identifier_type_t free_form_types = VARIABLE | GLOBAL_FLAG | COUNTRY_FLAG | PROVINCE_FLAG;
				if (value_item == nullptr && value_type == IDENTIFIER &&
					!share_identifier_type(effect.value_identifier_type, free_form_types)) {
					spdlog::warn_s(
						"Unrecognised identifier {} for effect {} - the effect may do nothing!",
						value_identifier, identifier
					);
				}
			}
		}

		if (!ret && share_value_type(value_type, STRING)) {
			std::string_view value_string {};
			const bool local_ret = expect_identifier_or_string(assign_variable_callback(value_string))(node);
			ret |= local_ret;
			if (local_ret) {
				value = EffectNode::string_t { value_string };
			}
		}

		/* Parse into typed locals rather than the variant itself - assign_variable_callback(value)
		 * would require the variant (and so move-only EffectNode) to be copy-assignable. */
		if (!ret && share_value_type(value_type, BOOLEAN)) {
			EffectNode::boolean_t boolean_value = false;
			ret |= expect_bool(assign_variable_callback(boolean_value))(node);
			if (ret) {
				value = boolean_value;
			}
		}
		if (!ret && share_value_type(value_type, BOOLEAN_INT)) {
			EffectNode::boolean_t boolean_value = false;
			ret |= expect_int_bool(assign_variable_callback(boolean_value))(node);
			if (ret) {
				value = boolean_value;
			}
		}
		if (!ret && share_value_type(value_type, INTEGER)) {
			EffectNode::integer_t integer_value = 0;
			ret |= expect_uint64(assign_variable_callback(integer_value))(node);
			if (ret) {
				value = integer_value;
			}
		}
		if (!ret && share_value_type(value_type, REAL)) {
			EffectNode::real_t real_value = 0;
			ret |= expect_fixed_point(assign_variable_callback(real_value))(node);
			if (ret) {
				value = real_value;
			}
		}

		if (!ret && share_value_type(value_type, GROUP)) {
			const scope_type_t child_scope = effect.scope_change == NO_SCOPE ? current_scope : effect.scope_change;

			EffectNode::effect_list_t children;
			ret |= expect_effect_node_list(
				definition_manager,
				child_scope,
				this_scope,
				from_scope,
				vector_callback(children),
				&limit,
				&has_limit
			)(node);
			value = std::move(children);
		}

		if (!ret) {
			spdlog::warn_s("Could not parse effect node {} - it will do nothing!", identifier);
			return callback({});
		}

		return callback({
			&effect,
			std::move(value),
			true,
			value_item,
			std::move(limit),
			has_limit
		});
	};
}

node_callback_t EffectManager::expect_effect_node_list(
	DefinitionManager const& definition_manager, scope_type_t current_scope, scope_type_t this_scope,
	scope_type_t from_scope, callback_t<EffectNode&&> callback, ConditionScript* limit_out, bool* has_limit_out
) const {
	return [this, &definition_manager, callback, limit_out, has_limit_out, current_scope, this_scope, from_scope](
		ast::NodeCPtr node
	) mutable -> bool {
		const auto expect_node = [this, &definition_manager, callback, current_scope, this_scope, from_scope](
			Effect const& effect, ast::NodeCPtr effect_node
		) -> bool {
			return expect_effect_node(
				definition_manager, effect, current_scope, this_scope, from_scope, callback
			)(effect_node);
		};

		const auto unknown_effect_node = [
			&definition_manager, limit_out, has_limit_out, current_scope, this_scope, from_scope
		](std::string_view id, ast::NodeCPtr unknown_node) -> bool {
			if (id == "limit") {
				if (limit_out == nullptr || has_limit_out == nullptr) {
					spdlog::error_s("Encountered limit block at the top level of an effect script!");
					return false;
				}
				if (*has_limit_out) {
					spdlog::error_s("Effect group has multiple limit blocks!");
					return false;
				}
				ConditionScript parsed_limit { current_scope, this_scope, from_scope };
				parsed_limit.expect_script()(unknown_node);
				if (!parsed_limit.parse_script(false, definition_manager)) {
					spdlog::error_s("Failed to parse limit block in effect list!");
					return false;
				}
				*limit_out = std::move(parsed_limit);
				*has_limit_out = true;
				return true;
			}

			/* Warn once per unknown effect so incomplete effect coverage doesn't flood the log
			 * while game files still load. */
			static case_insensitive_string_set_t warned_effects;
			static std::mutex warned_effects_mutex;

			const std::lock_guard<std::mutex> lock_guard { warned_effects_mutex };
			if (warned_effects.emplace(id).second) {
				spdlog::warn_s("Effect {} is not registered and will be skipped wherever it appears!", id);
			}
			return true;
		};

		return effects.expect_item_dictionary_and_default(
			unknown_effect_node,
			expect_node
		)(node);
	};
}

bool EffectManager::expect_effect_script(
	DefinitionManager const& definition_manager, scope_type_t initial_scope, scope_type_t this_scope,
	scope_type_t from_scope, callback_t<EffectNode&&> callback, std::span<const ast::NodeCPtr> nodes
) const {
	EffectNode::effect_list_t effect_list;
	bool ret = true;

	for (const ast::NodeCPtr node : nodes) {
		ret &= expect_effect_node_list(
			definition_manager,
			initial_scope,
			this_scope,
			from_scope,
			vector_callback(effect_list),
			nullptr,
			nullptr
		)(node);
	}

	ret &= callback({ nullptr, std::move(effect_list), true });

	return ret;
}
