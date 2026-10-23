// Stable agent handles with generation counters.
//
// An AgentId is a non-negative 31-bit integer so it fits in GDScript ints and
// PackedInt32Array, and -1 can mean "no agent":
//
//   bits  0..19  slot index (up to 1,048,576 slots)
//   bits 20..30  generation (1..2047)
//
// Generation 0 is never handed out, so the id 0 is never valid. When an agent is
// released its slot's generation is bumped, which makes every copy of the old id
// stale. Released slots are reused in FIFO order, and a slot whose generation would
// wrap around is retired forever, so a stale id can never alias a newer agent.
#pragma once

#include <cstdint>
#include <deque>
#include <vector>

namespace horde {

using AgentId = int32_t;
constexpr AgentId INVALID_AGENT_ID = -1;

class HandleTable {
public:
	static constexpr uint32_t SLOT_BITS = 20;
	static constexpr uint32_t MAX_SLOTS = 1u << SLOT_BITS;
	static constexpr uint32_t SLOT_MASK = MAX_SLOTS - 1u;
	static constexpr uint32_t GENERATION_BITS = 11;
	static constexpr uint32_t MAX_GENERATION = (1u << GENERATION_BITS) - 1u;

	static constexpr uint32_t slot_of(AgentId p_id) { return static_cast<uint32_t>(p_id) & SLOT_MASK; }
	static constexpr uint32_t generation_of(AgentId p_id) { return (static_cast<uint32_t>(p_id) >> SLOT_BITS) & MAX_GENERATION; }
	static constexpr AgentId make_id(uint32_t p_slot, uint32_t p_generation) {
		return static_cast<AgentId>((p_generation << SLOT_BITS) | p_slot);
	}

	// Allocates a slot pointing at `p_dense_index`. Returns INVALID_AGENT_ID when all
	// slots are in use or retired.
	AgentId allocate(uint32_t p_dense_index);

	// Invalidates `p_id`. Returns false if the id was already stale. The slot is not
	// reusable until recycle() is called for it.
	bool release(AgentId p_id);

	// Makes a released slot available for allocation again (unless it is retired).
	void recycle(uint32_t p_slot);

	bool is_valid(AgentId p_id) const;

	// Precondition: is_valid(p_id) or the slot is live.
	uint32_t dense_index(AgentId p_id) const { return dense_[slot_of(p_id)]; }
	void set_dense_index(uint32_t p_slot, uint32_t p_dense_index) { dense_[p_slot] = p_dense_index; }

	// Id of the agent currently living in `p_slot`.
	AgentId id_for_slot(uint32_t p_slot) const { return make_id(p_slot, generation_[p_slot]); }

	// Releases every live id and makes all non-retired slots available again.
	void clear();

	uint32_t slot_count() const { return static_cast<uint32_t>(generation_.size()); }
	uint32_t live_count() const { return live_count_; }
	uint32_t retired_count() const { return retired_count_; }

private:
	std::vector<uint16_t> generation_; // Current generation per slot; 0 means retired.
	std::vector<uint32_t> dense_; // Slot -> dense index.
	std::vector<uint8_t> live_;
	std::deque<uint32_t> free_; // FIFO reuse maximises the time before a slot is reused.
	uint32_t live_count_ = 0;
	uint32_t retired_count_ = 0;
};

} // namespace horde
