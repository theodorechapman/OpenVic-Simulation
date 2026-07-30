#pragma once

#include <variant>

#include "openvic-simulation/scripts/Condition.hpp"
#include "openvic-simulation/types/Date.hpp"

namespace OpenVic {
	struct CountryInstance;
	struct FlagStrings;
	struct InstanceManager;
	struct Pop;
	struct ProvinceInstance;
	struct State;

	/* The game state a condition script is evaluated against: the scope object the script is
	 * currently testing, plus the THIS and FROM scopes it was invoked with. Contexts are small
	 * values - scope-changing conditions copy them with a new current scope rather than mutating.
	 *
	 * The narrow members (today, global_flags) exist so evaluation can be unit tested without
	 * constructing a full game state; instance_manager may then be null, and evaluators requiring
	 * it must treat null as condition-not-met. */
	struct EvaluationContext {
		using scope_ref_t = std::variant<
			std::monostate, CountryInstance const*, State const*, ProvinceInstance const*, Pop const*
		>;

		const Date today;
		FlagStrings const* const global_flags;
		InstanceManager const* const instance_manager;

		scope_ref_t current_scope;
		scope_ref_t this_scope;
		scope_ref_t from_scope;

		/* Evaluate against a full game state. */
		EvaluationContext(
			InstanceManager const& new_instance_manager, scope_ref_t new_current_scope,
			scope_ref_t new_this_scope = {}, scope_ref_t new_from_scope = {}
		);

		/* Evaluate against explicitly provided state, without a full game instance (for unit tests). */
		constexpr EvaluationContext(
			Date new_today, FlagStrings const* new_global_flags, scope_ref_t new_current_scope = {},
			scope_ref_t new_this_scope = {}, scope_ref_t new_from_scope = {}
		) : EvaluationContext {
			new_today, new_global_flags, nullptr, new_current_scope, new_this_scope, new_from_scope
		} {}

		constexpr EvaluationContext(
			Date new_today, FlagStrings const* new_global_flags, InstanceManager const* new_instance_manager,
			scope_ref_t new_current_scope, scope_ref_t new_this_scope, scope_ref_t new_from_scope
		) : today { new_today }, global_flags { new_global_flags }, instance_manager { new_instance_manager },
			current_scope { new_current_scope }, this_scope { new_this_scope }, from_scope { new_from_scope } {}

		constexpr scope_type_t get_current_scope_type() const {
			if (std::holds_alternative<CountryInstance const*>(current_scope)) {
				return scope_type_t::COUNTRY;
			} else if (std::holds_alternative<State const*>(current_scope)) {
				return scope_type_t::STATE;
			} else if (std::holds_alternative<ProvinceInstance const*>(current_scope)) {
				return scope_type_t::PROVINCE;
			} else if (std::holds_alternative<Pop const*>(current_scope)) {
				return scope_type_t::POP;
			} else {
				return scope_type_t::NO_SCOPE;
			}
		}

		constexpr CountryInstance const* get_current_country() const {
			CountryInstance const* const* country = std::get_if<CountryInstance const*>(&current_scope);
			return country != nullptr ? *country : nullptr;
		}
		constexpr State const* get_current_state() const {
			State const* const* state = std::get_if<State const*>(&current_scope);
			return state != nullptr ? *state : nullptr;
		}
		constexpr ProvinceInstance const* get_current_province() const {
			ProvinceInstance const* const* province = std::get_if<ProvinceInstance const*>(&current_scope);
			return province != nullptr ? *province : nullptr;
		}
		constexpr Pop const* get_current_pop() const {
			Pop const* const* pop = std::get_if<Pop const*>(&current_scope);
			return pop != nullptr ? *pop : nullptr;
		}

		constexpr EvaluationContext with_current_scope(scope_ref_t new_current_scope) const {
			EvaluationContext copy { *this };
			copy.current_scope = new_current_scope;
			return copy;
		}
	};
}
