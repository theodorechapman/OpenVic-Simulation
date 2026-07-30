#include "EventInstanceManager.hpp"

#include "openvic-simulation/country/CountryInstance.hpp"
#include "openvic-simulation/country/CountryInstanceManager.hpp"
#include "openvic-simulation/DefinitionManager.hpp"
#include "openvic-simulation/InstanceManager.hpp"
#include "openvic-simulation/map/MapInstance.hpp"
#include "openvic-simulation/map/ProvinceDefinition.hpp"
#include "openvic-simulation/map/ProvinceInstance.hpp"
#include "openvic-simulation/misc/Event.hpp"
#include "openvic-simulation/scripts/EvaluationContext.hpp"
#include "openvic-simulation/scripts/ExecutionContext.hpp"

using namespace OpenVic;

EventInstanceManager::EventInstanceManager(uint64_t new_rng_seed) : rng { new_rng_seed } {}

fixed_point_t EventInstanceManager::next_random_chance() {
	return fixed_point_t::parse_raw(rng() & (fixed_point_t::ONE - 1));
}

void EventInstanceManager::events_tick(InstanceManager& instance_manager) {
	country_events_tick(instance_manager);
	province_events_tick(instance_manager);
	on_action_pulses_tick(instance_manager);
}

void EventInstanceManager::on_action_pulses_tick(InstanceManager& instance_manager) {
	const Date today = instance_manager.get_today();
	if (!today.is_month_start()) {
		return;
	}

	EventManager const& event_manager = instance_manager.definition_manager.get_event_manager();

	if (today.get_month() == 1) {
		OnAction const* yearly_pulse = event_manager.get_on_action_by_identifier("on_yearly_pulse");
		if (yearly_pulse != nullptr) {
			fire_on_action_pulse(*yearly_pulse, instance_manager);
		}
	}
	if ((today.get_month() - 1) % 3 == 0) {
		OnAction const* quarterly_pulse = event_manager.get_on_action_by_identifier("on_quarterly_pulse");
		if (quarterly_pulse != nullptr) {
			fire_on_action_pulse(*quarterly_pulse, instance_manager);
		}
	}
}

void EventInstanceManager::fire_on_action_pulse(OnAction const& on_action, InstanceManager& instance_manager) {
	OnAction::weight_map_t const& weighted_events = on_action.get_weighted_events();

	uint64_t total_weight = 0;
	for (auto const& [event, weight] : weighted_events) {
		total_weight += weight;
	}
	if (total_weight == 0) {
		return;
	}

	for (CountryInstance& country : instance_manager.get_country_instance_manager().get_country_instances()) {
		if (!country.exists()) {
			continue;
		}

		uint64_t roll = rng() % total_weight;
		Event const* chosen_event = nullptr;
		for (auto const& [event, weight] : weighted_events) {
			if (roll < weight) {
				chosen_event = event;
				break;
			}
			roll -= weight;
		}
		if (chosen_event == nullptr) {
			continue;
		}

		if (chosen_event->fire_only_once) {
			const decltype(fired_country_events)::const_iterator it = fired_country_events.find(chosen_event);
			if (it != fired_country_events.end() && it->second.contains(&country)) {
				continue;
			}
		}

		const EvaluationContext context { instance_manager, &country, &country };
		if (chosen_event->check_trigger(context)) {
			fire_country_event(*chosen_event, instance_manager, country);
		}
	}
}

void EventInstanceManager::country_events_tick(InstanceManager& instance_manager) {
	for (Event const& event : instance_manager.definition_manager.get_event_manager().get_events()) {
		if (event.get_type() != Event::event_type_t::COUNTRY || event.is_triggered_only) {
			continue;
		}

		ordered_set<CountryInstance const*> const* fired_for = nullptr;
		if (event.fire_only_once) {
			const decltype(fired_country_events)::const_iterator it = fired_country_events.find(&event);
			if (it != fired_country_events.end()) {
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

			const EvaluationContext context { instance_manager, &country, &country };
			if (!event.check_trigger(context)) {
				continue;
			}

			if (next_random_chance() < event.calculate_daily_fire_chance(context)) {
				fire_country_event(event, instance_manager, country);
			}
		}
	}
}

void EventInstanceManager::province_events_tick(InstanceManager& instance_manager) {
	for (Event const& event : instance_manager.definition_manager.get_event_manager().get_events()) {
		if (event.get_type() != Event::event_type_t::PROVINCE || event.is_triggered_only) {
			continue;
		}

		ordered_set<ProvinceInstance const*> const* fired_for = nullptr;
		if (event.fire_only_once) {
			const decltype(fired_province_events)::const_iterator it = fired_province_events.find(&event);
			if (it != fired_province_events.end()) {
				fired_for = &it->second;
			}
		}

		for (ProvinceInstance& province : instance_manager.get_map_instance().get_province_instances()) {
			/* Only owned land provinces receive spontaneous events. */
			if (province.province_definition.is_water() || province.get_owner() == nullptr) {
				continue;
			}
			if (fired_for != nullptr && fired_for->contains(&province)) {
				continue;
			}

			const EvaluationContext context { instance_manager, &province, province.get_owner() };
			if (!event.check_trigger(context)) {
				continue;
			}

			if (next_random_chance() < event.calculate_daily_fire_chance(context)) {
				fire_province_event(event, instance_manager, province);
			}
		}
	}
}

void EventInstanceManager::fire_country_event(
	Event const& event, InstanceManager& instance_manager, CountryInstance& country
) {
	if (event.fire_only_once) {
		/* Recorded before execution so an effect chain re-triggering the event cannot recurse. */
		fired_country_events[&event].insert(&country);
	}

	SPDLOG_INFO("Firing event {} for country {}", event.get_identifier(), country.get_identifier());

	ExecutionContext context { instance_manager, &country, &country };
	event.fire(context, event.choose_ai_option(context.to_evaluation_context(), next_random_chance()));
}

void EventInstanceManager::fire_province_event(
	Event const& event, InstanceManager& instance_manager, ProvinceInstance& province
) {
	if (event.fire_only_once) {
		fired_province_events[&event].insert(&province);
	}

	SPDLOG_INFO("Firing event {} for province {}", event.get_identifier(), province.get_identifier());

	ExecutionContext context { instance_manager, &province, province.get_owner() };
	event.fire(context, event.choose_ai_option(context.to_evaluation_context(), next_random_chance()));
}
