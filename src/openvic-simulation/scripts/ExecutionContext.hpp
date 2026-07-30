#pragma once

#include <variant>

#include "openvic-simulation/scripts/EvaluationContext.hpp"

namespace OpenVic {
	struct CountryInstance;
	struct FlagStrings;
	struct InstanceManager;
	struct Pop;
	struct ProvinceInstance;
	struct State;

	/* The game state an effect script executes against - the mutable counterpart of
	 * EvaluationContext. Contexts are small values - scope-changing effects copy them
	 * with a new current scope rather than mutating.
	 *
	 * The narrow members (today, global_flags) exist so execution can be unit tested without
	 * constructing a full game state; instance_manager may then be null, and executors requiring
	 * it must do nothing when it is null. */
	struct ExecutionContext {
		using scope_ref_t = std::variant<std::monostate, CountryInstance*, State*, ProvinceInstance*, Pop*>;

		const Date today;
		FlagStrings* const global_flags;
		InstanceManager* const instance_manager;

		scope_ref_t current_scope;
		scope_ref_t this_scope;
		scope_ref_t from_scope;

		/* Execute against a full game state. */
		ExecutionContext(
			InstanceManager& new_instance_manager, scope_ref_t new_current_scope,
			scope_ref_t new_this_scope = {}, scope_ref_t new_from_scope = {}
		);

		/* Execute against explicitly provided state, without a full game instance (for unit tests). */
		constexpr ExecutionContext(
			Date new_today, FlagStrings* new_global_flags, scope_ref_t new_current_scope = {},
			scope_ref_t new_this_scope = {}, scope_ref_t new_from_scope = {}
		) : today { new_today }, global_flags { new_global_flags }, instance_manager { nullptr },
			current_scope { new_current_scope }, this_scope { new_this_scope }, from_scope { new_from_scope } {}

		constexpr CountryInstance* get_current_country() const {
			CountryInstance* const* country = std::get_if<CountryInstance*>(&current_scope);
			return country != nullptr ? *country : nullptr;
		}
		constexpr State* get_current_state() const {
			State* const* state = std::get_if<State*>(&current_scope);
			return state != nullptr ? *state : nullptr;
		}
		constexpr ProvinceInstance* get_current_province() const {
			ProvinceInstance* const* province = std::get_if<ProvinceInstance*>(&current_scope);
			return province != nullptr ? *province : nullptr;
		}
		constexpr Pop* get_current_pop() const {
			Pop* const* pop = std::get_if<Pop*>(&current_scope);
			return pop != nullptr ? *pop : nullptr;
		}

		constexpr ExecutionContext with_current_scope(scope_ref_t new_current_scope) const {
			ExecutionContext copy { *this };
			copy.current_scope = new_current_scope;
			return copy;
		}

		/* A read-only view of this context, for evaluating conditions embedded in
		 * effect scripts (limit blocks). */
		constexpr EvaluationContext to_evaluation_context() const {
			return EvaluationContext {
				today, global_flags, instance_manager,
				_to_evaluation_scope(current_scope), _to_evaluation_scope(this_scope), _to_evaluation_scope(from_scope)
			};
		}

	private:
		static constexpr EvaluationContext::scope_ref_t _to_evaluation_scope(scope_ref_t scope) {
			return std::visit(
				[](auto scope_pointer) -> EvaluationContext::scope_ref_t {
					if constexpr (std::same_as<decltype(scope_pointer), std::monostate>) {
						return scope_pointer;
					} else {
						return static_cast<std::remove_pointer_t<decltype(scope_pointer)> const*>(scope_pointer);
					}
				},
				scope
			);
		}
	};
}
