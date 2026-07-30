#pragma once

#include <cstdint>

#include <XoshiroCpp.hpp>

#include "openvic-simulation/types/fixed_point/FixedPoint.hpp"
#include "openvic-simulation/types/OrderedContainers.hpp"

namespace OpenVic {
	struct CountryInstance;
	struct Event;
	struct InstanceManager;

	/* Runtime state for spontaneous event firing: which fire_only_once events have fired for
	 * which countries, and the (deterministic, seeded) random source for MTTH rolls.
	 *
	 * Events currently auto-resolve with the AI's option choice as soon as they fire - presenting
	 * events to a human player (and keeping them pending until answered) comes later via the
	 * game action system. */
	struct EventInstanceManager {
	private:
		XoshiroCpp::Xoshiro256PlusPlus rng;
		ordered_map<Event const*, ordered_set<CountryInstance const*>> fired_events;

		/* A random fixed point value in [0, 1). */
		fixed_point_t next_random_chance();

	public:
		explicit EventInstanceManager(uint64_t new_rng_seed = 0x4F70656E56696321 /* "OpenVic!" */);

		/* Daily spontaneous country event checks: for every country and every non-triggered-only
		 * country event, roll the event's daily fire chance (1 / MTTH) if its trigger passes.
		 * TODO - Victoria 2 staggers checks across days for performance rather than
		 * checking every event for every country daily. */
		void country_events_tick(InstanceManager& instance_manager);

		/* Execute the event for the given country - immediate effects plus the AI-chosen
		 * option's effects - and record it for fire_only_once tracking. */
		void fire_country_event(Event const& event, InstanceManager& instance_manager, CountryInstance& country);
	};
}
