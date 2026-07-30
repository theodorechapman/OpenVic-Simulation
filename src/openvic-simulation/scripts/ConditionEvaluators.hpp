#pragma once

namespace OpenVic {
	struct ConditionNode;
	struct EvaluationContext;

	/* Evaluation functions bound to Conditions at registration time (see ConditionManager::setup_conditions).
	 * Each takes the evaluation context and the parsed node of the condition it is bound to, and returns the
	 * condition's truth value against the current game state. Evaluation never mutates game state. */
	namespace ConditionEvaluators {
		/* Fallback bound to conditions without an implemented evaluator - warns once and returns false. */
		bool unimplemented(EvaluationContext const& context, ConditionNode const& node);

		/* Logical group conditions */
		bool logical_and(EvaluationContext const& context, ConditionNode const& node);
		bool logical_or(EvaluationContext const& context, ConditionNode const& node);
		bool logical_not(EvaluationContext const& context, ConditionNode const& node);

		/* Scope-changing conditions */
		bool redirect_this(EvaluationContext const& context, ConditionNode const& node);
		bool redirect_from(EvaluationContext const& context, ConditionNode const& node);
		bool capital_scope(EvaluationContext const& context, ConditionNode const& node);
		bool province_owner(EvaluationContext const& context, ConditionNode const& node);
		bool province_controller(EvaluationContext const& context, ConditionNode const& node);
		bool pop_location(EvaluationContext const& context, ConditionNode const& node);

		/* Scope-iterating conditions */
		bool any_owned_province(EvaluationContext const& context, ConditionNode const& node);
		bool any_core(EvaluationContext const& context, ConditionNode const& node);
		bool all_core(EvaluationContext const& context, ConditionNode const& node);
		bool any_greater_power(EvaluationContext const& context, ConditionNode const& node);

		/* Global leaf conditions */
		bool always(EvaluationContext const& context, ConditionNode const& node);
		bool year(EvaluationContext const& context, ConditionNode const& node);
		bool month(EvaluationContext const& context, ConditionNode const& node);
		bool has_global_flag(EvaluationContext const& context, ConditionNode const& node);

		/* Country scope leaf conditions */
		bool tag(EvaluationContext const& context, ConditionNode const& node);
		bool exists(EvaluationContext const& context, ConditionNode const& node);
		bool owns(EvaluationContext const& context, ConditionNode const& node);
		bool war(EvaluationContext const& context, ConditionNode const& node);
		bool ai(EvaluationContext const& context, ConditionNode const& node);
		bool civilised(EvaluationContext const& context, ConditionNode const& node);
		bool is_greater_power(EvaluationContext const& context, ConditionNode const& node);
		bool is_secondary_power(EvaluationContext const& context, ConditionNode const& node);
		bool has_country_flag(EvaluationContext const& context, ConditionNode const& node);
		bool prestige(EvaluationContext const& context, ConditionNode const& node);
		bool war_with(EvaluationContext const& context, ConditionNode const& node);
		/* Family evaluator for every technology-name condition (<tech> = 1). */
		bool has_technology(EvaluationContext const& context, ConditionNode const& node);
		bool has_invention(EvaluationContext const& context, ConditionNode const& node);
		/* Country scope compares the capital's continent, province scope the province's own. */
		bool continent(EvaluationContext const& context, ConditionNode const& node);

		/* Province scope leaf conditions */
		bool terrain(EvaluationContext const& context, ConditionNode const& node);
		bool trade_goods(EvaluationContext const& context, ConditionNode const& node);
		bool life_rating(EvaluationContext const& context, ConditionNode const& node);
		bool province_id(EvaluationContext const& context, ConditionNode const& node);
		bool is_capital(EvaluationContext const& context, ConditionNode const& node);
		bool is_coastal(EvaluationContext const& context, ConditionNode const& node);
		bool port(EvaluationContext const& context, ConditionNode const& node);

		/* Pop scope leaf conditions */
		bool pop_type(EvaluationContext const& context, ConditionNode const& node);
		bool pop_strata(EvaluationContext const& context, ConditionNode const& node);
	}
}
