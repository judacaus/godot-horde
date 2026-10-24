// Engine-agnostic horde simulation.
//
// Agents are stored densely in structure-of-arrays form and addressed from the outside
// through stable generational AgentIds (see handle_table.hpp). Each step:
//
//   1. Dead agents are compacted away (kills only flag agents, so ids stay cheap to
//      invalidate and queries between steps never need a rebuild because of a kill).
//   2. The spatial hash is rebuilt if positions or membership changed.
//   3. Steering: velocity moves towards seek + separation, limited by acceleration and
//      max speed.
//   4. Integration, then constraints: static circular obstacles, the solid target and
//      the rectangular bounds push agents out and cancel the velocity into them.
//
// With the same inputs, call order and build, results are bit-identical: there is no
// global state, no unordered containers in the hot path, and randomness comes from a
// seeded PCG32. Cross-platform bit equality is not guaranteed (libm, compilers).
#pragma once

#include "core/handle_table.hpp"
#include "core/math.hpp"
#include "core/rng.hpp"
#include "core/spatial_hash.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace horde {

struct TypeParams {
	float radius = 8.0f;
	float max_speed = 80.0f;
	float acceleration = 600.0f;
	float max_hp = 10.0f;
	float separation_weight = 1.5f;
	float seek_weight = 1.0f;
};

struct DeathEvent {
	AgentId id = INVALID_AGENT_ID;
	Vec2 position;
	uint16_t type = 0;
};

struct Obstacle {
	Vec2 center;
	float radius = 0.0f;
};

struct StepStats {
	uint32_t substeps = 0; // Steps run by the last advance().
	uint64_t neighbor_checks = 0; // Pair distance tests in the last step.
	uint32_t hash_rebuilds = 0; // Cumulative.
};

class Simulation {
public:
	static constexpr uint16_t MAX_TYPES = 65535;
	static constexpr uint32_t INVALID_OBSTACLE = 0xFFFFFFFFu;

	Simulation();

	// --- Agent types -------------------------------------------------------------
	// Returns the new type index, or MAX_TYPES if there are too many types.
	uint16_t add_type(const TypeParams &p_params);
	void set_type(uint16_t p_type, const TypeParams &p_params);
	const TypeParams &get_type(uint16_t p_type) const { return types_[p_type]; }
	uint32_t type_count() const { return static_cast<uint32_t>(types_.size()); }

	// --- World configuration -----------------------------------------------------
	void set_seed(uint64_t p_seed) { rng_.seed(p_seed); }

	void set_target(Vec2 p_target) { target_ = p_target; }
	Vec2 get_target() const { return target_; }
	void set_target_enabled(bool p_enabled) { target_enabled_ = p_enabled; }
	bool is_target_enabled() const { return target_enabled_; }
	// Agents within target_radius + agent radius + contact_margin are reported as contacts.
	// Non-finite values are ignored: an infinite solid target would push agents to infinity.
	void set_target_radius(float p_radius) {
		if (std::isfinite(p_radius)) {
			target_radius_ = p_radius > 0.0f ? p_radius : 0.0f;
		}
	}
	float get_target_radius() const { return target_radius_; }
	// A solid target pushes agents out like an obstacle of radius target_radius.
	void set_target_solid(bool p_solid) { target_solid_ = p_solid; }
	bool is_target_solid() const { return target_solid_; }
	void set_contact_margin(float p_margin) { contact_margin_ = p_margin > 0.0f ? p_margin : 0.0f; }
	float get_contact_margin() const { return contact_margin_; }

	// Fraction of the overlap between two agents removed per step by moving them apart
	// (the agent farther from the target moves more). 0 disables it: only the soft
	// separation steering keeps agents apart.
	void set_overlap_resolution(float p_fraction) { overlap_resolution_ = std::min(std::max(p_fraction, 0.0f), 1.0f); }
	float get_overlap_resolution() const { return overlap_resolution_; }

	// 0 means automatic: twice the largest agent radius. Non-finite values are ignored.
	void set_cell_size(float p_cell_size);
	float get_cell_size_setting() const { return cell_size_setting_; }
	float get_effective_cell_size() const { return cell_size_; }

	// Enables the bounds. Rectangles with non-finite values are ignored.
	void set_bounds(const Rect2f &p_bounds);
	void disable_bounds() { bounds_enabled_ = false; }
	bool has_bounds() const { return bounds_enabled_; }
	Rect2f get_bounds() const { return bounds_; }

	// Returns the obstacle index, or INVALID_OBSTACLE if the center or radius is not finite.
	uint32_t add_obstacle(Vec2 p_center, float p_radius);
	void clear_obstacles();
	const std::vector<Obstacle> &get_obstacles() const { return obstacles_; }

	// --- Time ----------------------------------------------------------------------
	// 0 disables the fixed timestep: advance() then runs exactly one step of `delta`.
	void set_fixed_timestep(float p_dt) { fixed_dt_ = p_dt > 0.0f ? p_dt : 0.0f; }
	float get_fixed_timestep() const { return fixed_dt_; }
	void set_max_substeps(uint32_t p_max) { max_substeps_ = p_max > 0 ? p_max : 1; }
	uint32_t get_max_substeps() const { return max_substeps_; }

	// Advances by `p_delta` seconds (variable or fixed-step) and refreshes contacts().
	// Returns the number of simulation steps run.
	uint32_t advance(float p_delta);
	// Runs exactly one simulation step of `p_dt` seconds. Does not refresh contacts().
	void step(float p_dt);
	// Interpolation factor between previous and current positions after advance().
	float get_interpolation_alpha() const { return alpha_; }

	// --- Agents ----------------------------------------------------------------------
	AgentId spawn(uint16_t p_type, Vec2 p_position);
	// Spawns `p_count` agents; for count > 1 they are scattered uniformly inside a disc
	// sized for the type's radius so they don't start fully overlapped.
	void spawn(uint16_t p_type, Vec2 p_position, uint32_t p_count, std::vector<AgentId> &r_ids);
	// Spawns `p_count` agents evenly on a circle, starting at a random angle.
	void spawn_ring(uint16_t p_type, Vec2 p_center, float p_radius, uint32_t p_count, std::vector<AgentId> &r_ids);

	// Kills an agent. Returns false for stale or invalid ids. The death is queued in
	// pending_deaths(); the id is stale immediately.
	bool kill(AgentId p_id);
	// Subtracts hp from a single agent; kills it at hp <= 0. Returns false if stale or
	// if the amount is NaN (which would leave the agent with NaN hp, impossible to kill).
	bool damage(AgentId p_id, float p_amount);
	// Damages every agent overlapping the circle; appends hit ids (killed ones too).
	// A NaN amount damages and appends nothing.
	void damage_circle(Vec2 p_center, float p_radius, float p_amount, std::vector<AgentId> &r_hit_ids);
	// Appends every live agent whose body overlaps the circle. Order is unspecified
	// but deterministic. An infinite radius returns every live agent.
	void query_circle(Vec2 p_center, float p_radius, std::vector<AgentId> &r_ids);
	// Reference implementation of query_circle used by tests: linear scan.
	void query_circle_brute_force(Vec2 p_center, float p_radius, std::vector<AgentId> &r_ids) const;

	bool is_alive(AgentId p_id) const { return handles_.is_valid(p_id); }
	bool get_position(AgentId p_id, Vec2 &r_position) const;
	bool get_velocity(AgentId p_id, Vec2 &r_velocity) const;
	bool get_hp(AgentId p_id, float &r_hp) const;
	bool get_type_of(AgentId p_id, uint16_t &r_type) const;
	uint32_t agent_count() const { return alive_count_; }

	// Removes every agent without reporting deaths. Already pending deaths are kept.
	void clear();

	// --- Events ----------------------------------------------------------------------
	std::vector<DeathEvent> &pending_deaths() { return deaths_; }
	// Agents touching the target after the last advance().
	const std::vector<AgentId> &contacts() const { return contacts_; }

	// --- Dense data (rendering, debugging) ------------------------------------------
	// Includes agents killed since the last step; check alive_flags().
	uint32_t dense_size() const { return static_cast<uint32_t>(px_.size()); }
	const float *pos_x() const { return px_.data(); }
	const float *pos_y() const { return py_.data(); }
	const float *prev_pos_x() const { return prev_x_.data(); }
	const float *prev_pos_y() const { return prev_y_.data(); }
	const float *vel_x() const { return vx_.data(); }
	const float *vel_y() const { return vy_.data(); }
	const float *hp() const { return hp_.data(); }
	const float *anim_phase() const { return phase_.data(); }
	const uint16_t *types() const { return type_.data(); }
	const uint8_t *alive_flags() const { return alive_.data(); }
	AgentId id_at(uint32_t p_dense_index) const { return handles_.id_for_slot(slot_[p_dense_index]); }

	const StepStats &get_stats() const { return stats_; }
	const HandleTable &handles() const { return handles_; }

private:
	struct ObstacleGrid {
		std::vector<uint32_t> bucket_start;
		std::vector<int32_t> cell_x;
		std::vector<int32_t> cell_y;
		std::vector<uint32_t> obstacle;
		std::vector<uint32_t> large; // Obstacles covering too many cells; checked always.
		uint32_t mask = 0;
	};

	AgentId spawn_one(uint16_t p_type, Vec2 p_position);
	void kill_dense(uint32_t p_dense_index);
	void compact();
	void ensure_hash();
	void rebuild_hash();
	void rebuild_obstacle_grid();
	void update_cell_size();
	void compute_steering(float p_dt);
	void integrate_and_constrain(float p_dt);
	void update_contacts();
	template <typename F>
	void for_each_obstacle_near(int32_t p_cx, int32_t p_cy, F &&p_visit) const;

	std::vector<TypeParams> types_;
	float max_radius_ = 0.0f;
	HandleTable handles_;

	// Dense structure of arrays.
	std::vector<float> px_, py_;
	std::vector<float> prev_x_, prev_y_;
	std::vector<float> vx_, vy_;
	std::vector<float> hp_;
	std::vector<float> phase_;
	std::vector<uint16_t> type_;
	std::vector<uint8_t> alive_;
	std::vector<uint32_t> slot_;
	uint32_t alive_count_ = 0;
	uint32_t dead_count_ = 0;

	// Spatial hash over dense indices [0, hashed_count_). Agents spawned after the
	// last rebuild form a "tail" that queries scan linearly until the next rebuild.
	SpatialHash hash_;
	std::vector<float> sorted_radius_;
	std::vector<float> sorted_target_distance_;
	std::vector<float> correction_x_, correction_y_; // Dense, per step.
	float overlap_resolution_ = 0.8f;
	bool hash_valid_ = false;
	uint32_t hashed_count_ = 0;
	float cell_size_setting_ = 0.0f;
	float cell_size_ = 16.0f;
	int32_t neighbor_ring_ = 1;

	Vec2 target_;
	bool target_enabled_ = true;
	float target_radius_ = 0.0f;
	bool target_solid_ = true;
	float contact_margin_ = 1.0f;

	bool bounds_enabled_ = false;
	Rect2f bounds_;

	std::vector<Obstacle> obstacles_;
	ObstacleGrid obstacle_grid_;
	bool obstacle_grid_valid_ = true;

	float fixed_dt_ = 0.0f;
	uint32_t max_substeps_ = 8;
	double accumulator_ = 0.0;
	float alpha_ = 1.0f;

	Pcg32 rng_;
	std::vector<DeathEvent> deaths_;
	std::vector<AgentId> contacts_;
	StepStats stats_;
};

} // namespace horde
