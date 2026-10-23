#include "core/handle_table.hpp"

namespace horde {

AgentId HandleTable::allocate(uint32_t p_dense_index) {
	uint32_t slot;
	if (!free_.empty()) {
		slot = free_.front();
		free_.pop_front();
	} else {
		if (generation_.size() >= MAX_SLOTS) {
			return INVALID_AGENT_ID;
		}
		slot = static_cast<uint32_t>(generation_.size());
		generation_.push_back(1);
		dense_.push_back(0);
		live_.push_back(0);
	}
	dense_[slot] = p_dense_index;
	live_[slot] = 1;
	live_count_++;
	return make_id(slot, generation_[slot]);
}

bool HandleTable::release(AgentId p_id) {
	if (!is_valid(p_id)) {
		return false;
	}
	const uint32_t slot = slot_of(p_id);
	live_[slot] = 0;
	live_count_--;
	const uint32_t generation = generation_[slot];
	if (generation >= MAX_GENERATION) {
		generation_[slot] = 0; // Retired: never reused, so old ids can't alias.
		retired_count_++;
	} else {
		generation_[slot] = static_cast<uint16_t>(generation + 1);
	}
	return true;
}

void HandleTable::recycle(uint32_t p_slot) {
	if (p_slot >= generation_.size() || live_[p_slot] || generation_[p_slot] == 0) {
		return;
	}
	free_.push_back(p_slot);
}

bool HandleTable::is_valid(AgentId p_id) const {
	if (p_id < 0) {
		return false;
	}
	const uint32_t slot = slot_of(p_id);
	const uint32_t generation = generation_of(p_id);
	return slot < generation_.size() && live_[slot] && generation != 0 && generation_[slot] == generation;
}

void HandleTable::clear() {
	free_.clear();
	const uint32_t count = slot_count();
	for (uint32_t slot = 0; slot < count; slot++) {
		if (live_[slot]) {
			release(make_id(slot, generation_[slot]));
		}
		if (generation_[slot] != 0) {
			free_.push_back(slot);
		}
	}
}

} // namespace horde
