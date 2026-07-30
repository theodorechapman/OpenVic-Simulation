#include "ConditionEvaluators.hpp"

#include <mutex>

#include "openvic-simulation/country/CountryDefinition.hpp"
#include "openvic-simulation/country/CountryInstance.hpp"
#include "openvic-simulation/country/CountryInstanceManager.hpp"
#include "openvic-simulation/country/CountryParty.hpp"
#include "openvic-simulation/economy/GoodDefinition.hpp"
#include "openvic-simulation/InstanceManager.hpp"
#include "openvic-simulation/map/MapInstance.hpp"
#include "openvic-simulation/map/ProvinceDefinition.hpp"
#include "openvic-simulation/map/ProvinceInstance.hpp"
#include "openvic-simulation/map/Region.hpp"
#include "openvic-simulation/map/TerrainType.hpp"
#include "openvic-simulation/politics/Ideology.hpp"
#include "openvic-simulation/politics/PartyPolicy.hpp"
#include "openvic-simulation/politics/Reform.hpp"
#include "openvic-simulation/population/Pop.hpp"
#include "openvic-simulation/population/PopType.hpp"
#include "openvic-simulation/research/Invention.hpp"
#include "openvic-simulation/research/Technology.hpp"
#include "openvic-simulation/scripts/Condition.hpp"
#include "openvic-simulation/scripts/EvaluationContext.hpp"
#include "openvic-simulation/types/OrderedContainers.hpp"

using namespace OpenVic;

/* Evaluate every child of a group condition against the given context, returning true if all pass.
 * Returns fail_value if the node's value is not a condition list (a parse-layer invariant violation). */
static bool _evaluate_children(EvaluationContext const& context, ConditionNode const& node) {
	ConditionNode::condition_list_t const* children =
		std::get_if<ConditionNode::condition_list_t>(&node.get_value());

	if (children == nullptr) {
		spdlog::warn_s(
			"Group condition {} has no child condition list!",
			node.get_condition() != nullptr ? node.get_condition()->get_identifier() : "<null>"
		);
		return false;
	}

	for (ConditionNode const& child : *children) {
		if (!child.evaluate(context)) {
			return false;
		}
	}
	return true;
}

static bool _evaluate_children_in_scope(
	EvaluationContext const& context, ConditionNode const& node, EvaluationContext::scope_ref_t new_scope
) {
	if (std::holds_alternative<std::monostate>(new_scope)) {
		return false;
	}
	return _evaluate_children(context.with_current_scope(new_scope), node);
}

template<typename T>
static T const* _get_value(ConditionNode const& node) {
	T const* value = std::get_if<T>(&node.get_value());
	if (value == nullptr) {
		spdlog::warn_s(
			"Condition {} evaluated with unexpected value type!",
			node.get_condition() != nullptr ? node.get_condition()->get_identifier() : "<null>"
		);
	}
	return value;
}

bool ConditionEvaluators::unimplemented(EvaluationContext const& context, ConditionNode const& node) {
	if (node.get_condition() != nullptr) {
		/* Warn once per condition rather than flooding the log on every daily evaluation. */
		static case_insensitive_string_set_t warned_conditions;
		static std::mutex warned_conditions_mutex;

		const std::lock_guard<std::mutex> lock_guard { warned_conditions_mutex };
		if (warned_conditions.emplace(node.get_condition()->get_identifier()).second) {
			spdlog::warn_s(
				"Condition {} has no evaluator implemented - evaluating to false!",
				node.get_condition()->get_identifier()
			);
		}
	}
	return false;
}

/* Logical group conditions */

bool ConditionEvaluators::logical_and(EvaluationContext const& context, ConditionNode const& node) {
	return _evaluate_children(context, node);
}

bool ConditionEvaluators::logical_or(EvaluationContext const& context, ConditionNode const& node) {
	ConditionNode::condition_list_t const* children = _get_value<ConditionNode::condition_list_t>(node);
	if (children == nullptr) {
		return false;
	}
	for (ConditionNode const& child : *children) {
		if (child.evaluate(context)) {
			return true;
		}
	}
	return false;
}

bool ConditionEvaluators::logical_not(EvaluationContext const& context, ConditionNode const& node) {
	ConditionNode::condition_list_t const* children = _get_value<ConditionNode::condition_list_t>(node);
	if (children == nullptr) {
		return false;
	}
	/* NOT is true only if no child is true, matching Victoria 2's NOT-as-NOR behaviour. */
	for (ConditionNode const& child : *children) {
		if (child.evaluate(context)) {
			return false;
		}
	}
	return true;
}

/* Scope-changing conditions */

bool ConditionEvaluators::redirect_this(EvaluationContext const& context, ConditionNode const& node) {
	return _evaluate_children_in_scope(context, node, context.this_scope);
}

bool ConditionEvaluators::redirect_from(EvaluationContext const& context, ConditionNode const& node) {
	return _evaluate_children_in_scope(context, node, context.from_scope);
}

bool ConditionEvaluators::capital_scope(EvaluationContext const& context, ConditionNode const& node) {
	CountryInstance const* country = context.get_current_country();
	if (country == nullptr || country->get_capital() == nullptr) {
		return false;
	}
	return _evaluate_children_in_scope(context, node, country->get_capital());
}

bool ConditionEvaluators::province_owner(EvaluationContext const& context, ConditionNode const& node) {
	ProvinceInstance const* province = context.get_current_province();
	if (province == nullptr || province->get_owner() == nullptr) {
		return false;
	}
	return _evaluate_children_in_scope(context, node, province->get_owner());
}

bool ConditionEvaluators::province_controller(EvaluationContext const& context, ConditionNode const& node) {
	ProvinceInstance const* province = context.get_current_province();
	if (province == nullptr || province->get_controller() == nullptr) {
		return false;
	}
	return _evaluate_children_in_scope(context, node, province->get_controller());
}

bool ConditionEvaluators::pop_location(EvaluationContext const& context, ConditionNode const& node) {
	Pop const* pop = context.get_current_pop();
	if (pop == nullptr) {
		return false;
	}
	ProvinceInstance const& location = pop->get_location();
	return _evaluate_children_in_scope(context, node, &location);
}

/* Scope-iterating conditions */

bool ConditionEvaluators::any_owned_province(EvaluationContext const& context, ConditionNode const& node) {
	CountryInstance const* country = context.get_current_country();
	if (country == nullptr) {
		return false;
	}
	for (ProvinceInstance const* province : country->get_owned_provinces()) {
		if (_evaluate_children_in_scope(context, node, province)) {
			return true;
		}
	}
	return false;
}

bool ConditionEvaluators::any_core(EvaluationContext const& context, ConditionNode const& node) {
	CountryInstance const* country = context.get_current_country();
	if (country == nullptr) {
		return false;
	}
	for (ProvinceInstance const* province : country->get_core_provinces()) {
		if (_evaluate_children_in_scope(context, node, province)) {
			return true;
		}
	}
	return false;
}

bool ConditionEvaluators::all_core(EvaluationContext const& context, ConditionNode const& node) {
	CountryInstance const* country = context.get_current_country();
	if (country == nullptr) {
		return false;
	}
	/* Vacuously true for countries with no cores, consistent with universal quantification. */
	for (ProvinceInstance const* province : country->get_core_provinces()) {
		if (!_evaluate_children_in_scope(context, node, province)) {
			return false;
		}
	}
	return true;
}

bool ConditionEvaluators::any_greater_power(EvaluationContext const& context, ConditionNode const& node) {
	if (context.instance_manager == nullptr) {
		return false;
	}
	for (CountryInstance const& great_power :
		context.instance_manager->get_country_instance_manager().get_great_powers()) {
		if (_evaluate_children_in_scope(context, node, &great_power)) {
			return true;
		}
	}
	return false;
}

/* Global leaf conditions */

bool ConditionEvaluators::always(EvaluationContext const& context, ConditionNode const& node) {
	ConditionNode::boolean_t const* value = _get_value<ConditionNode::boolean_t>(node);
	return value != nullptr && *value;
}

bool ConditionEvaluators::year(EvaluationContext const& context, ConditionNode const& node) {
	ConditionNode::integer_t const* value = _get_value<ConditionNode::integer_t>(node);
	return value != nullptr && context.today.get_year() >= *value;
}

bool ConditionEvaluators::month(EvaluationContext const& context, ConditionNode const& node) {
	ConditionNode::integer_t const* value = _get_value<ConditionNode::integer_t>(node);
	/* Victoria 2 script months are 0-based (0 = January), Date months are 1-based. */
	return value != nullptr && static_cast<ConditionNode::integer_t>(context.today.get_month() - 1) >= *value;
}

bool ConditionEvaluators::has_global_flag(EvaluationContext const& context, ConditionNode const& node) {
	ConditionNode::string_t const* value = _get_value<ConditionNode::string_t>(node);
	return value != nullptr && context.global_flags != nullptr && context.global_flags->has_flag(*value);
}

/* Country scope leaf conditions */

/* The parse layer stores pre-resolved definition pointers as HasIdentifier const*, so evaluators
 * downcast to the definition type matching the condition's registered value identifier type. */

bool ConditionEvaluators::tag(EvaluationContext const& context, ConditionNode const& node) {
	CountryInstance const* country = context.get_current_country();
	if (country == nullptr) {
		return false;
	}

	CountryDefinition const* target =
		static_cast<CountryDefinition const*>(node.get_condition_value_item());
	if (target != nullptr) {
		return &country->country_definition == target;
	}

	/* Unresolved identifiers may be the special values THIS or FROM (case-insensitive). */
	ConditionNode::string_t const* value = _get_value<ConditionNode::string_t>(node);
	if (value == nullptr) {
		return false;
	}
	static constexpr case_insensitive_string_equal string_equals_ci {};
	if (string_equals_ci(*value, "THIS")) {
		CountryInstance const* const* this_country = std::get_if<CountryInstance const*>(&context.this_scope);
		return this_country != nullptr && *this_country == country;
	}
	if (string_equals_ci(*value, "FROM")) {
		CountryInstance const* const* from_country = std::get_if<CountryInstance const*>(&context.from_scope);
		return from_country != nullptr && *from_country == country;
	}
	return false;
}

bool ConditionEvaluators::exists(EvaluationContext const& context, ConditionNode const& node) {
	/* Boolean form: does the currently scoped country exist? */
	if (ConditionNode::boolean_t const* value = std::get_if<ConditionNode::boolean_t>(&node.get_value())) {
		CountryInstance const* country = context.get_current_country();
		return country != nullptr && country->exists() == *value;
	}

	/* Identifier form: does the named country exist? */
	CountryDefinition const* target =
		static_cast<CountryDefinition const*>(node.get_condition_value_item());
	if (target == nullptr || context.instance_manager == nullptr) {
		return false;
	}
	return context.instance_manager->get_country_instance_manager()
		.get_country_instance_by_definition(*target).exists();
}

bool ConditionEvaluators::owns(EvaluationContext const& context, ConditionNode const& node) {
	CountryInstance const* country = context.get_current_country();
	if (country == nullptr) {
		return false;
	}
	ProvinceDefinition const* target =
		static_cast<ProvinceDefinition const*>(node.get_condition_value_item());
	if (target == nullptr || context.instance_manager == nullptr) {
		return false;
	}
	ProvinceInstance const* province =
		context.instance_manager->get_map_instance().get_province_instance_by_index(target->index);
	return province != nullptr && province->get_owner() == country;
}

bool ConditionEvaluators::war(EvaluationContext const& context, ConditionNode const& node) {
	ConditionNode::boolean_t const* value = _get_value<ConditionNode::boolean_t>(node);
	CountryInstance const* country = context.get_current_country();
	return value != nullptr && country != nullptr && country->is_at_war() == *value;
}

bool ConditionEvaluators::ai(EvaluationContext const& context, ConditionNode const& node) {
	ConditionNode::boolean_t const* value = _get_value<ConditionNode::boolean_t>(node);
	CountryInstance const* country = context.get_current_country();
	return value != nullptr && country != nullptr && country->is_ai() == *value;
}

bool ConditionEvaluators::civilised(EvaluationContext const& context, ConditionNode const& node) {
	ConditionNode::boolean_t const* value = _get_value<ConditionNode::boolean_t>(node);
	CountryInstance const* country = context.get_current_country();
	return value != nullptr && country != nullptr && country->is_civilised() == *value;
}

bool ConditionEvaluators::is_greater_power(EvaluationContext const& context, ConditionNode const& node) {
	ConditionNode::boolean_t const* value = _get_value<ConditionNode::boolean_t>(node);
	CountryInstance const* country = context.get_current_country();
	return value != nullptr && country != nullptr && country->is_great_power() == *value;
}

bool ConditionEvaluators::is_secondary_power(EvaluationContext const& context, ConditionNode const& node) {
	ConditionNode::boolean_t const* value = _get_value<ConditionNode::boolean_t>(node);
	CountryInstance const* country = context.get_current_country();
	return value != nullptr && country != nullptr && country->is_secondary_power() == *value;
}

bool ConditionEvaluators::has_country_flag(EvaluationContext const& context, ConditionNode const& node) {
	ConditionNode::string_t const* value = _get_value<ConditionNode::string_t>(node);
	CountryInstance const* country = context.get_current_country();
	return value != nullptr && country != nullptr && country->has_flag(*value);
}

bool ConditionEvaluators::prestige(EvaluationContext const& context, ConditionNode const& node) {
	ConditionNode::real_t const* value = _get_value<ConditionNode::real_t>(node);
	CountryInstance const* country = context.get_current_country();
	return value != nullptr && country != nullptr && country->get_prestige_untracked() >= *value;
}

bool ConditionEvaluators::war_with(EvaluationContext const& context, ConditionNode const& node) {
	CountryInstance const* country = context.get_current_country();
	CountryDefinition const* target =
		static_cast<CountryDefinition const*>(node.get_condition_value_item());
	if (country == nullptr || target == nullptr || context.instance_manager == nullptr) {
		return false;
	}
	return country->is_at_war_with(
		context.instance_manager->get_country_instance_manager().get_country_instance_by_definition(*target)
	);
}

bool ConditionEvaluators::has_technology(EvaluationContext const& context, ConditionNode const& node) {
	ConditionNode::boolean_t const* value = _get_value<ConditionNode::boolean_t>(node);
	CountryInstance const* country = context.get_current_country();
	Technology const* technology = static_cast<Technology const*>(node.get_condition_key_item());
	if (value == nullptr || country == nullptr || technology == nullptr) {
		return false;
	}
	/* Victoria 2: '<technology> = 1' passes once the technology is researched. */
	return country->is_technology_unlocked(*technology) == *value;
}

bool ConditionEvaluators::has_invention(EvaluationContext const& context, ConditionNode const& node) {
	CountryInstance const* country = context.get_current_country();
	Invention const* invention = static_cast<Invention const*>(node.get_condition_value_item());
	return country != nullptr && invention != nullptr && country->is_invention_unlocked(*invention);
}

bool ConditionEvaluators::active_reform(EvaluationContext const& context, ConditionNode const& node) {
	CountryInstance const* country = context.get_current_country();
	Reform const* reform = static_cast<Reform const*>(node.get_condition_value_item());
	if (country == nullptr || reform == nullptr) {
		return false;
	}
	return country->get_reforms().at(reform->group) == reform;
}

bool ConditionEvaluators::ruling_party_policy(EvaluationContext const& context, ConditionNode const& node) {
	CountryInstance const* country = context.get_current_country();
	PartyPolicy const* policy = static_cast<PartyPolicy const*>(node.get_condition_value_item());
	if (country == nullptr || policy == nullptr) {
		return false;
	}
	CountryParty const* ruling_party = country->get_ruling_party_untracked();
	return ruling_party != nullptr && ruling_party->get_policies()[policy->group.index] == policy;
}

/* National support fraction for an issue/ideology: pop supporter equivalents over total
 * population. TODO - verify against Victoria 2 whether the base is total or adult population,
 * and note upstream currently fills pop support with placeholder values. */
template<typename Key>
static bool _support_at_least(
	CountryInstance const* country, Key const* key, ConditionNode::real_t const* value,
	auto get_supporter_equivalents
) {
	if (country == nullptr || key == nullptr || value == nullptr) {
		return false;
	}
	/* parse_raw shift instead of the int constructor, which is capped at 4-byte inputs. */
	const fixed_point_t total_population = fixed_point_t::parse_raw(
		static_cast<fixed_point_t::value_type>(type_safe::get(country->get_total_population())) << fixed_point_t::PRECISION
	);
	if (total_population <= 0) {
		return false;
	}
	const fixed_point_t supporters = get_supporter_equivalents(*country)[key->index];
	/* supporters / total >= value, rearranged to avoid division. */
	return supporters >= *value * total_population;
}

bool ConditionEvaluators::reform_support(EvaluationContext const& context, ConditionNode const& node) {
	return _support_at_least(
		context.get_current_country(), static_cast<Reform const*>(node.get_condition_key_item()),
		_get_value<ConditionNode::real_t>(node),
		[](CountryInstance const& country) { return country.get_supporter_equivalents_by_reform(); }
	);
}

bool ConditionEvaluators::party_policy_support(EvaluationContext const& context, ConditionNode const& node) {
	return _support_at_least(
		context.get_current_country(), static_cast<PartyPolicy const*>(node.get_condition_key_item()),
		_get_value<ConditionNode::real_t>(node),
		[](CountryInstance const& country) { return country.get_supporter_equivalents_by_party_policy(); }
	);
}

bool ConditionEvaluators::ideology_support(EvaluationContext const& context, ConditionNode const& node) {
	return _support_at_least(
		context.get_current_country(), static_cast<Ideology const*>(node.get_condition_key_item()),
		_get_value<ConditionNode::real_t>(node),
		[](CountryInstance const& country) { return country.get_supporter_equivalents_by_ideology(); }
	);
}

bool ConditionEvaluators::continent(EvaluationContext const& context, ConditionNode const& node) {
	Continent const* target = static_cast<Continent const*>(node.get_condition_value_item());
	if (target == nullptr) {
		return false;
	}

	if (ProvinceInstance const* province = context.get_current_province()) {
		return province->province_definition.get_continent() == target;
	}
	if (CountryInstance const* country = context.get_current_country()) {
		return country->get_capital() != nullptr &&
			country->get_capital()->province_definition.get_continent() == target;
	}
	return false;
}

/* Province scope leaf conditions */

bool ConditionEvaluators::terrain(EvaluationContext const& context, ConditionNode const& node) {
	ProvinceInstance const* province = context.get_current_province();
	return province != nullptr && node.get_condition_value_item() != nullptr &&
		province->get_terrain_type() == static_cast<TerrainType const*>(node.get_condition_value_item());
}

bool ConditionEvaluators::trade_goods(EvaluationContext const& context, ConditionNode const& node) {
	ProvinceInstance const* province = context.get_current_province();
	return province != nullptr && node.get_condition_value_item() != nullptr &&
		province->get_rgo_good() == static_cast<GoodDefinition const*>(node.get_condition_value_item());
}

bool ConditionEvaluators::life_rating(EvaluationContext const& context, ConditionNode const& node) {
	ConditionNode::real_t const* value = _get_value<ConditionNode::real_t>(node);
	ProvinceInstance const* province = context.get_current_province();
	return value != nullptr && province != nullptr &&
		fixed_point_t { type_safe::get(province->get_life_rating()) } >= *value;
}

bool ConditionEvaluators::province_id(EvaluationContext const& context, ConditionNode const& node) {
	ProvinceInstance const* province = context.get_current_province();
	return province != nullptr &&
		&province->province_definition == static_cast<ProvinceDefinition const*>(node.get_condition_value_item());
}

bool ConditionEvaluators::is_capital(EvaluationContext const& context, ConditionNode const& node) {
	ConditionNode::boolean_t const* value = _get_value<ConditionNode::boolean_t>(node);
	ProvinceInstance const* province = context.get_current_province();
	if (value == nullptr || province == nullptr) {
		return false;
	}
	const bool capital = province->get_owner() != nullptr && province->get_owner()->get_capital() == province;
	return capital == *value;
}

bool ConditionEvaluators::is_coastal(EvaluationContext const& context, ConditionNode const& node) {
	ConditionNode::boolean_t const* value = _get_value<ConditionNode::boolean_t>(node);
	ProvinceInstance const* province = context.get_current_province();
	return value != nullptr && province != nullptr && province->province_definition.is_coastal() == *value;
}

bool ConditionEvaluators::port(EvaluationContext const& context, ConditionNode const& node) {
	ConditionNode::boolean_t const* value = _get_value<ConditionNode::boolean_t>(node);
	ProvinceInstance const* province = context.get_current_province();
	return value != nullptr && province != nullptr && province->province_definition.has_port() == *value;
}

/* Pop scope leaf conditions */

bool ConditionEvaluators::pop_type(EvaluationContext const& context, ConditionNode const& node) {
	Pop const* pop = context.get_current_pop();
	if (pop == nullptr || node.get_condition_value_item() == nullptr) {
		return false;
	}
	PopType const& type = pop->get_type();
	return &type == static_cast<PopType const*>(node.get_condition_value_item());
}

bool ConditionEvaluators::pop_strata(EvaluationContext const& context, ConditionNode const& node) {
	Pop const* pop = context.get_current_pop();
	if (pop == nullptr || node.get_condition_value_item() == nullptr) {
		return false;
	}
	PopType const& type = pop->get_type();
	return &type.strata == static_cast<Strata const*>(node.get_condition_value_item());
}
