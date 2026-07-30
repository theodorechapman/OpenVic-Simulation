#pragma once

#include <cstdint>

#include <XoshiroCpp.hpp>

#include "openvic-simulation/types/fixed_point/FixedPoint.hpp"
#include "openvic-simulation/types/OrderedContainers.hpp"

namespace OpenVic {
	struct CountryInstance;
	struct Event;
	struct InstanceManager;
	struct OnAction;
	struct ProvinceInstance;

	/* Runtime state for spontaneous event firing: which fire_only_once events have fired for
	 * which countries/provinces, and the (deterministic, seeded) random source for MTTH rolls.
	 *
	 * Events currently auto-resolve with the AI's option choice as soon as they fire - presenting
	 * events to a human player (and keeping them pending until answered) comes later via the
	 * game action system. */
	struct EventInstanceManager {
	private:
		XoshiroCpp::Xoshiro256PlusPlus rng;
		ordered_map<Event const*, ordered_set<CountryInstance const*>> fired_country_events;
		ordered_map<Event const*, ordered_set<ProvinceInstance const*>> fired_province_events;

		/* A random fixed point value in [0, 1). */
		fixed_point_t next_random_chance();

		void country_events_tick(InstanceManager& instance_manager);
		void province_events_tick(InstanceManager& instance_manager);
		void on_action_pulses_tick(InstanceManager& instance_manager);

		/* Fire one weighted-random event from the on_action's table for each country,
		 * if that event's trigger passes for it. */
		void fire_on_action_pulse(OnAction const& on_action, InstanceManager& instance_manager);

	public:
		explicit EventInstanceManager(uint64_t new_rng_seed = 0x4F70656E56696321 /* "OpenVic!" */);

		/* Daily spontaneous event checks: for every country (or owned land province) and every
		 * non-triggered-only event of the matching type, roll the event's daily fire chance
		 * (1 / MTTH) if its trigger passes.
		 * TODO - Victoria 2 staggers checks across days for performance rather than
		 * checking every event for every country/province daily. */
		void events_tick(InstanceManager& instance_manager);

		/* Execute the event for the given country - immediate effects plus the AI-chosen
		 * option's effects - and record it for fire_only_once tracking. */
		void fire_country_event(Event const& event, InstanceManager& instance_manager, CountryInstance& country);

		/* As above for a province event, executed in the province's scope with the
		 * province's owner as THIS. */
		void fire_province_event(Event const& event, InstanceManager& instance_manager, ProvinceInstance& province);
	};
}
