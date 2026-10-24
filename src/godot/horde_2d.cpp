#include "godot/horde_2d.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/font.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/theme_db.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace godot {

namespace {

// Per-instance layout written to the MultiMesh buffer (2D transform + color + custom).
constexpr int FLOATS_PER_INSTANCE = 16;
constexpr int MIN_CAPACITY = 64;
// Facing only flips when the horizontal speed is clearly non-zero, to avoid flicker.
constexpr float FLIP_SPEED_THRESHOLD = 1.0f;

// Instance custom data must stay small: the Compatibility renderer (also used on the
// web) stores instance color and custom data as half floats.
const char *const DEFAULT_SHADER_CODE = R"(shader_type canvas_item;

// godot-horde default sprite shader.
// INSTANCE_CUSTOM.x: frame offset, .y: frames per second, .z: horizontal frame count.
// The per-type modulate arrives as the instance color, already multiplied into COLOR.
void vertex() {
	float hframes = max(INSTANCE_CUSTOM.z, 1.0);
	float frame = mod(floor(TIME * INSTANCE_CUSTOM.y + INSTANCE_CUSTOM.x), hframes);
	UV.x = (UV.x + frame) / hframes;
}
)";

inline horde::Vec2 to_core(const Vector2 &p_v) {
	return horde::Vec2(static_cast<float>(p_v.x), static_cast<float>(p_v.y));
}

inline Vector2 to_godot(horde::Vec2 p_v) {
	return Vector2(p_v.x, p_v.y);
}

bool same_params(const horde::TypeParams &a, const horde::TypeParams &b) {
	return a.radius == b.radius && a.max_speed == b.max_speed && a.acceleration == b.acceleration &&
			a.max_hp == b.max_hp && a.separation_weight == b.separation_weight && a.seek_weight == b.seek_weight;
}

int capacity_for(int p_count) {
	int capacity = MIN_CAPACITY;
	while (capacity < p_count) {
		capacity *= 2;
	}
	return capacity;
}

uint64_t now_usec() {
	return Time::get_singleton()->get_ticks_usec();
}

} // namespace

Horde2D::Horde2D() {
	sim_.set_target_radius(16.0f);
	// Scenes don't store default-valued properties, so set_seed() may never run: seed the
	// simulation here so the default `seed` (0) is the sequence actually used.
	sim_.set_seed(static_cast<uint64_t>(seed_));
}

Horde2D::~Horde2D() {
	free_render_resources();
}

// --- Agents --------------------------------------------------------------------------

PackedInt32Array Horde2D::to_packed(const std::vector<horde::AgentId> &p_ids) const {
	PackedInt32Array out;
	out.resize(static_cast<int64_t>(p_ids.size()));
	if (!p_ids.empty()) {
		std::copy(p_ids.begin(), p_ids.end(), out.ptrw());
	}
	return out;
}

PackedInt32Array Horde2D::spawn(const Ref<HordeType> &p_type, const Vector2 &p_position, int p_count) {
	ERR_FAIL_COND_V_MSG(p_type.is_null(), PackedInt32Array(), "Horde2D.spawn(): the HordeType is null.");
	if (p_count <= 0) {
		return PackedInt32Array();
	}
	const int type = find_or_register_type(p_type);
	ERR_FAIL_COND_V(type < 0, PackedInt32Array());
	sync_types();
	scratch_ids_.clear();
	sim_.spawn(static_cast<uint16_t>(type), to_core(p_position), static_cast<uint32_t>(p_count), scratch_ids_);
	if (static_cast<int>(scratch_ids_.size()) < p_count) {
		WARN_PRINT_ONCE("Horde2D.spawn(): agent limit reached or invalid position; fewer agents were spawned than requested.");
	}
	return to_packed(scratch_ids_);
}

PackedInt32Array Horde2D::spawn_ring(const Ref<HordeType> &p_type, const Vector2 &p_center, float p_radius, int p_count) {
	ERR_FAIL_COND_V_MSG(p_type.is_null(), PackedInt32Array(), "Horde2D.spawn_ring(): the HordeType is null.");
	if (p_count <= 0) {
		return PackedInt32Array();
	}
	const int type = find_or_register_type(p_type);
	ERR_FAIL_COND_V(type < 0, PackedInt32Array());
	sync_types();
	scratch_ids_.clear();
	sim_.spawn_ring(static_cast<uint16_t>(type), to_core(p_center), p_radius, static_cast<uint32_t>(p_count), scratch_ids_);
	return to_packed(scratch_ids_);
}

bool Horde2D::kill(int p_id) {
	return sim_.kill(p_id);
}

bool Horde2D::damage(int p_id, float p_amount) {
	ERR_FAIL_COND_V_MSG(std::isnan(p_amount), false, "Horde2D.damage(): the amount is NaN.");
	return sim_.damage(p_id, p_amount);
}

PackedInt32Array Horde2D::damage_circle(const Vector2 &p_center, float p_radius, float p_amount) {
	ERR_FAIL_COND_V_MSG(std::isnan(p_amount), PackedInt32Array(), "Horde2D.damage_circle(): the amount is NaN.");
	scratch_ids_.clear();
	sim_.damage_circle(to_core(p_center), p_radius, p_amount, scratch_ids_);
	return to_packed(scratch_ids_);
}

PackedInt32Array Horde2D::query_circle(const Vector2 &p_center, float p_radius) {
	scratch_ids_.clear();
	sim_.query_circle(to_core(p_center), p_radius, scratch_ids_);
	return to_packed(scratch_ids_);
}

bool Horde2D::is_agent_alive(int p_id) const {
	return sim_.is_alive(p_id);
}

Vector2 Horde2D::get_agent_position(int p_id) const {
	horde::Vec2 p;
	return sim_.get_position(p_id, p) ? to_godot(p) : Vector2();
}

Vector2 Horde2D::get_agent_velocity(int p_id) const {
	horde::Vec2 v;
	return sim_.get_velocity(p_id, v) ? to_godot(v) : Vector2();
}

float Horde2D::get_agent_hp(int p_id) const {
	float hp = 0.0f;
	return sim_.get_hp(p_id, hp) ? hp : 0.0f;
}

Ref<HordeType> Horde2D::get_agent_type(int p_id) const {
	uint16_t type = 0;
	if (!sim_.get_type_of(p_id, type) || type >= types_.size()) {
		return Ref<HordeType>();
	}
	return types_[type].type;
}

int Horde2D::get_agent_count() const {
	return static_cast<int>(sim_.agent_count());
}

void Horde2D::clear() {
	sim_.clear();
	render();
}

void Horde2D::step(double p_delta) {
	const uint64_t t0 = now_usec();
	sync_types();
	sync_target();
	const uint32_t steps = sim_.advance(static_cast<float>(p_delta));
	// Time actually simulated by this update: whole fixed steps, or the frame delta.
	const float fixed_dt = sim_.get_fixed_timestep();
	const double simulated = fixed_dt > 0.0f ? static_cast<double>(steps) * fixed_dt : (steps > 0 ? p_delta : 0.0);
	const uint64_t t1 = now_usec();
	render();
	const uint64_t t2 = now_usec();
	last_sim_usec_ = t1 - t0;
	last_render_usec_ = t2 - t1;
	emit_events(simulated);
	last_total_usec_ = now_usec() - t0;
	if (debug_draw_) {
		queue_redraw();
	}
}

// --- Obstacles -----------------------------------------------------------------------

int Horde2D::add_obstacle(const Vector2 &p_center, float p_radius) {
	const uint32_t added = sim_.add_obstacle(to_core(p_center), p_radius);
	ERR_FAIL_COND_V_MSG(added == horde::Simulation::INVALID_OBSTACLE, -1, "Horde2D.add_obstacle(): the center and radius must be finite.");
	const int index = static_cast<int>(added);
	if (debug_draw_) {
		queue_redraw();
	}
	return index;
}

void Horde2D::clear_obstacles() {
	sim_.clear_obstacles();
	if (debug_draw_) {
		queue_redraw();
	}
}

int Horde2D::get_obstacle_count() const {
	return static_cast<int>(sim_.get_obstacles().size());
}

// --- Properties ----------------------------------------------------------------------

void Horde2D::set_target(Node2D *p_target) {
	target_id_ = p_target ? p_target->get_instance_id() : 0;
}

Node2D *Horde2D::get_target() const {
	if (target_id_ == 0) {
		return nullptr;
	}
	return Object::cast_to<Node2D>(ObjectDB::get_instance(target_id_));
}

void Horde2D::set_target_position(const Vector2 &p_position) {
	target_position_ = p_position;
}

void Horde2D::set_target_radius(float p_radius) {
	ERR_FAIL_COND_MSG(!std::isfinite(p_radius), "Horde2D.target_radius must be finite.");
	sim_.set_target_radius(p_radius);
}

void Horde2D::set_target_solid(bool p_solid) {
	sim_.set_target_solid(p_solid);
}

void Horde2D::set_update_mode(UpdateMode p_mode) {
	update_mode_ = p_mode;
	update_processing();
}

void Horde2D::set_fixed_timestep(float p_dt) {
	sim_.set_fixed_timestep(p_dt);
}

void Horde2D::set_max_substeps(int p_max) {
	sim_.set_max_substeps(static_cast<uint32_t>(MAX(p_max, 1)));
}

void Horde2D::set_interpolate(bool p_interpolate) {
	interpolate_ = p_interpolate;
}

void Horde2D::set_seed(int64_t p_seed) {
	seed_ = p_seed;
	sim_.set_seed(static_cast<uint64_t>(p_seed));
}

void Horde2D::set_cell_size(float p_cell_size) {
	ERR_FAIL_COND_MSG(!std::isfinite(p_cell_size), "Horde2D.cell_size must be finite.");
	sim_.set_cell_size(p_cell_size);
}

void Horde2D::set_overlap_resolution(float p_fraction) {
	sim_.set_overlap_resolution(p_fraction);
}

void Horde2D::set_bounds_enabled(bool p_enabled) {
	bounds_enabled_ = p_enabled;
	set_bounds(bounds_);
}

void Horde2D::set_bounds(const Rect2 &p_bounds) {
	ERR_FAIL_COND_MSG(!std::isfinite(p_bounds.position.x) || !std::isfinite(p_bounds.position.y) ||
					!std::isfinite(p_bounds.size.x) || !std::isfinite(p_bounds.size.y),
			"Horde2D.bounds must be finite.");
	bounds_ = p_bounds;
	if (bounds_enabled_) {
		const Rect2 r = bounds_.abs();
		sim_.set_bounds(horde::Rect2f{ to_core(r.position), to_core(r.size) });
	} else {
		sim_.disable_bounds();
	}
	if (debug_draw_) {
		queue_redraw();
	}
}

void Horde2D::set_debug_draw(bool p_enabled) {
	debug_draw_ = p_enabled;
	queue_redraw();
}

void Horde2D::set_debug_draw_grid(bool p_enabled) {
	debug_draw_grid_ = p_enabled;
	queue_redraw();
}

Dictionary Horde2D::get_debug_stats() const {
	Dictionary d;
	const horde::StepStats &stats = sim_.get_stats();
	d["agents"] = static_cast<int64_t>(sim_.agent_count());
	d["types"] = static_cast<int64_t>(types_.size());
	d["obstacles"] = static_cast<int64_t>(sim_.get_obstacles().size());
	d["cell_size"] = sim_.get_effective_cell_size();
	d["substeps"] = static_cast<int64_t>(stats.substeps);
	d["neighbor_checks"] = static_cast<int64_t>(stats.neighbor_checks);
	d["hash_rebuilds"] = static_cast<int64_t>(stats.hash_rebuilds);
	d["sim_usec"] = static_cast<int64_t>(last_sim_usec_);
	d["render_usec"] = static_cast<int64_t>(last_render_usec_);
	d["update_usec"] = static_cast<int64_t>(last_total_usec_);
	d["deaths_emitted"] = static_cast<int64_t>(last_deaths_emitted_);
	d["contacts"] = static_cast<int64_t>(last_contacts_);
	return d;
}

// --- Internals -----------------------------------------------------------------------

int Horde2D::find_or_register_type(const Ref<HordeType> &p_type) {
	for (size_t i = 0; i < types_.size(); i++) {
		if (types_[i].type == p_type) {
			return static_cast<int>(i);
		}
	}
	const uint16_t index = sim_.add_type(p_type->get_params());
	ERR_FAIL_COND_V_MSG(index == horde::Simulation::MAX_TYPES, -1, "Horde2D: too many HordeTypes.");

	ensure_render_resources();
	RenderingServer *rs = RenderingServer::get_singleton();
	TypeSlot slot;
	slot.type = p_type;
	slot.params = p_type->get_params();
	slot.canvas_item = rs->canvas_item_create();
	rs->canvas_item_set_parent(slot.canvas_item, get_canvas_item());
	slot.multimesh = rs->multimesh_create();
	rs->multimesh_set_mesh(slot.multimesh, quad_->get_rid());
	types_.push_back(slot);
	apply_canvas_settings();
	return static_cast<int>(index);
}

void Horde2D::sync_types() {
	for (size_t i = 0; i < types_.size(); i++) {
		TypeSlot &slot = types_[i];
		const horde::TypeParams &params = slot.type->get_params();
		if (!same_params(params, slot.params)) {
			sim_.set_type(static_cast<uint16_t>(i), params);
			slot.params = params;
		}
	}
}

void Horde2D::sync_target() {
	Node2D *target = get_target();
	if (target && target->is_inside_tree() && is_inside_tree()) {
		sim_.set_target(to_core(to_local(target->get_global_position())));
	} else {
		sim_.set_target(to_core(target_position_));
	}
}

void Horde2D::update_processing() {
	const bool runtime = !Engine::get_singleton()->is_editor_hint();
	set_process(runtime && update_mode_ == UPDATE_PROCESS);
	set_physics_process(runtime && update_mode_ == UPDATE_PHYSICS);
}

void Horde2D::ensure_render_resources() {
	if (quad_.is_valid()) {
		return;
	}
	quad_.instantiate();
	quad_->set_size(Vector2(1, 1));
	shader_.instantiate();
	shader_->set_code(DEFAULT_SHADER_CODE);
	material_.instantiate();
	material_->set_shader(shader_);
}

void Horde2D::free_render_resources() {
	RenderingServer *rs = RenderingServer::get_singleton();
	if (!rs) {
		return;
	}
	for (TypeSlot &slot : types_) {
		if (slot.canvas_item.is_valid()) {
			rs->free_rid(slot.canvas_item);
		}
		if (slot.multimesh.is_valid()) {
			rs->free_rid(slot.multimesh);
		}
		slot.canvas_item = RID();
		slot.multimesh = RID();
	}
}

void Horde2D::apply_canvas_settings() {
	if (types_.empty()) {
		return;
	}
	RenderingServer *rs = RenderingServer::get_singleton();

	// Child canvas items don't inherit the node's texture filter, so resolve it here.
	RenderingServer::CanvasItemTextureFilter filter = RenderingServer::CANVAS_ITEM_TEXTURE_FILTER_DEFAULT;
	Node *node = this;
	while (node) {
		CanvasItem *item = Object::cast_to<CanvasItem>(node);
		if (!item) {
			break;
		}
		if (item->get_texture_filter() != CanvasItem::TEXTURE_FILTER_PARENT_NODE) {
			filter = static_cast<RenderingServer::CanvasItemTextureFilter>(item->get_texture_filter());
			break;
		}
		node = node->get_parent();
	}

	// A material set on the node replaces the default sprite shader.
	const bool use_parent_material = get_material().is_valid();
	for (TypeSlot &slot : types_) {
		rs->canvas_item_set_default_texture_filter(slot.canvas_item, filter);
		rs->canvas_item_set_use_parent_material(slot.canvas_item, use_parent_material);
		rs->canvas_item_set_material(slot.canvas_item, use_parent_material ? RID() : material_->get_rid());
	}
	applied_material_ = use_parent_material ? get_material()->get_rid() : RID();
}

void Horde2D::render() {
	if (types_.empty()) {
		return;
	}
	RenderingServer *rs = RenderingServer::get_singleton();
	const size_t type_count = types_.size();
	const uint32_t n = sim_.dense_size();
	const uint8_t *alive = sim_.alive_flags();
	const uint16_t *agent_types = sim_.types();

	const RID wanted_material = get_material().is_valid() ? get_material()->get_rid() : RID();
	if (wanted_material != applied_material_) {
		apply_canvas_settings();
	}

	scratch_counts_.assign(type_count, 0);
	for (uint32_t i = 0; i < n; i++) {
		if (alive[i]) {
			scratch_counts_[agent_types[i]]++;
		}
	}

	struct Writer {
		float *out = nullptr;
		float w = 0.0f, h = 0.0f;
		float color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		float hframes = 1.0f, fps = 0.0f;
		float min_x = std::numeric_limits<float>::max(), min_y = std::numeric_limits<float>::max();
		float max_x = std::numeric_limits<float>::lowest(), max_y = std::numeric_limits<float>::lowest();
		int cursor = 0;
	};
	std::vector<Writer> writers(type_count);
	for (size_t t = 0; t < type_count; t++) {
		TypeSlot &slot = types_[t];
		const int count = scratch_counts_[t];
		// The whole buffer is uploaded every frame, so keep it at most twice the agents:
		// grow to the next power of two, shrink when mostly empty (hysteresis avoids
		// reallocating on every small change).
		if (count > slot.capacity || (slot.capacity > 4 * MIN_CAPACITY && count * 4 < slot.capacity)) {
			slot.capacity = capacity_for(count);
			rs->multimesh_allocate_data(slot.multimesh, slot.capacity, RenderingServer::MULTIMESH_TRANSFORM_2D, true, true);
			slot.buffer.resize(static_cast<int64_t>(slot.capacity) * FLOATS_PER_INSTANCE);
		}
		const Ref<HordeType> &type = slot.type;
		Writer &w = writers[t];
		w.out = slot.buffer.ptrw();
		const Vector2 frame = type->get_frame_size();
		w.w = static_cast<float>(frame.x);
		w.h = static_cast<float>(frame.y);
		const Color c = type->get_modulate();
		w.color[0] = c.r;
		w.color[1] = c.g;
		w.color[2] = c.b;
		w.color[3] = c.a;
		w.hframes = static_cast<float>(type->get_hframes());
		w.fps = type->get_fps();

		// The draw command references the MultiMesh RID, which survives reallocation;
		// it only needs re-recording when the texture changes.
		const RID texture = type->get_texture().is_valid() ? type->get_texture()->get_rid() : RID();
		if (!slot.recorded || texture != slot.texture) {
			rs->canvas_item_clear(slot.canvas_item);
			rs->canvas_item_add_multimesh(slot.canvas_item, slot.multimesh, texture);
			slot.texture = texture;
			slot.recorded = true;
		}
	}

	const bool interpolate = interpolate_ && sim_.get_fixed_timestep() > 0.0f;
	const float alpha = sim_.get_interpolation_alpha();
	const float *px = interpolate ? sim_.prev_pos_x() : sim_.pos_x();
	const float *py = interpolate ? sim_.prev_pos_y() : sim_.pos_y();
	const float *cx = sim_.pos_x();
	const float *cy = sim_.pos_y();
	const float *vx = sim_.vel_x();
	const float *phase = sim_.anim_phase();
	const float target_x = sim_.get_target().x;

	for (uint32_t i = 0; i < n; i++) {
		if (!alive[i]) {
			continue;
		}
		Writer &w = writers[agent_types[i]];
		float *o = w.out + static_cast<size_t>(w.cursor++) * FLOATS_PER_INSTANCE;
		const float x = interpolate ? px[i] + (cx[i] - px[i]) * alpha : cx[i];
		const float y = interpolate ? py[i] + (cy[i] - py[i]) * alpha : cy[i];
		float flip;
		if (vx[i] < -FLIP_SPEED_THRESHOLD) {
			flip = -1.0f;
		} else if (vx[i] > FLIP_SPEED_THRESHOLD) {
			flip = 1.0f;
		} else {
			flip = target_x < x ? -1.0f : 1.0f;
		}
		// Transform2D rows: (x.x, y.x, pad, origin.x), (x.y, y.y, pad, origin.y).
		// QuadMesh is built for 3D (+Y up), so Y is negated to show textures upright.
		o[0] = w.w * flip;
		o[1] = 0.0f;
		o[2] = 0.0f;
		o[3] = x;
		o[4] = 0.0f;
		o[5] = -w.h;
		o[6] = 0.0f;
		o[7] = y;
		o[8] = w.color[0];
		o[9] = w.color[1];
		o[10] = w.color[2];
		o[11] = w.color[3];
		o[12] = phase[i] * w.hframes;
		o[13] = w.fps;
		o[14] = w.hframes;
		o[15] = 0.0f;
		w.min_x = std::min(w.min_x, x);
		w.min_y = std::min(w.min_y, y);
		w.max_x = std::max(w.max_x, x);
		w.max_y = std::max(w.max_y, y);
	}

	Rect2 all_agents;
	bool has_agents = false;
	for (size_t t = 0; t < type_count; t++) {
		TypeSlot &slot = types_[t];
		const Writer &w = writers[t];
		const int count = scratch_counts_[t];
		// One buffer upload per type per frame.
		rs->multimesh_set_buffer(slot.multimesh, slot.buffer);
		rs->multimesh_set_visible_instances(slot.multimesh, count);
		AABB aabb(Vector3(0, 0, 0), Vector3(1, 1, 0));
		if (count > 0) {
			const float hw = std::fabs(w.w) * 0.5f;
			const float hh = std::fabs(w.h) * 0.5f;
			const Rect2 r(w.min_x - hw, w.min_y - hh, (w.max_x - w.min_x) + 2.0f * hw, (w.max_y - w.min_y) + 2.0f * hh);
			aabb = AABB(Vector3(r.position.x, r.position.y, 0), Vector3(MAX(r.size.x, 1.0f), MAX(r.size.y, 1.0f), 0));
			all_agents = has_agents ? all_agents.merge(r) : r;
			has_agents = true;
		}
		// A custom AABB also spares the renderer from recomputing it from the buffer.
		rs->multimesh_set_custom_aabb(slot.multimesh, aabb);
	}
	agents_rect_ = has_agents ? all_agents : Rect2();
}

void Horde2D::emit_events(double p_simulated_time) {
	std::vector<horde::DeathEvent> &pending = sim_.pending_deaths();
	last_deaths_emitted_ = 0;
	if (!pending.empty()) {
		// Take the events first: handlers may kill more agents while we emit.
		scratch_deaths_.clear();
		scratch_deaths_.swap(pending);
		PackedInt32Array ids;
		PackedVector2Array positions;
		ids.resize(static_cast<int64_t>(scratch_deaths_.size()));
		positions.resize(static_cast<int64_t>(scratch_deaths_.size()));
		int32_t *id_out = ids.ptrw();
		Vector2 *pos_out = positions.ptrw();
		for (size_t k = 0; k < scratch_deaths_.size(); k++) {
			id_out[k] = scratch_deaths_[k].id;
			pos_out[k] = to_godot(scratch_deaths_[k].position);
		}
		last_deaths_emitted_ = static_cast<uint32_t>(scratch_deaths_.size());
		scratch_deaths_.clear();
		emit_signal("agents_died", ids, positions);
	}
	// Contacts are reported only for updates that simulated time, together with that
	// time, so damage scaled by `delta` does not depend on the frame rate.
	const std::vector<horde::AgentId> &contacts = sim_.contacts();
	last_contacts_ = p_simulated_time > 0.0 ? static_cast<uint32_t>(contacts.size()) : 0;
	if (last_contacts_ > 0) {
		emit_signal("target_contact", to_packed(contacts), p_simulated_time);
	}
}

void Horde2D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			ensure_render_resources();
			apply_canvas_settings();
			update_processing();
		} break;
		case NOTIFICATION_PROCESS: {
			step(get_process_delta_time());
		} break;
		case NOTIFICATION_PHYSICS_PROCESS: {
			step(get_physics_process_delta_time());
		} break;
		case NOTIFICATION_DRAW: {
			if (!debug_draw_) {
				break;
			}
			const Color grid_color(0.3f, 0.8f, 1.0f, 0.25f);
			const Color shape_color(1.0f, 0.6f, 0.2f, 0.9f);
			const Color target_color(1.0f, 0.2f, 0.3f, 0.9f);
			if (debug_draw_grid_ && agents_rect_.has_area()) {
				const float cell = sim_.get_effective_cell_size();
				const Rect2 r = agents_rect_;
				const double x0 = std::floor(r.position.x / cell) * cell;
				const double y0 = std::floor(r.position.y / cell) * cell;
				const double x1 = r.get_end().x;
				const double y1 = r.get_end().y;
				// Skip the grid when it would be unreadable anyway.
				if ((x1 - x0) / cell < 400.0 && (y1 - y0) / cell < 400.0) {
					PackedVector2Array lines;
					for (double x = x0; x <= x1 + cell; x += cell) {
						lines.push_back(Vector2(x, y0));
						lines.push_back(Vector2(x, y1 + cell));
					}
					for (double y = y0; y <= y1 + cell; y += cell) {
						lines.push_back(Vector2(x0, y));
						lines.push_back(Vector2(x1 + cell, y));
					}
					draw_multiline(lines, grid_color);
				}
			}
			if (bounds_enabled_) {
				draw_rect(bounds_.abs(), shape_color, false, 2.0f);
			}
			for (const horde::Obstacle &o : sim_.get_obstacles()) {
				draw_circle(to_godot(o.center), o.radius, shape_color, false, 2.0f);
			}
			if (sim_.get_target_radius() > 0.0f) {
				draw_circle(to_godot(sim_.get_target()), sim_.get_target_radius(), target_color, false, 2.0f);
			}
		} break;
		case NOTIFICATION_PREDELETE: {
			free_render_resources();
		} break;
		default:
			break;
	}
}

void Horde2D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("spawn", "type", "position", "count"), &Horde2D::spawn, DEFVAL(1));
	ClassDB::bind_method(D_METHOD("spawn_ring", "type", "center", "radius", "count"), &Horde2D::spawn_ring);
	ClassDB::bind_method(D_METHOD("kill", "id"), &Horde2D::kill);
	ClassDB::bind_method(D_METHOD("damage", "id", "amount"), &Horde2D::damage);
	ClassDB::bind_method(D_METHOD("damage_circle", "center", "radius", "amount"), &Horde2D::damage_circle);
	ClassDB::bind_method(D_METHOD("query_circle", "center", "radius"), &Horde2D::query_circle);
	ClassDB::bind_method(D_METHOD("is_agent_alive", "id"), &Horde2D::is_agent_alive);
	ClassDB::bind_method(D_METHOD("get_agent_position", "id"), &Horde2D::get_agent_position);
	ClassDB::bind_method(D_METHOD("get_agent_velocity", "id"), &Horde2D::get_agent_velocity);
	ClassDB::bind_method(D_METHOD("get_agent_hp", "id"), &Horde2D::get_agent_hp);
	ClassDB::bind_method(D_METHOD("get_agent_type", "id"), &Horde2D::get_agent_type);
	ClassDB::bind_method(D_METHOD("get_agent_count"), &Horde2D::get_agent_count);
	ClassDB::bind_method(D_METHOD("clear"), &Horde2D::clear);
	ClassDB::bind_method(D_METHOD("step", "delta"), &Horde2D::step);

	ClassDB::bind_method(D_METHOD("add_obstacle", "center", "radius"), &Horde2D::add_obstacle);
	ClassDB::bind_method(D_METHOD("clear_obstacles"), &Horde2D::clear_obstacles);
	ClassDB::bind_method(D_METHOD("get_obstacle_count"), &Horde2D::get_obstacle_count);
	ClassDB::bind_method(D_METHOD("get_debug_stats"), &Horde2D::get_debug_stats);

	ClassDB::bind_method(D_METHOD("set_target", "target"), &Horde2D::set_target);
	ClassDB::bind_method(D_METHOD("get_target"), &Horde2D::get_target);
	ClassDB::bind_method(D_METHOD("set_target_position", "position"), &Horde2D::set_target_position);
	ClassDB::bind_method(D_METHOD("get_target_position"), &Horde2D::get_target_position);
	ClassDB::bind_method(D_METHOD("set_target_radius", "radius"), &Horde2D::set_target_radius);
	ClassDB::bind_method(D_METHOD("get_target_radius"), &Horde2D::get_target_radius);
	ClassDB::bind_method(D_METHOD("set_target_solid", "solid"), &Horde2D::set_target_solid);
	ClassDB::bind_method(D_METHOD("is_target_solid"), &Horde2D::is_target_solid);
	ClassDB::bind_method(D_METHOD("set_update_mode", "mode"), &Horde2D::set_update_mode);
	ClassDB::bind_method(D_METHOD("get_update_mode"), &Horde2D::get_update_mode);
	ClassDB::bind_method(D_METHOD("set_fixed_timestep", "seconds"), &Horde2D::set_fixed_timestep);
	ClassDB::bind_method(D_METHOD("get_fixed_timestep"), &Horde2D::get_fixed_timestep);
	ClassDB::bind_method(D_METHOD("set_max_substeps", "max_substeps"), &Horde2D::set_max_substeps);
	ClassDB::bind_method(D_METHOD("get_max_substeps"), &Horde2D::get_max_substeps);
	ClassDB::bind_method(D_METHOD("set_interpolate", "enabled"), &Horde2D::set_interpolate);
	ClassDB::bind_method(D_METHOD("is_interpolating"), &Horde2D::is_interpolating);
	ClassDB::bind_method(D_METHOD("set_seed", "seed"), &Horde2D::set_seed);
	ClassDB::bind_method(D_METHOD("get_seed"), &Horde2D::get_seed);
	ClassDB::bind_method(D_METHOD("set_cell_size", "cell_size"), &Horde2D::set_cell_size);
	ClassDB::bind_method(D_METHOD("get_cell_size"), &Horde2D::get_cell_size);
	ClassDB::bind_method(D_METHOD("set_overlap_resolution", "fraction"), &Horde2D::set_overlap_resolution);
	ClassDB::bind_method(D_METHOD("get_overlap_resolution"), &Horde2D::get_overlap_resolution);
	ClassDB::bind_method(D_METHOD("set_bounds_enabled", "enabled"), &Horde2D::set_bounds_enabled);
	ClassDB::bind_method(D_METHOD("is_bounds_enabled"), &Horde2D::is_bounds_enabled);
	ClassDB::bind_method(D_METHOD("set_bounds", "bounds"), &Horde2D::set_bounds);
	ClassDB::bind_method(D_METHOD("get_bounds"), &Horde2D::get_bounds);
	ClassDB::bind_method(D_METHOD("set_debug_draw", "enabled"), &Horde2D::set_debug_draw);
	ClassDB::bind_method(D_METHOD("is_debug_draw_enabled"), &Horde2D::is_debug_draw_enabled);
	ClassDB::bind_method(D_METHOD("set_debug_draw_grid", "enabled"), &Horde2D::set_debug_draw_grid);
	ClassDB::bind_method(D_METHOD("is_debug_draw_grid_enabled"), &Horde2D::is_debug_draw_grid_enabled);

	ADD_GROUP("Target", "target_");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "target", PROPERTY_HINT_NODE_TYPE, "Node2D"), "set_target", "get_target");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "target_position", PROPERTY_HINT_NONE, "suffix:px"), "set_target_position", "get_target_position");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "target_radius", PROPERTY_HINT_RANGE, "0,512,0.1,or_greater,suffix:px"), "set_target_radius", "get_target_radius");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "target_solid"), "set_target_solid", "is_target_solid");
	ADD_GROUP("Simulation", "");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "update_mode", PROPERTY_HINT_ENUM, "Process,Physics,Manual"), "set_update_mode", "get_update_mode");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fixed_timestep", PROPERTY_HINT_RANGE, "0,0.1,0.0001,suffix:s"), "set_fixed_timestep", "get_fixed_timestep");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_substeps", PROPERTY_HINT_RANGE, "1,64,1"), "set_max_substeps", "get_max_substeps");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "interpolate"), "set_interpolate", "is_interpolating");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "seed"), "set_seed", "get_seed");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "cell_size", PROPERTY_HINT_RANGE, "0,512,0.1,or_greater,suffix:px"), "set_cell_size", "get_cell_size");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "overlap_resolution", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_overlap_resolution", "get_overlap_resolution");
	ADD_GROUP("Bounds", "bounds_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "bounds_enabled"), "set_bounds_enabled", "is_bounds_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::RECT2, "bounds", PROPERTY_HINT_NONE, "suffix:px"), "set_bounds", "get_bounds");
	ADD_GROUP("Debug", "debug_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "debug_draw"), "set_debug_draw", "is_debug_draw_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "debug_draw_grid"), "set_debug_draw_grid", "is_debug_draw_grid_enabled");

	ADD_SIGNAL(MethodInfo("agents_died", PropertyInfo(Variant::PACKED_INT32_ARRAY, "ids"), PropertyInfo(Variant::PACKED_VECTOR2_ARRAY, "positions")));
	ADD_SIGNAL(MethodInfo("target_contact", PropertyInfo(Variant::PACKED_INT32_ARRAY, "ids"), PropertyInfo(Variant::FLOAT, "delta")));

	BIND_ENUM_CONSTANT(UPDATE_PROCESS);
	BIND_ENUM_CONSTANT(UPDATE_PHYSICS);
	BIND_ENUM_CONSTANT(UPDATE_MANUAL);
}

} // namespace godot
