#pragma once

namespace OpenVic {
	struct InstanceManager;

	/* Runtime decision handling. Currently only the AI pass exists: players will take decisions
	 * through the game action system, which can reuse Decision's check/take methods directly. */
	struct DecisionInstanceManager {
		/* Monthly AI decision pass: every AI country takes every decision whose potential and
		 * allow conditions pass and whose evaluated ai_will_do desire is positive.
		 * TODO - Victoria 2 also lets ai_will_do model reluctance below certainty; treating any
		 * positive desire as "take it" matches its common ai_will_do = { factor = 1 } usage. */
		void ai_decisions_tick(InstanceManager& instance_manager);
	};
}
