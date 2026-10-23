#include "core/spatial_hash.hpp"

#include <algorithm>

namespace horde {

void SpatialHash::build(const float *p_xs, const float *p_ys, uint32_t p_count, float p_cell_size) {
	cell_size_ = p_cell_size > 0.0f ? p_cell_size : 1.0f;
	inv_cell_size_ = 1.0f / cell_size_;

	// Power-of-two table with load factor <= 0.5 keeps buckets short.
	uint32_t table_size = 16;
	while (table_size < p_count * 2u && table_size < (1u << 30)) {
		table_size <<= 1u;
	}
	mask_ = table_size - 1u;

	bucket_start_.assign(table_size + 1u, 0u);
	indices_.resize(p_count);
	xs_.resize(p_count);
	ys_.resize(p_count);
	cell_xs_.resize(p_count);
	cell_ys_.resize(p_count);
	point_bucket_.resize(p_count);
	point_cx_.resize(p_count);
	point_cy_.resize(p_count);

	// Count points per bucket (shifted by one so the prefix sum yields bucket ends).
	for (uint32_t i = 0; i < p_count; i++) {
		const int32_t cx = cell_coord(p_xs[i], inv_cell_size_);
		const int32_t cy = cell_coord(p_ys[i], inv_cell_size_);
		const uint32_t bucket = bucket_of(cx, cy);
		point_cx_[i] = cx;
		point_cy_[i] = cy;
		point_bucket_[i] = bucket;
		bucket_start_[bucket + 1u]++;
	}
	for (uint32_t b = 0; b < table_size; b++) {
		bucket_start_[b + 1u] += bucket_start_[b];
	}

	// Scatter in reverse, filling each bucket from its end: entries inside a bucket
	// end up in increasing point order and bucket_start_ ends up as bucket begins.
	for (uint32_t i = p_count; i-- > 0;) {
		const uint32_t e = --bucket_start_[point_bucket_[i] + 1u];
		indices_[e] = i;
		xs_[e] = p_xs[i];
		ys_[e] = p_ys[i];
		cell_xs_[e] = point_cx_[i];
		cell_ys_[e] = point_cy_[i];
	}
	// After the reverse scatter, bucket_start_[b + 1] holds the begin of bucket b.
	// Shift left by one so bucket_start_[b] is the begin and bucket_start_[b + 1] the end.
	std::rotate(bucket_start_.begin(), bucket_start_.begin() + 1, bucket_start_.end());
	bucket_start_[table_size] = p_count;
}

} // namespace horde
