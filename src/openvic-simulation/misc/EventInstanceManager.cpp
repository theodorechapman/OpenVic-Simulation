#include "EventInstanceManager.hpp"

#include "openvic-simulation/country/CountryInstance.hpp"
#include "openvic-simulation/country/CountryInstanceManager.hpp"
#include "openvic-simulation/DefinitionManager.hpp"
#include "openvic-simulation/InstanceManager.hpp"
#include "openvic-simulation/misc/Event.hpp"
#include "openvic-simulation/scripts/EvaluationContext.hpp"
#include "openvic-simulation/scripts/ExecutionContext.hpp"

using namespace OpenVic;

EventInstanceManager::EventInstanceManager(uint64_t new_rng_seed) : rng { new_rng_seed } {}

fixed_point_t EventInstanceManager::next_random_chance() {
	return fixed_point_t::parse_raw(rng() & (fixed_point_t::ONE - 1));
}

void EventInstanceManager::country_events_tick(InstanceManager& instance_manager) {
	for (Event const& event : instance_manager.definition_manager.get_event_manager().get_events()) {
		if (event.get_type() != Event::event_type_t::COUNTRY || event.is_triggered_only) {
			continue;
		}

		ordered_set<CountryInstance const*> const* fired_for = nullptr;
		if (event.fire_only_once) {
			const decltype(fired_events)::const_iterator it = fired_events.find(&event);
			if (it != fired_events.end()) {
				fired_for = &it->second;
			}
		}

		for (CountryInstance& country : instance_manager.get_country_instance_manager().get_country_instances()) {
			if (!country.exists()) {
				continue;
			}
			if (fired_for != nullptr && fired_for->contains(&country)) {
				continue;
			}

			const EvaluationContext context { instance_manager, &country };
			if (!event.check_trigger(context)) {
				continue;
			}

			if (next_random_chance() < event.calculate_daily_fire_chance(context)) {
				fire_country_event(event, instance_manager, country);
			}
		}
	}
}

void EventInstanceManager::fire_country_event(
	Event const& event, InstanceManager& instance_manager, CountryInstance& country
) {
	if (event.fire_only_once) {
		/* Recorded before execution so an effect chain re-triggering the event cannot recurse. */
		fired_events[&event].insert(&country);
	}

	SPDLOG_INFO("Firing event {} for country {}", event.get_identifier(), country.get_identifier());

	ExecutionContext context { instance_manager, &country };
	event.fire(context, event.choose_ai_option(context.to_evaluation_context()));
}
