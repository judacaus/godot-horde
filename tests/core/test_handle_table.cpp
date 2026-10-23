#include "core/handle_table.hpp"
#include "core/rng.hpp"

#include <doctest.h>

#include <set>
#include <vector>

using namespace horde;

TEST_CASE("HandleTable: allocated ids are valid, non-negative and never 0") {
	HandleTable table;
	for (uint32_t i = 0; i < 100; i++) {
		const AgentId id = table.allocate(i);
		REQUIRE(id != INVALID_AGENT_ID);
		CHECK(id > 0);
		CHECK(table.is_valid(id));
		CHECK(table.dense_index(id) == i);
	}
	CHECK_FALSE(table.is_valid(0));
	CHECK_FALSE(table.is_valid(-1));
	CHECK_FALSE(table.is_valid(INVALID_AGENT_ID));
	CHECK(table.live_count() == 100);
}

TEST_CASE("HandleTable: released ids become stale, also after the slot is reused") {
	HandleTable table;
	const AgentId a = table.allocate(0);
	CHECK(table.release(a));
	CHECK_FALSE(table.is_valid(a));
	CHECK_FALSE(table.release(a)); // Double release is rejected.

	table.recycle(HandleTable::slot_of(a));
	const AgentId b = table.allocate(0);
	CHECK(HandleTable::slot_of(b) == HandleTable::slot_of(a)); // Same slot...
	CHECK(b != a); // ...but a new generation.
	CHECK(table.is_valid(b));
	CHECK_FALSE(table.is_valid(a));
}

TEST_CASE("HandleTable: slots are not reused before recycle()") {
	HandleTable table;
	const AgentId a = table.allocate(0);
	table.release(a);
	const AgentId b = table.allocate(1);
	CHECK(HandleTable::slot_of(b) != HandleTable::slot_of(a));
}

TEST_CASE("HandleTable: forged ids for free or out-of-range slots are rejected") {
	HandleTable table;
	const AgentId a = table.allocate(0);
	const uint32_t slot = HandleTable::slot_of(a);
	const uint32_t generation = HandleTable::generation_of(a);
	table.release(a);
	// The bumped generation exists in the table, but the slot holds no live agent.
	CHECK_FALSE(table.is_valid(HandleTable::make_id(slot, generation + 1)));
	CHECK_FALSE(table.is_valid(HandleTable::make_id(12345, 1)));
	CHECK_FALSE(table.is_valid(HandleTable::make_id(slot, 0)));
}

TEST_CASE("HandleTable: generations retire instead of wrapping, so stale ids never alias") {
	HandleTable table;
	std::set<AgentId> seen;
	std::vector<AgentId> history;
	// Recycle the same slot far beyond the 11-bit generation range.
	for (uint32_t i = 0; i < 3 * HandleTable::MAX_GENERATION; i++) {
		const AgentId id = table.allocate(0);
		REQUIRE(id != INVALID_AGENT_ID);
		CHECK(seen.insert(id).second); // Every id handed out is unique.
		history.push_back(id);
		table.release(id);
		table.recycle(HandleTable::slot_of(id));
	}
	CHECK(table.retired_count() >= 2);
	for (AgentId old : history) {
		CHECK_FALSE(table.is_valid(old));
	}
}

TEST_CASE("HandleTable: clear() invalidates every live id") {
	HandleTable table;
	std::vector<AgentId> ids;
	for (uint32_t i = 0; i < 50; i++) {
		ids.push_back(table.allocate(i));
	}
	table.clear();
	CHECK(table.live_count() == 0);
	for (AgentId id : ids) {
		CHECK_FALSE(table.is_valid(id));
	}
	// Slots are available again and new ids differ from the old ones.
	const AgentId fresh = table.allocate(0);
	CHECK(table.is_valid(fresh));
	for (AgentId id : ids) {
		CHECK(fresh != id);
	}
}

TEST_CASE("HandleTable: random allocate/release sequences keep validity consistent") {
	Pcg32 rng(42);
	HandleTable table;
	std::vector<AgentId> live;
	std::vector<AgentId> dead;
	for (int op = 0; op < 20000; op++) {
		if (live.empty() || rng.next_float() < 0.55f) {
			const AgentId id = table.allocate(0);
			REQUIRE(id != INVALID_AGENT_ID);
			live.push_back(id);
		} else {
			const size_t k = rng.next_u32() % live.size();
			const AgentId id = live[k];
			live[k] = live.back();
			live.pop_back();
			REQUIRE(table.release(id));
			dead.push_back(id);
			if (rng.next_float() < 0.8f) {
				table.recycle(HandleTable::slot_of(id));
			}
		}
	}
	CHECK(table.live_count() == live.size());
	for (AgentId id : live) {
		CHECK(table.is_valid(id));
	}
	for (AgentId id : dead) {
		CHECK_FALSE(table.is_valid(id));
	}
}
