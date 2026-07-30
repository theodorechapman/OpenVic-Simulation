#include "EvaluationContext.hpp"

#include "openvic-simulation/InstanceManager.hpp"

using namespace OpenVic;

EvaluationContext::EvaluationContext(
	InstanceManager const& new_instance_manager, scope_ref_t new_current_scope,
	scope_ref_t new_this_scope, scope_ref_t new_from_scope
) : today { new_instance_manager.get_today() }, global_flags { &new_instance_manager.get_global_flags() },
	instance_manager { &new_instance_manager }, current_scope { new_current_scope },
	this_scope { new_this_scope }, from_scope { new_from_scope } {}
