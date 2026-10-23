// Uniform spatial hash for points.
//
// Space is divided into square cells of `cell_size`. Each cell (cx, cy) maps to a
// bucket of a power-of-two table; build() counting-sorts the points by bucket so
// every bucket is a contiguous range of the sorted arrays. Several cells can share
// a bucket, so every entry also stores its exact cell and lookups skip entries from
// other cells. That guarantees each point is visited at most once per query.
#pragma once

#include "core/math.hpp"

#include <cstdint>
#include <vector>

namespace horde {

class SpatialHash {
public:
	// Rebuilds the hash from `p_count` points. Point i is reported back as index i.
	void build(const float *p_xs, const float *p_ys, uint32_t p_count, float p_cell_size);

	float cell_size() const { return cell_size_; }
	float inv_cell_size() const { return inv_cell_size_; }
	uint32_t size() const { return static_cast<uint32_t>(indices_.size()); }

	uint32_t bucket_of(int32_t p_cx, int32_t p_cy) const { return hash_cell(p_cx, p_cy) & mask_; }
	uint32_t bucket_begin(uint32_t p_bucket) const { return bucket_start_[p_bucket]; }
	uint32_t bucket_end(uint32_t p_bucket) const { return bucket_start_[p_bucket + 1]; }

	// Sorted (bucket-ordered) entry data. Entry e holds point index indices()[e].
	const uint32_t *indices() const { return indices_.data(); }
	const float *xs() const { return xs_.data(); }
	const float *ys() const { return ys_.data(); }
	const int32_t *cell_xs() const { return cell_xs_.data(); }
	const int32_t *cell_ys() const { return cell_ys_.data(); }

	// Calls p_visit(entry) for every entry whose cell intersects the box [p_min, p_max].
	// Entries are visited at most once. Order is deterministic for a given build.
	template <typename F>
	void for_each_in_box(Vec2 p_min, Vec2 p_max, F &&p_visit) const;

	static uint32_t hash_cell(int32_t p_cx, int32_t p_cy) {
		uint32_t h = static_cast<uint32_t>(p_cx) * 0x9E3779B1u;
		h ^= (static_cast<uint32_t>(p_cy) + 0x7F4A7C15u) * 0x85EBCA77u;
		h ^= h >> 16;
		return h;
	}

private:
	float cell_size_ = 1.0f;
	float inv_cell_size_ = 1.0f;
	uint32_t mask_ = 0;
	std::vector<uint32_t> bucket_start_; // Size: table size + 1.
	std::vector<uint32_t> indices_;
	std::vector<float> xs_;
	std::vector<float> ys_;
	std::vector<int32_t> cell_xs_;
	std::vector<int32_t> cell_ys_;
	std::vector<uint32_t> point_bucket_; // Scratch, indexed by point.
	std::vector<int32_t> point_cx_;
	std::vector<int32_t> point_cy_;
};

template <typename F>
void SpatialHash::for_each_in_box(Vec2 p_min, Vec2 p_max, F &&p_visit) const {
	const uint32_t count = size();
	if (count == 0) {
		return;
	}
	const int32_t x0 = cell_coord(p_min.x, inv_cell_size_);
	const int32_t y0 = cell_coord(p_min.y, inv_cell_size_);
	const int32_t x1 = cell_coord(p_max.x, inv_cell_size_);
	const int32_t y1 = cell_coord(p_max.y, inv_cell_size_);
	if (x1 < x0 || y1 < y0) {
		return;
	}
	const uint64_t cells = (static_cast<uint64_t>(x1 - x0) + 1u) * (static_cast<uint64_t>(y1 - y0) + 1u);
	if (cells > count) {
		// Huge box: scanning every entry once is cheaper than walking empty cells.
		for (uint32_t e = 0; e < count; e++) {
			if (cell_xs_[e] >= x0 && cell_xs_[e] <= x1 && cell_ys_[e] >= y0 && cell_ys_[e] <= y1) {
				p_visit(e);
			}
		}
		return;
	}
	for (int32_t cy = y0; cy <= y1; cy++) {
		for (int32_t cx = x0; cx <= x1; cx++) {
			const uint32_t bucket = bucket_of(cx, cy);
			const uint32_t end = bucket_start_[bucket + 1];
			for (uint32_t e = bucket_start_[bucket]; e < end; e++) {
				if (cell_xs_[e] == cx && cell_ys_[e] == cy) {
					p_visit(e);
				}
			}
		}
	}
}

} // namespace horde
