#include <span>

#include "openvic-simulation/economy/GoodDefinition.hpp"
#include "openvic-simulation/types/IndexedFlatMap.hpp"

#include "Helper.hpp" // IWYU pragma: keep
#include <snitch/snitch_macros_check.hpp>
#include <snitch/snitch_macros_test_case.hpp>

using namespace OpenVic;

/* Regression test: constructing an IndexedFlatMap over an empty key span used to call
 * front()/back() on the empty span (undefined behaviour, crashing in practice), making
 * instance managers unconstructible over empty definition registries. Empty maps must
 * instead match the state produced by default construction. */
TEST_CASE("IndexedFlatMap empty key span construction", "[IndexedFlatMap]") {
	const std::span<const GoodDefinition> no_keys {};

	const IndexedFlatMap<GoodDefinition, int> map { no_keys };

	CHECK(map.get_keys().empty());
}
