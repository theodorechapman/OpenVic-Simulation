#include <string_view>

#include <openvic-dataloader/v2script/Parser.hpp>

#include "openvic-simulation/DefinitionManager.hpp"
#include "openvic-simulation/scripts/ConditionScript.hpp"
#include "openvic-simulation/scripts/EvaluationContext.hpp"
#include "openvic-simulation/types/Date.hpp"
#include "openvic-simulation/types/FlagStrings.hpp"

#include "Helper.hpp" // IWYU pragma: keep
#include <snitch/snitch_macros_check.hpp>
#include <snitch/snitch_macros_misc.hpp>
#include <snitch/snitch_macros_test_case.hpp>

using namespace OpenVic;
using namespace std::string_view_literals;

namespace {
	/* Minimal state for evaluating conditions that don't require loaded game data:
	 * empty definitions, no countries or provinces, today == Date {} (1st January, year 0). */
	struct ConditionEvaluationFixture {
		DefinitionManager definition_manager;
		FlagStrings global_flags { "global"sv };
		Date today {};

		ConditionEvaluationFixture() {
			REQUIRE(definition_manager.get_script_manager().get_condition_manager().setup_conditions(definition_manager));
		}

		/* Parse condition script source text exactly as game files are parsed. */
		ConditionScript parse(std::string_view source) {
			ovdl::v2script::Parser parser = ovdl::v2script::Parser::from_string(source);
			REQUIRE(parser.simple_parse());
			REQUIRE_FALSE(parser.has_error());

			ConditionScript script { scope_type_t::COUNTRY, scope_type_t::COUNTRY, scope_type_t::NO_SCOPE };
			script.expect_script()(parser.get_file_node());
			REQUIRE(script.parse_script(false, definition_manager));
			return script;
		}

		bool evaluate(std::string_view source, EvaluationContext::scope_ref_t current_scope = {}) {
			const ConditionScript script = parse(source);
			const EvaluationContext context { today, &global_flags, current_scope };
			return script.evaluate(context);
		}
	};
}

TEST_CASE("ConditionScript boolean leaf evaluation", "[scripts][condition-evaluation]") {
	ConditionEvaluationFixture fixture;

	CHECK(fixture.evaluate("always = yes"sv));
	CHECK_FALSE(fixture.evaluate("always = no"sv));
}

TEST_CASE("ConditionScript logical operators", "[scripts][condition-evaluation]") {
	ConditionEvaluationFixture fixture;

	CHECK(fixture.evaluate("AND = { always = yes always = yes }"sv));
	CHECK_FALSE(fixture.evaluate("AND = { always = yes always = no }"sv));

	CHECK(fixture.evaluate("OR = { always = no always = yes }"sv));
	CHECK_FALSE(fixture.evaluate("OR = { always = no always = no }"sv));

	CHECK(fixture.evaluate("NOT = { always = no }"sv));
	CHECK_FALSE(fixture.evaluate("NOT = { always = yes }"sv));
	/* Victoria 2's NOT is NOR over its children. */
	CHECK_FALSE(fixture.evaluate("NOT = { always = no always = yes }"sv));

	/* Implicit AND across multiple root-level conditions. */
	CHECK_FALSE(fixture.evaluate("always = yes\nalways = no"sv));
	CHECK(fixture.evaluate("NOT = { always = no }\nOR = { always = yes }"sv));
}

TEST_CASE("ConditionScript date conditions", "[scripts][condition-evaluation]") {
	ConditionEvaluationFixture fixture;

	/* today is Date {} - year 0, script-month 0 (script months are 0-based). */
	CHECK(fixture.evaluate("year = 0"sv));
	CHECK_FALSE(fixture.evaluate("year = 1836"sv));
	CHECK(fixture.evaluate("month = 0"sv));
	CHECK_FALSE(fixture.evaluate("month = 5"sv));

	fixture.today = Date { 1840, 7, 1 };

	CHECK(fixture.evaluate("year = 1836"sv));
	CHECK_FALSE(fixture.evaluate("year = 1841"sv));
	CHECK(fixture.evaluate("month = 5"sv));
	CHECK_FALSE(fixture.evaluate("month = 7"sv));
}

TEST_CASE("ConditionScript global flags", "[scripts][condition-evaluation]") {
	ConditionEvaluationFixture fixture;

	CHECK_FALSE(fixture.evaluate("has_global_flag = openvic_test_flag"sv));

	fixture.global_flags.set_flag("openvic_test_flag"sv, false);

	CHECK(fixture.evaluate("has_global_flag = openvic_test_flag"sv));
	CHECK_FALSE(fixture.evaluate("has_global_flag = some_other_flag"sv));
	CHECK_FALSE(fixture.evaluate("NOT = { has_global_flag = openvic_test_flag }"sv));
}

TEST_CASE("ConditionScript scope redirection", "[scripts][condition-evaluation]") {
	ConditionEvaluationFixture fixture;

	/* THIS redirects to the context's this_scope, which is empty here - even
	 * an always-true child must fail because there is no scope to test it against. */
	CHECK_FALSE(fixture.evaluate("THIS = { always = yes }"sv));

	/* Country leaf conditions evaluated without a current country scope are false. */
	CHECK_FALSE(fixture.evaluate("war = no"sv));
	CHECK_FALSE(fixture.evaluate("civilized = no"sv));
	CHECK_FALSE(fixture.evaluate("is_greater_power = no"sv));
}

TEST_CASE("ConditionScript unimplemented conditions evaluate to false", "[scripts][condition-evaluation]") {
	ConditionEvaluationFixture fixture;

	CHECK_FALSE(fixture.evaluate("is_canal_enabled = 1"sv));
	/* ...even when wrapped so the surrounding logic still works. */
	CHECK(fixture.evaluate("NOT = { is_canal_enabled = 1 }"sv));
}

TEST_CASE("ConditionScript unparsed script evaluates to false", "[scripts][condition-evaluation]") {
	ConditionEvaluationFixture fixture;

	const ConditionScript script { scope_type_t::COUNTRY, scope_type_t::COUNTRY, scope_type_t::NO_SCOPE };
	const EvaluationContext context { fixture.today, &fixture.global_flags };
	CHECK_FALSE(script.evaluate(context));
}
