#include "core/simulation.hpp"

#include <algorithm>
#include <cmath>

namespace horde {

namespace {

constexpr float TWO_PI = 6.28318530717958647692f;
constexpr float COINCIDENT_DISTANCE_SQ = 1e-12f;
// Obstacles covering more cells than this go to a list that every agent checks.
constexpr uint64_t MAX_CELLS_PER_OBSTACLE = 1024;
// Neighbour searches never look further than this many cells away; the effective
// cell size is raised if a user-provided cell size would need more.
constexpr int32_t MAX_NEIGHBOR_RING = 4;

// Deterministic and antisymmetric push direction for two agents at the same spot.
Vec2 coincident_direction(uint32_t p_slot_self, uint32_t p_slot_other) {
	constexpr float D = 0.70710678f;
	static const Vec2 DIRECTIONS[8] = {
		Vec2(1.0f, 0.0f), Vec2(D, D), Vec2(0.0f, 1.0f), Vec2(-D, D),
		Vec2(-1.0f, 0.0f), Vec2(-D, -D), Vec2(0.0f, -1.0f), Vec2(D, -D)
	};
	const uint32_t lo = std::min(p_slot_self, p_slot_other);
	const uint32_t hi = std::max(p_slot_self, p_slot_other);
	uint32_t h = lo * 0x9E3779B1u ^ hi * 0x85EBCA77u;
	h ^= h >> 15;
	const Vec2 dir = DIRECTIONS[h & 7u];
	return p_slot_self == lo ? dir : dir * -1.0f;
}

// Pushes a circle of combined radius `p_reach` out of `p_center` and removes the
// velocity component pointing into it.
inline void push_out(Vec2 p_center, float p_reach, float &r_x, float &r_y, float &r_vx, float &r_vy) {
	const float dx = r_x - p_center.x;
	const float dy = r_y - p_center.y;
	const float d2 = dx * dx + dy * dy;
	if (d2 >= p_reach * p_reach) {
		return;
	}
	float nx = 1.0f;
	float ny = 0.0f;
	if (d2 > COINCIDENT_DISTANCE_SQ) {
		const float inv_d = 1.0f / std::sqrt(d2);
		nx = dx * inv_d;
		ny = dy * inv_d;
	}
	r_x = p_center.x + nx * p_reach;
	r_y = p_center.y + ny * p_reach;
	const float vn = r_vx * nx + r_vy * ny;
	if (vn < 0.0f) {
		r_vx -= nx * vn;
		r_vy -= ny * vn;
	}
}

inline void clamp_length(float &r_x, float &r_y, float p_max) {
	const float l2 = r_x * r_x + r_y * r_y;
	if (l2 > p_max * p_max) {
		const float s = p_max / std::sqrt(l2);
		r_x *= s;
		r_y *= s;
	}
}

TypeParams sanitized(const TypeParams &p_params) {
	TypeParams p = p_params;
	p.radius = std::max(p.radius, 0.0f);
	p.max_speed = std::max(p.max_speed, 0.0f);
	p.acceleration = std::max(p.acceleration, 0.0f);
	p.max_hp = std::max(p.max_hp, 0.0f);
	return p;
}

} // namespace

Simulation::Simulation() {
	update_cell_size();
}

// --- Types and configuration -------------------------------------------------------

uint16_t Simulation::add_type(const TypeParams &p_params) {
	if (types_.size() >= MAX_TYPES) {
		return MAX_TYPES;
	}
	types_.push_back(sanitized(p_params));
	update_cell_size();
	return static_cast<uint16_t>(types_.size() - 1);
}

void Simulation::set_type(uint16_t p_type, const TypeParams &p_params) {
	if (p_type >= types_.size()) {
		return;
	}
	types_[p_type] = sanitized(p_params);
	update_cell_size();
	// Radii are cached next to the hash entries.
	hash_valid_ = false;
}

void Simulation::set_cell_size(float p_cell_size) {
	if (!std::isfinite(p_cell_size)) {
		return;
	}
	cell_size_setting_ = p_cell_size > 0.0f ? p_cell_size : 0.0f;
	update_cell_size();
}

void Simulation::update_cell_size() {
	float max_radius = 0.0f;
	for (const TypeParams &t : types_) {
		max_radius = std::max(max_radius, t.radius);
	}
	const float interaction = 2.0f * max_radius;
	float cell = cell_size_setting_ > 0.0f ? cell_size_setting_ : interaction;
	cell = std::max(cell, interaction / static_cast<float>(MAX_NEIGHBOR_RING));
	cell = std::max(cell, 1.0f);
	if (cell != cell_size_ || max_radius != max_radius_) {
		hash_valid_ = false;
		obstacle_grid_valid_ = false;
	}
	cell_size_ = cell;
	max_radius_ = max_radius;
	neighbor_ring_ = std::max<int32_t>(1, static_cast<int32_t>(std::ceil(interaction / cell_size_)));
}

void Simulation::set_bounds(const Rect2f &p_bounds) {
	if (!std::isfinite(p_bounds.position.x) || !std::isfinite(p_bounds.position.y) ||
			!std::isfinite(p_bounds.size.x) || !std::isfinite(p_bounds.size.y)) {
		return;
	}
	bounds_ = p_bounds;
	if (bounds_.size.x < 0.0f) {
		bounds_.position.x += bounds_.size.x;
		bounds_.size.x = -bounds_.size.x;
	}
	if (bounds_.size.y < 0.0f) {
		bounds_.position.y += bounds_.size.y;
		bounds_.size.y = -bounds_.size.y;
	}
	bounds_enabled_ = true;
}

uint32_t Simulation::add_obstacle(Vec2 p_center, float p_radius) {
	// An infinite obstacle would push every agent to infinity (or NaN) for good.
	if (!std::isfinite(p_center.x) || !std::isfinite(p_center.y) || !std::isfinite(p_radius)) {
		return INVALID_OBSTACLE;
	}
	obstacles_.push_back(Obstacle{ p_center, std::max(p_radius, 0.0f) });
	obstacle_grid_valid_ = false;
	return static_cast<uint32_t>(obstacles_.size() - 1);
}

void Simulation::clear_obstacles() {
	obstacles_.clear();
	obstacle_grid_valid_ = false;
}

void Simulation::rebuild_obstacle_grid() {
	ObstacleGrid &g = obstacle_grid_;
	g.large.clear();
	struct Entry {
		int32_t cx, cy;
		uint32_t obstacle;
	};
	std::vector<Entry> entries;
	const float inv_cell = 1.0f / cell_size_;
	for (uint32_t o = 0; o < obstacles_.size(); o++) {
		const Obstacle &ob = obstacles_[o];
		const float reach = ob.radius + max_radius_;
		const float pad = reach * 1e-4f + 1e-2f;
		const int32_t x0 = cell_coord(ob.center.x - reach - pad, inv_cell);
		const int32_t x1 = cell_coord(ob.center.x + reach + pad, inv_cell);
		const int32_t y0 = cell_coord(ob.center.y - reach - pad, inv_cell);
		const int32_t y1 = cell_coord(ob.center.y + reach + pad, inv_cell);
		// 64-bit difference: saturated cell coordinates span up to 2^31 cells.
		const uint64_t cells = (static_cast<uint64_t>(static_cast<int64_t>(x1) - x0) + 1u) *
				(static_cast<uint64_t>(static_cast<int64_t>(y1) - y0) + 1u);
		if (cells > MAX_CELLS_PER_OBSTACLE) {
			g.large.push_back(o);
			continue;
		}
		for (int32_t cy = y0; cy <= y1; cy++) {
			for (int32_t cx = x0; cx <= x1; cx++) {
				entries.push_back(Entry{ cx, cy, o });
			}
		}
	}

	uint32_t table_size = 16;
	while (table_size < entries.size() * 2u) {
		table_size <<= 1u;
	}
	g.mask = table_size - 1u;
	g.bucket_start.assign(table_size + 1u, 0u);
	for (const Entry &en : entries) {
		g.bucket_start[(SpatialHash::hash_cell(en.cx, en.cy) & g.mask) + 1u]++;
	}
	for (uint32_t b = 0; b < table_size; b++) {
		g.bucket_start[b + 1u] += g.bucket_start[b];
	}
	g.cell_x.resize(entries.size());
	g.cell_y.resize(entries.size());
	g.obstacle.resize(entries.size());
	std::vector<uint32_t> cursor(g.bucket_start.begin(), g.bucket_start.end() - 1);
	for (const Entry &en : entries) {
		const uint32_t e = cursor[SpatialHash::hash_cell(en.cx, en.cy) & g.mask]++;
		g.cell_x[e] = en.cx;
		g.cell_y[e] = en.cy;
		g.obstacle[e] = en.obstacle;
	}
	obstacle_grid_valid_ = true;
}

template <typename F>
void Simulation::for_each_obstacle_near(int32_t p_cx, int32_t p_cy, F &&p_visit) const {
	const ObstacleGrid &g = obstacle_grid_;
	for (uint32_t o : g.large) {
		p_visit(o);
	}
	const uint32_t bucket = SpatialHash::hash_cell(p_cx, p_cy) & g.mask;
	const uint32_t end = g.bucket_start[bucket + 1u];
	for (uint32_t e = g.bucket_start[bucket]; e < end; e++) {
		if (g.cell_x[e] == p_cx && g.cell_y[e] == p_cy) {
			p_visit(g.obstacle[e]);
		}
	}
}

// --- Agents ----------------------------------------------------------------------

AgentId Simulation::spawn_one(uint16_t p_type, Vec2 p_position) {
	if (p_type >= types_.size() || !std::isfinite(p_position.x) || !std::isfinite(p_position.y)) {
		return INVALID_AGENT_ID;
	}
	const uint32_t dense = dense_size();
	const AgentId id = handles_.allocate(dense);
	if (id == INVALID_AGENT_ID) {
		return INVALID_AGENT_ID;
	}
	px_.push_back(p_position.x);
	py_.push_back(p_position.y);
	prev_x_.push_back(p_position.x);
	prev_y_.push_back(p_position.y);
	vx_.push_back(0.0f);
	vy_.push_back(0.0f);
	hp_.push_back(types_[p_type].max_hp);
	phase_.push_back(rng_.next_float());
	type_.push_back(p_type);
	alive_.push_back(1);
	slot_.push_back(HandleTable::slot_of(id));
	alive_count_++;
	return id;
}

AgentId Simulation::spawn(uint16_t p_type, Vec2 p_position) {
	return spawn_one(p_type, p_position);
}

void Simulation::spawn(uint16_t p_type, Vec2 p_position, uint32_t p_count, std::vector<AgentId> &r_ids) {
	if (p_type >= types_.size() || p_count == 0) {
		return;
	}
	// Disc large enough for the agents to start roughly side by side.
	const float spread = p_count > 1 ? types_[p_type].radius * 1.2f * std::sqrt(static_cast<float>(p_count)) : 0.0f;
	for (uint32_t k = 0; k < p_count; k++) {
		Vec2 position = p_position;
		if (spread > 0.0f) {
			const float angle = rng_.next_float() * TWO_PI;
			const float distance = spread * std::sqrt(rng_.next_float());
			position = position + Vec2(std::cos(angle), std::sin(angle)) * distance;
		}
		const AgentId id = spawn_one(p_type, position);
		if (id == INVALID_AGENT_ID) {
			return;
		}
		r_ids.push_back(id);
	}
}

void Simulation::spawn_ring(uint16_t p_type, Vec2 p_center, float p_radius, uint32_t p_count, std::vector<AgentId> &r_ids) {
	if (p_type >= types_.size() || p_count == 0) {
		return;
	}
	const float start = rng_.next_float() * TWO_PI;
	const float step_angle = TWO_PI / static_cast<float>(p_count);
	for (uint32_t k = 0; k < p_count; k++) {
		const float angle = start + step_angle * static_cast<float>(k);
		const AgentId id = spawn_one(p_type, p_center + Vec2(std::cos(angle), std::sin(angle)) * p_radius);
		if (id == INVALID_AGENT_ID) {
			return;
		}
		r_ids.push_back(id);
	}
}

void Simulation::kill_dense(uint32_t p_dense_index) {
	const AgentId id = handles_.id_for_slot(slot_[p_dense_index]);
	handles_.release(id);
	alive_[p_dense_index] = 0;
	alive_count_--;
	dead_count_++;
	deaths_.push_back(DeathEvent{ id, Vec2(px_[p_dense_index], py_[p_dense_index]), type_[p_dense_index] });
}

bool Simulation::kill(AgentId p_id) {
	if (!handles_.is_valid(p_id)) {
		return false;
	}
	kill_dense(handles_.dense_index(p_id));
	return true;
}

bool Simulation::damage(AgentId p_id, float p_amount) {
	if (std::isnan(p_amount) || !handles_.is_valid(p_id)) {
		return false;
	}
	const uint32_t d = handles_.dense_index(p_id);
	hp_[d] = std::min(hp_[d] - p_amount, types_[type_[d]].max_hp);
	if (hp_[d] <= 0.0f) {
		kill_dense(d);
	}
	return true;
}

void Simulation::damage_circle(Vec2 p_center, float p_radius, float p_amount, std::vector<AgentId> &r_hit_ids) {
	if (std::isnan(p_amount)) {
		return;
	}
	const size_t first = r_hit_ids.size();
	query_circle(p_center, p_radius, r_hit_ids);
	// Kills only flag agents, so the ids gathered above stay addressable while looping.
	for (size_t k = first; k < r_hit_ids.size(); k++) {
		damage(r_hit_ids[k], p_amount);
	}
}

void Simulation::query_circle(Vec2 p_center, float p_radius, std::vector<AgentId> &r_ids) {
	if (!(p_radius >= 0.0f) || !std::isfinite(p_center.x) || !std::isfinite(p_center.y)) {
		return;
	}
	ensure_hash();
	// Expand the searched box slightly so float rounding in the overlap test can never
	// accept an agent whose cell was not visited.
	const float reach = p_radius + max_radius_;
	const float margin = reach * 1e-5f + (std::fabs(p_center.x) + std::fabs(p_center.y) + reach) * 1e-6f + 1e-3f;
	const Vec2 extent(reach + margin, reach + margin);
	const uint32_t *indices = hash_.indices();
	const float *xs = hash_.xs();
	const float *ys = hash_.ys();
	hash_.for_each_in_box(p_center - extent, p_center + extent, [&](uint32_t e) {
		const uint32_t i = indices[e];
		if (alive_[i] && circles_overlap(p_center, p_radius, Vec2(xs[e], ys[e]), types_[type_[i]].radius)) {
			r_ids.push_back(id_at(i));
		}
	});
	const uint32_t n = dense_size();
	for (uint32_t i = hashed_count_; i < n; i++) {
		if (alive_[i] && circles_overlap(p_center, p_radius, Vec2(px_[i], py_[i]), types_[type_[i]].radius)) {
			r_ids.push_back(id_at(i));
		}
	}
}

void Simulation::query_circle_brute_force(Vec2 p_center, float p_radius, std::vector<AgentId> &r_ids) const {
	if (!(p_radius >= 0.0f) || !std::isfinite(p_center.x) || !std::isfinite(p_center.y)) {
		return;
	}
	const uint32_t n = dense_size();
	for (uint32_t i = 0; i < n; i++) {
		if (alive_[i] && circles_overlap(p_center, p_radius, Vec2(px_[i], py_[i]), types_[type_[i]].radius)) {
			r_ids.push_back(id_at(i));
		}
	}
}

bool Simulation::get_position(AgentId p_id, Vec2 &r_position) const {
	if (!handles_.is_valid(p_id)) {
		return false;
	}
	const uint32_t d = handles_.dense_index(p_id);
	r_position = Vec2(px_[d], py_[d]);
	return true;
}

bool Simulation::get_velocity(AgentId p_id, Vec2 &r_velocity) const {
	if (!handles_.is_valid(p_id)) {
		return false;
	}
	const uint32_t d = handles_.dense_index(p_id);
	r_velocity = Vec2(vx_[d], vy_[d]);
	return true;
}

bool Simulation::get_hp(AgentId p_id, float &r_hp) const {
	if (!handles_.is_valid(p_id)) {
		return false;
	}
	r_hp = hp_[handles_.dense_index(p_id)];
	return true;
}

bool Simulation::get_type_of(AgentId p_id, uint16_t &r_type) const {
	if (!handles_.is_valid(p_id)) {
		return false;
	}
	r_type = type_[handles_.dense_index(p_id)];
	return true;
}

void Simulation::clear() {
	handles_.clear();
	px_.clear();
	py_.clear();
	prev_x_.clear();
	prev_y_.clear();
	vx_.clear();
	vy_.clear();
	hp_.clear();
	phase_.clear();
	type_.clear();
	alive_.clear();
	slot_.clear();
	alive_count_ = 0;
	dead_count_ = 0;
	hash_valid_ = false;
	hashed_count_ = 0;
	contacts_.clear();
}

// --- Stepping --------------------------------------------------------------------

void Simulation::compact() {
	if (dead_count_ == 0) {
		return;
	}
	const uint32_t n = dense_size();
	uint32_t w = 0;
	for (uint32_t r = 0; r < n; r++) {
		if (!alive_[r]) {
			handles_.recycle(slot_[r]);
			continue;
		}
		if (w != r) {
			px_[w] = px_[r];
			py_[w] = py_[r];
			prev_x_[w] = prev_x_[r];
			prev_y_[w] = prev_y_[r];
			vx_[w] = vx_[r];
			vy_[w] = vy_[r];
			hp_[w] = hp_[r];
			phase_[w] = phase_[r];
			type_[w] = type_[r];
			alive_[w] = 1;
			slot_[w] = slot_[r];
			handles_.set_dense_index(slot_[r], w);
		}
		w++;
	}
	px_.resize(w);
	py_.resize(w);
	prev_x_.resize(w);
	prev_y_.resize(w);
	vx_.resize(w);
	vy_.resize(w);
	hp_.resize(w);
	phase_.resize(w);
	type_.resize(w);
	alive_.resize(w);
	slot_.resize(w);
	dead_count_ = 0;
	hash_valid_ = false;
	hashed_count_ = 0;
}

void Simulation::ensure_hash() {
	if (!hash_valid_ || hashed_count_ > dense_size()) {
		rebuild_hash();
		return;
	}
	const uint32_t tail = dense_size() - hashed_count_;
	const uint32_t tail_limit = std::max<uint32_t>(64u, hashed_count_ / 8u);
	if (tail > tail_limit) {
		rebuild_hash();
	}
}

void Simulation::rebuild_hash() {
	const uint32_t n = dense_size();
	hash_.build(px_.data(), py_.data(), n, cell_size_);
	sorted_radius_.resize(n);
	const uint32_t *indices = hash_.indices();
	for (uint32_t e = 0; e < n; e++) {
		sorted_radius_[e] = types_[type_[indices[e]]].radius;
	}
	hashed_count_ = n;
	hash_valid_ = true;
	stats_.hash_rebuilds++;
}

uint32_t Simulation::advance(float p_delta) {
	uint32_t steps = 0;
	if (p_delta > 0.0f && std::isfinite(p_delta)) {
		if (fixed_dt_ <= 0.0f) {
			step(p_delta);
			steps = 1;
			alpha_ = 1.0f;
		} else {
			accumulator_ += static_cast<double>(p_delta);
			const double dt = static_cast<double>(fixed_dt_);
			while (accumulator_ >= dt && steps < max_substeps_) {
				step(fixed_dt_);
				accumulator_ -= dt;
				steps++;
			}
			if (accumulator_ >= dt) {
				// Too far behind: drop whole steps instead of spiralling.
				accumulator_ = std::fmod(accumulator_, dt);
			}
			alpha_ = static_cast<float>(accumulator_ / dt);
		}
	}
	stats_.substeps = steps;
	update_contacts();
	return steps;
}

void Simulation::step(float p_dt) {
	if (!(p_dt > 0.0f) || !std::isfinite(p_dt)) {
		return;
	}
	compact();
	if (!obstacle_grid_valid_) {
		rebuild_obstacle_grid();
	}
	if (!hash_valid_ || hashed_count_ != dense_size()) {
		rebuild_hash();
	}
	prev_x_ = px_;
	prev_y_ = py_;
	compute_steering(p_dt);
	integrate_and_constrain(p_dt);
	hash_valid_ = false;
}

void Simulation::compute_steering(float p_dt) {
	const uint32_t n = hash_.size();
	const uint32_t *indices = hash_.indices();
	const float *xs = hash_.xs();
	const float *ys = hash_.ys();
	const int32_t *cxs = hash_.cell_xs();
	const int32_t *cys = hash_.cell_ys();
	const float *radii = sorted_radius_.data();
	const int32_t ring = neighbor_ring_;
	uint64_t checks = 0;

	// Distance to the target per hash entry: decides who yields when two agents overlap.
	const bool prioritize = target_enabled_;
	sorted_target_distance_.resize(n);
	if (prioritize) {
		for (uint32_t e = 0; e < n; e++) {
			const float tx = target_.x - xs[e];
			const float ty = target_.y - ys[e];
			sorted_target_distance_[e] = std::sqrt(tx * tx + ty * ty);
		}
	}
	const float *target_distance = sorted_target_distance_.data();
	correction_x_.assign(n, 0.0f);
	correction_y_.assign(n, 0.0f);

	for (uint32_t e = 0; e < n; e++) {
		const uint32_t i = indices[e];
		const TypeParams &tp = types_[type_[i]];
		const float xi = xs[e];
		const float yi = ys[e];
		const float ri = radii[e];

		// Separation: push away from every overlapping neighbour, stronger when deeper.
		// The same overlaps feed a positional correction in which the agent farther from
		// the target yields, so the pressure of a crowd seeking one point does not pile
		// up towards the middle (with a symmetric split the core collapses).
		float sep_x = 0.0f;
		float sep_y = 0.0f;
		float corr_x = 0.0f;
		float corr_y = 0.0f;
		if (tp.separation_weight != 0.0f && ri > 0.0f) {
			const int32_t cx = cxs[e];
			const int32_t cy = cys[e];
			for (int32_t oy = -ring; oy <= ring; oy++) {
				for (int32_t ox = -ring; ox <= ring; ox++) {
					const int32_t ncx = cx + ox;
					const int32_t ncy = cy + oy;
					const uint32_t bucket = hash_.bucket_of(ncx, ncy);
					const uint32_t end = hash_.bucket_end(bucket);
					for (uint32_t f = hash_.bucket_begin(bucket); f < end; f++) {
						if (f == e || cxs[f] != ncx || cys[f] != ncy) {
							continue;
						}
						checks++;
						const float dx = xi - xs[f];
						const float dy = yi - ys[f];
						const float reach = ri + radii[f];
						const float d2 = dx * dx + dy * dy;
						if (d2 >= reach * reach) {
							continue;
						}
						float d = 0.0f;
						float nx;
						float ny;
						if (d2 > COINCIDENT_DISTANCE_SQ) {
							d = std::sqrt(d2);
							nx = dx / d;
							ny = dy / d;
						} else {
							const Vec2 dir = coincident_direction(slot_[i], slot_[indices[f]]);
							nx = dir.x;
							ny = dir.y;
						}
						const float overlap = reach - d;
						const float push = overlap / reach;
						sep_x += nx * push;
						sep_y += ny * push;
						float share = 0.5f;
						if (prioritize) {
							share = std::min(std::max(0.5f + (target_distance[e] - target_distance[f]) / reach, 0.0f), 1.0f);
						}
						corr_x += nx * overlap * share;
						corr_y += ny * overlap * share;
					}
				}
			}
			corr_x *= overlap_resolution_;
			corr_y *= overlap_resolution_;
			clamp_length(corr_x, corr_y, ri);
		}
		correction_x_[i] = corr_x;
		correction_y_[i] = corr_y;

		// Seek: unit vector towards the target.
		float seek_x = 0.0f;
		float seek_y = 0.0f;
		if (target_enabled_ && tp.seek_weight != 0.0f) {
			const float tx = target_.x - xi;
			const float ty = target_.y - yi;
			const float t2 = tx * tx + ty * ty;
			if (t2 > COINCIDENT_DISTANCE_SQ) {
				const float inv = 1.0f / std::sqrt(t2);
				seek_x = tx * inv;
				seek_y = ty * inv;
			}
		}

		// Desired velocity, reached with bounded acceleration.
		float desired_x = (seek_x * tp.seek_weight + sep_x * tp.separation_weight) * tp.max_speed;
		float desired_y = (seek_y * tp.seek_weight + sep_y * tp.separation_weight) * tp.max_speed;
		clamp_length(desired_x, desired_y, tp.max_speed);
		float steer_x = desired_x - vx_[i];
		float steer_y = desired_y - vy_[i];
		clamp_length(steer_x, steer_y, tp.acceleration * p_dt);
		float vx = vx_[i] + steer_x;
		float vy = vy_[i] + steer_y;
		clamp_length(vx, vy, tp.max_speed);
		vx_[i] = vx;
		vy_[i] = vy;
	}
	stats_.neighbor_checks = checks;
}

void Simulation::integrate_and_constrain(float p_dt) {
	const uint32_t n = dense_size();
	const bool has_obstacles = !obstacles_.empty();
	const bool solid_target = target_enabled_ && target_solid_ && target_radius_ > 0.0f;
	const float inv_cell = 1.0f / cell_size_;
	for (uint32_t i = 0; i < n; i++) {
		const float r = types_[type_[i]].radius;
		float vx = vx_[i];
		float vy = vy_[i];
		float x = px_[i] + vx * p_dt + correction_x_[i];
		float y = py_[i] + vy * p_dt + correction_y_[i];
		if (has_obstacles) {
			const int32_t cx = cell_coord(x, inv_cell);
			const int32_t cy = cell_coord(y, inv_cell);
			for_each_obstacle_near(cx, cy, [&](uint32_t o) {
				push_out(obstacles_[o].center, obstacles_[o].radius + r, x, y, vx, vy);
			});
		}
		if (solid_target) {
			push_out(target_, target_radius_ + r, x, y, vx, vy);
		}
		if (bounds_enabled_) {
			const Vec2 lo = bounds_.position;
			const Vec2 hi = bounds_.end();
			const float min_x = std::min(lo.x + r, (lo.x + hi.x) * 0.5f);
			const float max_x = std::max(hi.x - r, (lo.x + hi.x) * 0.5f);
			const float min_y = std::min(lo.y + r, (lo.y + hi.y) * 0.5f);
			const float max_y = std::max(hi.y - r, (lo.y + hi.y) * 0.5f);
			if (x < min_x) {
				x = min_x;
				vx = std::max(vx, 0.0f);
			} else if (x > max_x) {
				x = max_x;
				vx = std::min(vx, 0.0f);
			}
			if (y < min_y) {
				y = min_y;
				vy = std::max(vy, 0.0f);
			} else if (y > max_y) {
				y = max_y;
				vy = std::min(vy, 0.0f);
			}
		}
		px_[i] = x;
		py_[i] = y;
		vx_[i] = vx;
		vy_[i] = vy;
	}
}

void Simulation::update_contacts() {
	contacts_.clear();
	if (!target_enabled_ || target_radius_ <= 0.0f) {
		return;
	}
	const uint32_t n = dense_size();
	for (uint32_t i = 0; i < n; i++) {
		if (!alive_[i]) {
			continue;
		}
		const float reach = target_radius_ + types_[type_[i]].radius + contact_margin_;
		const float dx = px_[i] - target_.x;
		const float dy = py_[i] - target_.y;
		if (dx * dx + dy * dy <= reach * reach) {
			contacts_.push_back(id_at(i));
		}
	}
}

} // namespace horde
