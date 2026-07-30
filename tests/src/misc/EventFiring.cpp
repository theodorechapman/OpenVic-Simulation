#include <string_view>

#include <openvic-dataloader/v2script/Parser.hpp>

#include "openvic-simulation/DefinitionManager.hpp"
#include "openvic-simulation/InstanceManager.hpp"
#include "openvic-simulation/misc/Event.hpp"
#include "openvic-simulation/misc/GameRulesManager.hpp"
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
	struct EventFiringFixture {
		DefinitionManager definition_manager;
		FlagStrings global_flags { "global"sv };
		Date today {};

		EventFiringFixture() {
			REQUIRE(definition_manager.get_script_manager().get_condition_manager().setup_conditions(definition_manager));
			REQUIRE(definition_manager.get_script_manager().get_effect_manager().setup_effects(definition_manager));
		}

		/* Load event script source exactly as game event files are loaded. */
		Event const* load_event(std::string_view source, std::string_view event_id) {
			ovdl::v2script::Parser parser = ovdl::v2script::Parser::from_string(source);
			REQUIRE(parser.simple_parse());
			REQUIRE_FALSE(parser.has_error());

			EventManager& event_manager = definition_manager.get_event_manager();
			REQUIRE(event_manager.load_event_file(
				definition_manager.get_politics_manager().get_issue_manager(), parser.get_file_node()
			));
			event_manager.lock_events();
			REQUIRE(event_manager.parse_scripts(definition_manager));

			Event const* event = event_manager.get_event_by_identifier(event_id);
			REQUIRE(event != nullptr);
			return event;
		}
	};
}

TEST_CASE("Event fires its immediate and chosen option effects", "[misc][event-firing]") {
	EventFiringFixture fixture;

	Event const* event = fixture.load_event(
		"country_event = { "
		"id = 100000 title = \"TEST_TITLE\" desc = \"TEST_DESC\" "
		"trigger = { always = yes } "
		"mean_time_to_happen = { days = 1 } "
		"immediate = { set_global_flag = immediate_ran set_global_flag = execution_order } "
		"option = { name = \"TEST_OPTION\" set_global_flag = option_ran clr_global_flag = execution_order } "
		"}"sv,
		"100000"sv
	);

	const EvaluationContext evaluation_context { fixture.today, &fixture.global_flags };
	CHECK(event->check_trigger(evaluation_context));
	CHECK(event->calculate_daily_fire_chance(evaluation_context) == fixed_point_t { 1 });

	ExecutionContext execution_context { fixture.today, &fixture.global_flags };
	event->fire(execution_context, event->choose_ai_option(evaluation_context, fixed_point_t { 0 }));

	CHECK(fixture.global_flags.has_flag("immediate_ran"sv));
	CHECK(fixture.global_flags.has_flag("option_ran"sv));
	/* The option's clr must run after the immediate's set. */
	CHECK_FALSE(fixture.global_flags.has_flag("execution_order"sv));
}

TEST_CASE("Event AI option choice picks the highest ai_chance", "[misc][event-firing]") {
	EventFiringFixture fixture;

	Event const* event = fixture.load_event(
		"country_event = { "
		"id = 100001 title = \"TEST_TITLE\" desc = \"TEST_DESC\" "
		"trigger = { always = yes } "
		"option = { name = \"BAD\" ai_chance = { factor = 1 } set_global_flag = chose_bad } "
		"option = { name = \"GOOD\" ai_chance = { factor = 5 } set_global_flag = chose_good } "
		"option = { name = \"WORSE\" ai_chance = { factor = 2 } set_global_flag = chose_worse } "
		"}"sv,
		"100001"sv
	);

	const EvaluationContext evaluation_context { fixture.today, &fixture.global_flags };

	/* ai_chance weights 1/5/2 (total 8) partition [0,1) into [0,1/8) -> BAD,
	 * [1/8,6/8) -> GOOD, [6/8,1) -> WORSE - matching Victoria 2's weighted random roll. */
	CHECK(event->choose_ai_option(evaluation_context, fixed_point_t { 0 }) == 0);
	CHECK(event->choose_ai_option(evaluation_context, fixed_point_t { 1 } / 4) == 1);
	CHECK(event->choose_ai_option(evaluation_context, fixed_point_t { 9 } / 10) == 2);

	const size_t chosen = event->choose_ai_option(evaluation_context, fixed_point_t { 1 } / 4);
	ExecutionContext execution_context { fixture.today, &fixture.global_flags };
	event->fire(execution_context, chosen);

	CHECK(fixture.global_flags.has_flag("chose_good"sv));
	CHECK_FALSE(fixture.global_flags.has_flag("chose_bad"sv));
	CHECK_FALSE(fixture.global_flags.has_flag("chose_worse"sv));
}

TEST_CASE("Event trigger and MTTH evaluation", "[misc][event-firing]") {
	EventFiringFixture fixture;

	Event const* event = fixture.load_event(
		"country_event = { "
		"id = 100002 title = \"TEST_TITLE\" desc = \"TEST_DESC\" "
		"trigger = { always = no } "
		"mean_time_to_happen = { days = 100 modifier = { factor = 0.5 always = yes } } "
		"option = { name = \"TEST_OPTION\" } "
		"}"sv,
		"100002"sv
	);

	const EvaluationContext evaluation_context { fixture.today, &fixture.global_flags };
	CHECK_FALSE(event->check_trigger(evaluation_context));
	/* MTTH 100 days halved by its modifier: daily chance = 1/50. */
	CHECK(event->calculate_daily_fire_chance(evaluation_context) == fixed_point_t { 1 } / 50);
}

TEST_CASE("InstanceManager constructs and ticks events over an empty world", "[misc][event-firing]") {
	EventFiringFixture fixture;

	fixture.load_event(
		"country_event = { "
		"id = 100003 title = \"TEST_TITLE\" desc = \"TEST_DESC\" "
		"trigger = { always = yes } "
		"option = { name = \"TEST_OPTION\" set_global_flag = fired_without_country } "
		"}"sv,
		"100003"sv
	);

	/* Instance managers assert their definition registries are locked, as they would be
	 * after data loading - lock the (empty) registries the same way. */
	fixture.definition_manager.get_economy_manager().get_good_definition_manager().lock_good_definitions();
	fixture.definition_manager.get_map_definition().lock_province_definitions();
	fixture.definition_manager.get_country_definition_manager().lock_country_definitions();

	GameRulesManager game_rules_manager;
	InstanceManager instance_manager { game_rules_manager, fixture.definition_manager, []() {} };

	/* No countries or provinces exist, so nothing can fire - but nothing must crash either. */
	instance_manager.get_event_instance_manager().events_tick(instance_manager);

	CHECK_FALSE(instance_manager.get_global_flags().has_flag("fired_without_country"sv));
}
