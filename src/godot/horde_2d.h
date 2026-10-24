#pragma once

#include "core/simulation.hpp"
#include "godot/horde_type.h"

#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/classes/quad_mesh.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>

#include <vector>

namespace godot {

// A 2D swarm of lightweight agents simulated in C++ and drawn with one MultiMesh per
// HordeType. All positions are in this node's local coordinates.
class Horde2D : public Node2D {
	GDCLASS(Horde2D, Node2D)

public:
	enum UpdateMode {
		UPDATE_PROCESS,
		UPDATE_PHYSICS,
		UPDATE_MANUAL,
	};

	Horde2D();
	~Horde2D() override;

	// --- Agents ----------------------------------------------------------------------
	PackedInt32Array spawn(const Ref<HordeType> &p_type, const Vector2 &p_position, int p_count = 1);
	PackedInt32Array spawn_ring(const Ref<HordeType> &p_type, const Vector2 &p_center, float p_radius, int p_count);
	bool kill(int p_id);
	bool damage(int p_id, float p_amount);
	PackedInt32Array damage_circle(const Vector2 &p_center, float p_radius, float p_amount);
	PackedInt32Array query_circle(const Vector2 &p_center, float p_radius);
	bool is_agent_alive(int p_id) const;
	Vector2 get_agent_position(int p_id) const;
	Vector2 get_agent_velocity(int p_id) const;
	float get_agent_hp(int p_id) const;
	Ref<HordeType> get_agent_type(int p_id) const;
	int get_agent_count() const;
	void clear();

	// Advances the simulation by `delta` seconds, redraws and emits signals. Called
	// automatically unless update_mode is UPDATE_MANUAL.
	void step(double p_delta);

	// --- Obstacles -------------------------------------------------------------------
	int add_obstacle(const Vector2 &p_center, float p_radius);
	void clear_obstacles();
	int get_obstacle_count() const;

	// --- Properties ------------------------------------------------------------------
	void set_target(Node2D *p_target);
	Node2D *get_target() const;
	void set_target_position(const Vector2 &p_position);
	Vector2 get_target_position() const { return target_position_; }
	void set_target_radius(float p_radius);
	float get_target_radius() const { return sim_.get_target_radius(); }
	void set_target_solid(bool p_solid);
	bool is_target_solid() const { return sim_.is_target_solid(); }

	void set_update_mode(UpdateMode p_mode);
	UpdateMode get_update_mode() const { return update_mode_; }
	void set_fixed_timestep(float p_dt);
	float get_fixed_timestep() const { return sim_.get_fixed_timestep(); }
	void set_max_substeps(int p_max);
	int get_max_substeps() const { return static_cast<int>(sim_.get_max_substeps()); }
	void set_interpolate(bool p_interpolate);
	bool is_interpolating() const { return interpolate_; }
	void set_seed(int64_t p_seed);
	int64_t get_seed() const { return seed_; }

	void set_cell_size(float p_cell_size);
	float get_cell_size() const { return sim_.get_cell_size_setting(); }
	void set_overlap_resolution(float p_fraction);
	float get_overlap_resolution() const { return sim_.get_overlap_resolution(); }

	void set_bounds_enabled(bool p_enabled);
	bool is_bounds_enabled() const { return bounds_enabled_; }
	void set_bounds(const Rect2 &p_bounds);
	Rect2 get_bounds() const { return bounds_; }

	void set_debug_draw(bool p_enabled);
	bool is_debug_draw_enabled() const { return debug_draw_; }
	void set_debug_draw_grid(bool p_enabled);
	bool is_debug_draw_grid_enabled() const { return debug_draw_grid_; }

	Dictionary get_debug_stats() const;

protected:
	static void _bind_methods();
	void _notification(int p_what);

private:
	struct TypeSlot {
		Ref<HordeType> type;
		horde::TypeParams params; // Last params pushed to the simulation.
		RID canvas_item;
		RID multimesh;
		RID texture; // Texture the draw command was recorded with.
		bool recorded = false;
		int capacity = 0;
		PackedFloat32Array buffer;
	};

	int find_or_register_type(const Ref<HordeType> &p_type);
	void sync_types();
	void sync_target();
	void update_processing();
	void ensure_render_resources();
	void free_render_resources();
	void apply_canvas_settings();
	void render();
	void emit_events(double p_simulated_time);
	PackedInt32Array to_packed(const std::vector<horde::AgentId> &p_ids) const;

	horde::Simulation sim_;
	std::vector<TypeSlot> types_;
	std::vector<horde::AgentId> scratch_ids_;
	std::vector<horde::DeathEvent> scratch_deaths_;
	std::vector<int> scratch_counts_;

	uint64_t target_id_ = 0;
	Vector2 target_position_;
	UpdateMode update_mode_ = UPDATE_PROCESS;
	bool interpolate_ = true;
	int64_t seed_ = 0;
	bool bounds_enabled_ = false;
	Rect2 bounds_ = Rect2(-512, -512, 1024, 1024);
	bool debug_draw_ = false;
	bool debug_draw_grid_ = true;

	Ref<QuadMesh> quad_;
	Ref<Shader> shader_;
	Ref<ShaderMaterial> material_;
	RID applied_material_;
	Rect2 agents_rect_;

	// Stats of the last update.
	uint64_t last_sim_usec_ = 0;
	uint64_t last_render_usec_ = 0;
	uint64_t last_total_usec_ = 0;
	uint32_t last_deaths_emitted_ = 0;
	uint32_t last_contacts_ = 0;
};

} // namespace godot

VARIANT_ENUM_CAST(Horde2D::UpdateMode);
