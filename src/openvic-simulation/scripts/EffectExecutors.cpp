#include "EffectExecutors.hpp"

#include <charconv>
#include <mutex>

#include "openvic-simulation/country/CountryDefinition.hpp"
#include "openvic-simulation/country/CountryInstance.hpp"
#include "openvic-simulation/country/CountryInstanceManager.hpp"
#include "openvic-simulation/DefinitionManager.hpp"
#include "openvic-simulation/InstanceManager.hpp"
#include "openvic-simulation/map/MapInstance.hpp"
#include "openvic-simulation/map/ProvinceDefinition.hpp"
#include "openvic-simulation/map/ProvinceInstance.hpp"
#include "openvic-simulation/misc/Event.hpp"
#include "openvic-simulation/scripts/Effect.hpp"
#include "openvic-simulation/scripts/ExecutionContext.hpp"
#include "openvic-simulation/types/OrderedContainers.hpp"

using namespace OpenVic;

/* Execute every child of a group effect against the given context, in order. Does nothing
 * if the node's value is not an effect list (a parse-layer invariant violation). */
static void _execute_children(ExecutionContext& context, EffectNode const& node) {
	EffectNode::effect_list_t const* children = std::get_if<EffectNode::effect_list_t>(&node.get_value());

	if (children == nullptr) {
		spdlog::warn_s(
			"Group effect {} has no child effect list!",
			node.get_effect() != nullptr ? node.get_effect()->get_identifier() : "<null>"
		);
		return;
	}

	for (EffectNode const& child : *children) {
		child.execute(context);
	}
}

/* Execute a scope-changing effect's children against the target scope, gated by the node's
 * limit condition if it has one. Does nothing for empty target scopes. */
static void _execute_children_in_scope(
	ExecutionContext& context, EffectNode const& node, ExecutionContext::scope_ref_t new_scope
) {
	if (std::holds_alternative<std::monostate>(new_scope)) {
		return;
	}

	ExecutionContext sub_context = context.with_current_scope(new_scope);

	if (node.does_has_limit() && !node.get_limit().evaluate(sub_context.to_evaluation_context())) {
		return;
	}

	_execute_children(sub_context, node);
}

template<typename T>
static T const* _get_value(EffectNode const& node) {
	T const* value = std::get_if<T>(&node.get_value());
	if (value == nullptr) {
		spdlog::warn_s(
			"Effect {} executed with unexpected value type!",
			node.get_effect() != nullptr ? node.get_effect()->get_identifier() : "<null>"
		);
	}
	return value;
}

void EffectExecutors::unimplemented(ExecutionContext& context, EffectNode const& node) {
	if (node.get_effect() != nullptr) {
		/* Warn once per effect rather than flooding the log every time a script runs. */
		static case_insensitive_string_set_t warned_effects;
		static std::mutex warned_effects_mutex;

		const std::lock_guard<std::mutex> lock_guard { warned_effects_mutex };
		if (warned_effects.emplace(node.get_effect()->get_identifier()).second) {
			spdlog::warn_s(
				"Effect {} has no executor implemented - it does nothing!", node.get_effect()->get_identifier()
			);
		}
	}
}

/* Scope-changing effects */

void EffectExecutors::redirect_this(ExecutionContext& context, EffectNode const& node) {
	_execute_children_in_scope(context, node, context.this_scope);
}

void EffectExecutors::redirect_from(ExecutionContext& context, EffectNode const& node) {
	_execute_children_in_scope(context, node, context.from_scope);
}

void EffectExecutors::capital_scope(ExecutionContext& context, EffectNode const& node) {
	CountryInstance* country = context.get_current_country();
	if (country == nullptr || country->get_capital() == nullptr) {
		return;
	}
	_execute_children_in_scope(context, node, country->get_capital());
}

void EffectExecutors::province_owner(ExecutionContext& context, EffectNode const& node) {
	ProvinceInstance* province = context.get_current_province();
	if (province == nullptr || province->get_owner() == nullptr) {
		return;
	}
	_execute_children_in_scope(context, node, province->get_owner());
}

void EffectExecutors::any_owned_province(ExecutionContext& context, EffectNode const& node) {
	CountryInstance* country = context.get_current_country();
	if (country == nullptr) {
		return;
	}
	/* Applies to every owned province passing the node's limit - Victoria 2's any_owned
	 * effect scope is "all owned", unlike the any_ = "at least one" condition scopes. */
	for (ProvinceInstance* province : country->get_owned_provinces()) {
		_execute_children_in_scope(context, node, province);
	}
}

/* Flag effects */

void EffectExecutors::set_global_flag(ExecutionContext& context, EffectNode const& node) {
	EffectNode::string_t const* value = _get_value<EffectNode::string_t>(node);
	if (value != nullptr && context.global_flags != nullptr) {
		context.global_flags->set_flag(*value, false);
	}
}

void EffectExecutors::clr_global_flag(ExecutionContext& context, EffectNode const& node) {
	EffectNode::string_t const* value = _get_value<EffectNode::string_t>(node);
	if (value != nullptr && context.global_flags != nullptr) {
		context.global_flags->clear_flag(*value, false);
	}
}

void EffectExecutors::set_country_flag(ExecutionContext& context, EffectNode const& node) {
	EffectNode::string_t const* value = _get_value<EffectNode::string_t>(node);
	CountryInstance* country = context.get_current_country();
	if (value != nullptr && country != nullptr) {
		country->set_flag(*value, false);
	}
}

void EffectExecutors::clr_country_flag(ExecutionContext& context, EffectNode const& node) {
	EffectNode::string_t const* value = _get_value<EffectNode::string_t>(node);
	CountryInstance* country = context.get_current_country();
	if (value != nullptr && country != nullptr) {
		country->clear_flag(*value, false);
	}
}

void EffectExecutors::set_province_flag(ExecutionContext& context, EffectNode const& node) {
	EffectNode::string_t const* value = _get_value<EffectNode::string_t>(node);
	ProvinceInstance* province = context.get_current_province();
	if (value != nullptr && province != nullptr) {
		province->set_flag(*value, false);
	}
}

void EffectExecutors::clr_province_flag(ExecutionContext& context, EffectNode const& node) {
	EffectNode::string_t const* value = _get_value<EffectNode::string_t>(node);
	ProvinceInstance* province = context.get_current_province();
	if (value != nullptr && province != nullptr) {
		province->clear_flag(*value, false);
	}
}

/* Event chain effects */

/* The immediate form (country_event = X) has an integer id and no delay; the dict form
 * (country_event = { id = X days = Y }) delays firing by Y days. */
static std::pair<EffectNode::integer_t, EffectNode::integer_t> _get_event_id_and_delay(EffectNode const& node) {
	if (EffectNode::integer_t const* event_id = std::get_if<EffectNode::integer_t>(&node.get_value())) {
		return { *event_id, 0 };
	}
	if (EffectNode::delayed_event_t const* delayed = std::get_if<EffectNode::delayed_event_t>(&node.get_value())) {
		return *delayed;
	}
	return { 0, 0 };
}

/* Event identifiers are their numeric ids as strings - format the id to look the event up.
 * Returns null (with a warning) for unknown ids. */
static Event const* _find_event(ExecutionContext const& context, EffectNode::integer_t event_id) {
	if (event_id == 0 || context.instance_manager == nullptr) {
		return nullptr;
	}

	char buffer[24] {};
	const std::to_chars_result result = std::to_chars(buffer, buffer + sizeof(buffer), event_id);
	const std::string_view event_identifier { buffer, result.ptr };

	Event const* event =
		context.instance_manager->definition_manager.get_event_manager().get_event_by_identifier(event_identifier);
	if (event == nullptr) {
		spdlog::warn_s("Event effect references unknown event id {}!", event_identifier);
	}
	return event;
}

void EffectExecutors::country_event(ExecutionContext& context, EffectNode const& node) {
	CountryInstance* country = context.get_current_country();
	const auto [event_id, delay_days] = _get_event_id_and_delay(node);
	Event const* event = _find_event(context, event_id);
	if (country == nullptr || event == nullptr) {
		return;
	}
	if (event->get_type() != Event::event_type_t::COUNTRY) {
		spdlog::warn_s("country_event effect fired non-country event {}!", event->get_identifier());
		return;
	}

	/* The receiving event's FROM is the scope that sent it. */
	EventInstanceManager& event_instance_manager = context.instance_manager->get_event_instance_manager();
	if (delay_days > 0) {
		event_instance_manager.queue_country_event(
			*event, *country, context.current_scope, context.today + Timespan { static_cast<int64_t>(delay_days) }
		);
	} else {
		event_instance_manager.fire_country_event(*event, *context.instance_manager, *country, context.current_scope);
	}
}

void EffectExecutors::province_event(ExecutionContext& context, EffectNode const& node) {
	ProvinceInstance* province = context.get_current_province();
	const auto [event_id, delay_days] = _get_event_id_and_delay(node);
	Event const* event = _find_event(context, event_id);
	if (province == nullptr || event == nullptr) {
		return;
	}
	if (event->get_type() != Event::event_type_t::PROVINCE) {
		spdlog::warn_s("province_event effect fired non-province event {}!", event->get_identifier());
		return;
	}

	EventInstanceManager& event_instance_manager = context.instance_manager->get_event_instance_manager();
	if (delay_days > 0) {
		event_instance_manager.queue_province_event(
			*event, *province, context.current_scope, context.today + Timespan { static_cast<int64_t>(delay_days) }
		);
	} else {
		event_instance_manager.fire_province_event(*event, *context.instance_manager, *province, context.current_scope);
	}
}

/* Core effects */

/* Resolve the dual-form core effect target: in province scope the node's value names the
 * country gaining/losing a core on the current province, in country scope it names the
 * province on which the current country gains/loses a core. */
static std::pair<ProvinceInstance*, CountryInstance*> _resolve_core_effect(
	ExecutionContext& context, EffectNode const& node
) {
	if (context.instance_manager == nullptr) {
		return { nullptr, nullptr };
	}

	if (ProvinceInstance* province = context.get_current_province()) {
		CountryDefinition const* country_definition =
			static_cast<CountryDefinition const*>(node.get_effect_value_item());
		if (country_definition == nullptr) {
			return { nullptr, nullptr };
		}
		return {
			province,
			&context.instance_manager->get_country_instance_manager()
				.get_country_instance_by_definition(*country_definition)
		};
	}

	if (CountryInstance* country = context.get_current_country()) {
		ProvinceDefinition const* province_definition =
			static_cast<ProvinceDefinition const*>(node.get_effect_value_item());
		if (province_definition == nullptr) {
			return { nullptr, nullptr };
		}
		return {
			context.instance_manager->get_map_instance().get_province_instance_by_index(province_definition->index),
			country
		};
	}

	return { nullptr, nullptr };
}

void EffectExecutors::add_core(ExecutionContext& context, EffectNode const& node) {
	const auto [province, country] = _resolve_core_effect(context, node);
	if (province != nullptr && country != nullptr) {
		province->add_core(*country, false);
	}
}

void EffectExecutors::remove_core(ExecutionContext& context, EffectNode const& node) {
	const auto [province, country] = _resolve_core_effect(context, node);
	if (province != nullptr && country != nullptr) {
		province->remove_core(*country, false);
	}
}
