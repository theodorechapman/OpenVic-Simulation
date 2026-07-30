#include <string_view>

#include <openvic-dataloader/v2script/Parser.hpp>

#include "openvic-simulation/DefinitionManager.hpp"
#include "openvic-simulation/scripts/EffectScript.hpp"
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
	/* Minimal game state for executing effects that don't require loaded game data:
	 * empty definitions, no countries or provinces, only global flags to mutate. */
	struct EffectExecutionFixture {
		DefinitionManager definition_manager;
		FlagStrings global_flags { "global"sv };
		Date today {};

		EffectExecutionFixture() {
			REQUIRE(definition_manager.get_script_manager().get_condition_manager().setup_conditions(definition_manager));
			REQUIRE(definition_manager.get_script_manager().get_effect_manager().setup_effects(definition_manager));
		}

		/* Parse effect script source text exactly as game files are parsed. */
		EffectScript parse(std::string_view source, bool expect_parse_success = true) {
			ovdl::v2script::Parser parser = ovdl::v2script::Parser::from_string(source);
			REQUIRE(parser.simple_parse());
			REQUIRE_FALSE(parser.has_error());

			EffectScript script {};
			script.expect_script()(parser.get_file_node());
			CHECK(script.parse_script(false, definition_manager) == expect_parse_success);
			return script;
		}

		void execute(std::string_view source) {
			EffectScript script = parse(source);
			ExecutionContext context { today, &global_flags };
			script.execute(context);
		}
	};
}

TEST_CASE("EffectScript global flag effects", "[scripts][effect-execution]") {
	EffectExecutionFixture fixture;

	CHECK_FALSE(fixture.global_flags.has_flag("openvic_test_flag"sv));

	fixture.execute("set_global_flag = openvic_test_flag"sv);
	CHECK(fixture.global_flags.has_flag("openvic_test_flag"sv));

	fixture.execute("clr_global_flag = openvic_test_flag"sv);
	CHECK_FALSE(fixture.global_flags.has_flag("openvic_test_flag"sv));
}

TEST_CASE("EffectScript effects execute in script order", "[scripts][effect-execution]") {
	EffectExecutionFixture fixture;

	fixture.execute("set_global_flag = ordered_flag clr_global_flag = ordered_flag"sv);
	CHECK_FALSE(fixture.global_flags.has_flag("ordered_flag"sv));

	fixture.execute("clr_global_flag = ordered_flag set_global_flag = ordered_flag"sv);
	CHECK(fixture.global_flags.has_flag("ordered_flag"sv));
}

TEST_CASE("EffectScript unknown effects are skipped without failing the script", "[scripts][effect-execution]") {
	EffectExecutionFixture fixture;

	/* annex_to is a real Victoria 2 effect not yet registered - the script must still
	 * load and the known effect after it must still run. */
	fixture.execute("annex_to = ENG set_global_flag = after_unknown"sv);
	CHECK(fixture.global_flags.has_flag("after_unknown"sv));
}

TEST_CASE("EffectScript scope effects gate their children on the scope existing", "[scripts][effect-execution]") {
	EffectExecutionFixture fixture;

	/* this_scope is empty in this context, so the redirect never executes its children -
	 * even though set_global_flag itself needs no scope. */
	fixture.execute("THIS = { set_global_flag = inside_this }"sv);
	CHECK_FALSE(fixture.global_flags.has_flag("inside_this"sv));
}

TEST_CASE("EffectScript limit blocks parse inside scope effects", "[scripts][effect-execution]") {
	EffectExecutionFixture fixture;

	const EffectScript script = fixture.parse(
		"THIS = { limit = { always = no } set_global_flag = limited }"sv
	);

	EffectNode::effect_list_t const* children =
		std::get_if<EffectNode::effect_list_t>(&script.get_effect_root().get_value());
	REQUIRE(children != nullptr);
	REQUIRE(children->size() == 1);
	CHECK(children->front().does_has_limit());
}

TEST_CASE("EffectScript top-level limit blocks fail to parse", "[scripts][effect-execution]") {
	EffectExecutionFixture fixture;

	fixture.parse("limit = { always = yes } set_global_flag = x"sv, false);
}

TEST_CASE("EffectScript unparsed script executes without effect", "[scripts][effect-execution]") {
	EffectExecutionFixture fixture;

	const EffectScript script {};
	ExecutionContext context { fixture.today, &fixture.global_flags };
	script.execute(context);
	CHECK(fixture.global_flags.get_flags().empty());
}
