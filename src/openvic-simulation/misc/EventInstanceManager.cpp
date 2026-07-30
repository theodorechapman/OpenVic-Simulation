#include "EventInstanceManager.hpp"

#include <algorithm>
#include <utility>

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

void EventInstanceManager::record_fired_event(
	Event const& event, std::string_view target_identifier, bool is_province_event, size_t option_index, Date fire_date
) {
	if (fired_event_log.size() >= FIRED_EVENT_LOG_CAPACITY) {
		return;
	}
	fired_event_log.push_back({
		&event, memory::string { target_identifier }, is_province_event, option_index, fire_date
	});
}

memory::vector<EventInstanceManager::FiredEventRecord> EventInstanceManager::drain_fired_event_log() {
	return std::exchange(fired_event_log, {});
}

void EventInstanceManager::events_tick(InstanceManager& instance_manager) {
	pending_events_tick(instance_manager);
	country_events_tick(instance_manager);
	province_events_tick(instance_manager);
	on_action_pulses_tick(instance_manager);
}

void EventInstanceManager::queue_country_event(
	Event const& event, CountryInstance& country, ExecutionContext::scope_ref_t from_scope, Date fire_date
) {
	pending_events.push_back({ &event, &country, nullptr, from_scope, fire_date });
}

void EventInstanceManager::queue_province_event(
	Event const& event, ProvinceInstance& province, ExecutionContext::scope_ref_t from_scope, Date fire_date
) {
	pending_events.push_back({ &event, nullptr, &province, from_scope, fire_date });
}

void EventInstanceManager::pending_events_tick(InstanceManager& instance_manager) {
	const Date today = instance_manager.get_today();

	/* Index-based loop: firing an event can queue further events, including ones due today. */
	for (size_t index = 0; index < pending_events.size();) {
		if (pending_events[index].fire_date > today) {
			++index;
			continue;
		}

		/* Copied then erased before firing, as firing may reallocate the vector. */
		const PendingEvent pending = pending_events[index];
		pending_events.erase(pending_events.begin() + index);

		if (pending.country != nullptr) {
			fire_country_event(*pending.event, instance_manager, *pending.country, pending.from_scope);
		} else if (pending.province != nullptr) {
			fire_province_event(*pending.event, instance_manager, *pending.province, pending.from_scope);
		}
	}
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
	Event const& event, InstanceManager& instance_manager, CountryInstance& country,
	ExecutionContext::scope_ref_t from_scope
) {
	if (event.fire_only_once) {
		/* Recorded before execution so an effect chain re-triggering the event cannot recurse. */
		fired_country_events[&event].insert(&country);
	}

	ExecutionContext context { instance_manager, &country, &country, from_scope };

	if (!country.is_ai()) {
		/* Human-controlled: immediate effects run now, the option choice waits for the player. */
		event.execute_immediate(context);
		pending_player_events.push_back({
			next_player_event_instance_id++, &event, &country, nullptr, from_scope, instance_manager.get_today()
		});
		SPDLOG_INFO(
			"Event {} fired for player country {} - awaiting option choice",
			event.get_identifier(), country.get_identifier()
		);
		return;
	}

	const size_t option_index = event.choose_ai_option(context.to_evaluation_context(), next_random_chance());

	SPDLOG_INFO(
		"Firing event {} for country {} - AI picked option {} ({})",
		event.get_identifier(), country.get_identifier(), option_index,
		option_index < event.get_options().size() ? event.get_options()[option_index].get_name() : "<none>"
	);
	record_fired_event(event, country.get_identifier(), false, option_index, instance_manager.get_today());

	event.fire(context, option_index);
}

void EventInstanceManager::fire_province_event(
	Event const& event, InstanceManager& instance_manager, ProvinceInstance& province,
	ExecutionContext::scope_ref_t from_scope
) {
	if (event.fire_only_once) {
		fired_province_events[&event].insert(&province);
	}

	ExecutionContext context { instance_manager, &province, province.get_owner(), from_scope };

	CountryInstance* owner = province.get_owner();
	if (owner != nullptr && !owner->is_ai()) {
		/* The owner chooses the option for their provinces' events, as in Victoria 2. */
		event.execute_immediate(context);
		pending_player_events.push_back({
			next_player_event_instance_id++, &event, owner, &province, from_scope, instance_manager.get_today()
		});
		SPDLOG_INFO(
			"Event {} fired for province {} - awaiting option choice by owner {}",
			event.get_identifier(), province.get_identifier(), owner->get_identifier()
		);
		return;
	}

	const size_t option_index = event.choose_ai_option(context.to_evaluation_context(), next_random_chance());

	SPDLOG_INFO(
		"Firing event {} for province {} - AI picked option {} ({})",
		event.get_identifier(), province.get_identifier(), option_index,
		option_index < event.get_options().size() ? event.get_options()[option_index].get_name() : "<none>"
	);
	record_fired_event(event, province.get_identifier(), true, option_index, instance_manager.get_today());

	event.fire(context, option_index);
}

std::span<const EventInstanceManager::PlayerEventInstance> EventInstanceManager::get_pending_player_events() const {
	return pending_player_events;
}

bool EventInstanceManager::resolve_player_event(
	InstanceManager& instance_manager, uint64_t instance_id, size_t option_index
) {
	const auto it = std::find_if(
		pending_player_events.begin(), pending_player_events.end(),
		[instance_id](PlayerEventInstance const& pending) -> bool { return pending.instance_id == instance_id; }
	);
	if (it == pending_player_events.end()) {
		spdlog::warn_s("Tried to resolve unknown pending player event instance {}!", instance_id);
		return false;
	}

	/* Copied then erased before executing, as option effects may fire further events. */
	const PlayerEventInstance pending = *it;
	pending_player_events.erase(it);

	if (option_index >= pending.event->get_options().size() && !pending.event->get_options().empty()) {
		spdlog::warn_s(
			"Player chose out of range option {} for event {}!", option_index, pending.event->get_identifier()
		);
		return false;
	}

	SPDLOG_INFO(
		"Player resolved event {} with option {} ({})",
		pending.event->get_identifier(), option_index,
		option_index < pending.event->get_options().size()
			? pending.event->get_options()[option_index].get_name() : "<none>"
	);

	if (pending.province != nullptr) {
		ExecutionContext context { instance_manager, pending.province, pending.province->get_owner(), pending.from_scope };
		record_fired_event(
			*pending.event, pending.province->get_identifier(), true, option_index, instance_manager.get_today()
		);
		pending.event->execute_option(context, option_index);
	} else {
		ExecutionContext context { instance_manager, pending.country, pending.country, pending.from_scope };
		record_fired_event(
			*pending.event, pending.country->get_identifier(), false, option_index, instance_manager.get_today()
		);
		pending.event->execute_option(context, option_index);
	}
	return true;
}
