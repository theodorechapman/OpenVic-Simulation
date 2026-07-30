#pragma once

namespace OpenVic {
	struct EffectNode;
	struct ExecutionContext;

	/* Execution functions bound to Effects at registration time (see EffectManager::setup_effects).
	 * Each takes the mutable execution context and the parsed node of the effect it is bound to,
	 * and applies the effect to the current game state. */
	namespace EffectExecutors {
		/* Fallback bound to effects without an implemented executor - warns once and does nothing. */
		void unimplemented(ExecutionContext& context, EffectNode const& node);

		/* Scope-changing effects. A scope node's limit condition gates execution against each target. */
		void redirect_this(ExecutionContext& context, EffectNode const& node);
		void redirect_from(ExecutionContext& context, EffectNode const& node);
		void capital_scope(ExecutionContext& context, EffectNode const& node);
		void province_owner(ExecutionContext& context, EffectNode const& node);
		void any_owned_province(ExecutionContext& context, EffectNode const& node);

		/* Flag effects */
		void set_global_flag(ExecutionContext& context, EffectNode const& node);
		void clr_global_flag(ExecutionContext& context, EffectNode const& node);
		void set_country_flag(ExecutionContext& context, EffectNode const& node);
		void clr_country_flag(ExecutionContext& context, EffectNode const& node);

		/* Core effects - dual form: in province scope the value names the country gaining/losing
		 * the core, in country scope the value names the province gaining/losing it. */
		void add_core(ExecutionContext& context, EffectNode const& node);
		void remove_core(ExecutionContext& context, EffectNode const& node);
	}
}
