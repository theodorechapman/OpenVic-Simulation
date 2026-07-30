#pragma once

#include <cstdint>

#include <XoshiroCpp.hpp>

#include "openvic-simulation/core/memory/String.hpp"
#include "openvic-simulation/core/memory/Vector.hpp"
#include "openvic-simulation/scripts/ExecutionContext.hpp"
#include "openvic-simulation/types/Date.hpp"
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
	public:
		/* A record of an event having fired, for UIs to display. */
		struct FiredEventRecord {
			Event const* event;
			memory::string target_identifier;
			bool is_province_event;
			size_t option_index;
			Date fire_date;
		};

		/* An event that fired for a human-controlled country and awaits their option choice.
		 * Immediate effects have already run, as in Victoria 2. */
		struct PlayerEventInstance {
			uint64_t instance_id;
			Event const* event;
			CountryInstance* country;   /* The choosing country. */
			ProvinceInstance* province; /* Target for province events, else null. */
			ExecutionContext::scope_ref_t from_scope;
			Date fire_date;
		};

	private:
		/* An event queued by the delayed form of the country_event/province_event effects
		 * (country_event = { id = X days = Y }), fired when its date arrives. Instance pointers
		 * are stable - countries and provinces are never created or destroyed mid-game. */
		struct PendingEvent {
			Event const* event;
			CountryInstance* country;   /* Target for country events, else null. */
			ProvinceInstance* province; /* Target for province events, else null. */
			ExecutionContext::scope_ref_t from_scope;
			Date fire_date;
		};

		XoshiroCpp::Xoshiro256PlusPlus rng;
		ordered_map<Event const*, ordered_set<CountryInstance const*>> fired_country_events;
		ordered_map<Event const*, ordered_set<ProvinceInstance const*>> fired_province_events;
		memory::vector<PendingEvent> pending_events;
		/* Capped so undrained (e.g. headless) runs don't grow it unboundedly. */
		static constexpr size_t FIRED_EVENT_LOG_CAPACITY = 1024;
		memory::vector<FiredEventRecord> fired_event_log;
		memory::vector<PlayerEventInstance> pending_player_events;
		uint64_t next_player_event_instance_id = 1;

		void record_fired_event(
			Event const& event, std::string_view target_identifier, bool is_province_event, size_t option_index,
			Date fire_date
		);

		/* A random fixed point value in [0, 1). */
		fixed_point_t next_random_chance();

		void pending_events_tick(InstanceManager& instance_manager);
		void country_events_tick(InstanceManager& instance_manager);
		void province_events_tick(InstanceManager& instance_manager);
		void on_action_pulses_tick(InstanceManager& instance_manager);

		/* Fire one weighted-random event from the on_action's table for each country,
		 * if that event's trigger passes for it. */
		void fire_on_action_pulse(OnAction const& on_action, InstanceManager& instance_manager);

	public:
		explicit EventInstanceManager(uint64_t new_rng_seed = 0x4F70656E56696321 /* "OpenVic!" */);

		/* Daily event processing: fire due queued events, then spontaneous checks - for every
		 * country (or owned land province) and every non-triggered-only event of the matching
		 * type, roll the event's daily fire chance (1 / MTTH) if its trigger passes.
		 * TODO - Victoria 2 staggers checks across days for performance rather than
		 * checking every event for every country/province daily. */
		void events_tick(InstanceManager& instance_manager);

		/* Queue an event to fire when the given date arrives, keeping the sender as FROM. */
		void queue_country_event(
			Event const& event, CountryInstance& country, ExecutionContext::scope_ref_t from_scope, Date fire_date
		);
		void queue_province_event(
			Event const& event, ProvinceInstance& province, ExecutionContext::scope_ref_t from_scope, Date fire_date
		);

		/* Execute the event for the given country - immediate effects plus the AI-chosen
		 * option's effects - and record it for fire_only_once tracking. from_scope is the
		 * sender for events fired by effects, empty for spontaneous events. */
		void fire_country_event(
			Event const& event, InstanceManager& instance_manager, CountryInstance& country,
			ExecutionContext::scope_ref_t from_scope = {}
		);

		/* As above for a province event, executed in the province's scope with the
		 * province's owner as THIS. */
		void fire_province_event(
			Event const& event, InstanceManager& instance_manager, ProvinceInstance& province,
			ExecutionContext::scope_ref_t from_scope = {}
		);

		/* Take (and clear) the accumulated fired-event records, for UIs to display. */
		memory::vector<FiredEventRecord> drain_fired_event_log();

		/* Events awaiting a human player's option choice. */
		std::span<const PlayerEventInstance> get_pending_player_events() const;

		/* Execute the chosen option of a pending player event and remove it from the pending
		 * list. Returns false for unknown instance ids or out of range options. Invoked
		 * through the respond_to_event game action. */
		bool resolve_player_event(InstanceManager& instance_manager, uint64_t instance_id, size_t option_index);
	};
}
