#include <string_view>

#include <openvic-dataloader/v2script/Parser.hpp>

#include "openvic-simulation/DefinitionManager.hpp"
#include "openvic-simulation/misc/Decision.hpp"
#include "openvic-simulation/scripts/EvaluationContext.hpp"
#include "openvic-simulation/scripts/ExecutionContext.hpp"
#include "openvic-simulation/types/Date.hpp"
#include "openvic-simulation/types/FlagStrings.hpp"

#include "Helper.hpp" // IWYU pragma: keep
#include <snitch/snitch_macros_check.hpp>
#include <snitch/snitch_macros_misc.hpp>
#include <snitch/snitch_macros_test_case.hpp>

using namespace OpenVic;
using namespace std::string_view_literals;

namespace {
	struct DecisionTakingFixture {
		DefinitionManager definition_manager;
		FlagStrings global_flags { "global"sv };
		Date today {};

		DecisionTakingFixture() {
			REQUIRE(definition_manager.get_script_manager().get_condition_manager().setup_conditions(definition_manager));
			REQUIRE(definition_manager.get_script_manager().get_effect_manager().setup_effects(definition_manager));
		}

		/* Load decision script source exactly as game decision files are loaded. */
		Decision const* load_decision(std::string_view source, std::string_view decision_id) {
			ovdl::v2script::Parser parser = ovdl::v2script::Parser::from_string(source);
			REQUIRE(parser.simple_parse());
			REQUIRE_FALSE(parser.has_error());

			DecisionManager& decision_manager = definition_manager.get_decision_manager();
			REQUIRE(decision_manager.load_decision_file(parser.get_file_node()));
			decision_manager.lock_decisions();
			REQUIRE(decision_manager.parse_scripts(definition_manager));

			Decision const* decision = decision_manager.get_decision_by_identifier(decision_id);
			REQUIRE(decision != nullptr);
			return decision;
		}
	};
}

TEST_CASE("Decision potential, allow and effects", "[misc][decision-taking]") {
	DecisionTakingFixture fixture;

	Decision const* decision = fixture.load_decision(
		"political_decisions = { "
		"test_decision = { "
		"potential = { always = yes } "
		"allow = { has_global_flag = decision_prerequisite } "
		"effect = { set_global_flag = decision_taken } "
		"ai_will_do = { factor = 2 modifier = { factor = 0 has_global_flag = ai_averse } } "
		"} "
		"}"sv,
		"test_decision"sv
	);

	const EvaluationContext evaluation_context { fixture.today, &fixture.global_flags };

	CHECK(decision->check_potential(evaluation_context));
	CHECK_FALSE(decision->check_allow(evaluation_context));
	CHECK(decision->evaluate_ai_desire(evaluation_context) == fixed_point_t { 2 });

	fixture.global_flags.set_flag("decision_prerequisite"sv, false);
	CHECK(decision->check_allow(evaluation_context));

	fixture.global_flags.set_flag("ai_averse"sv, false);
	CHECK(decision->evaluate_ai_desire(evaluation_context) == fixed_point_t { 0 });

	ExecutionContext execution_context { fixture.today, &fixture.global_flags };
	decision->take_decision(execution_context);
	CHECK(fixture.global_flags.has_flag("decision_taken"sv));
}
