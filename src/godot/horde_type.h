#pragma once

#include "core/simulation.hpp"

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/variant/color.hpp>

namespace godot {

// Shared description of one kind of agent: look (texture strip, animation, tint) and
// behaviour (radius, speed, steering weights, hit points).
class HordeType : public Resource {
	GDCLASS(HordeType, Resource)

public:
	void set_texture(const Ref<Texture2D> &p_texture);
	Ref<Texture2D> get_texture() const { return texture_; }
	void set_hframes(int p_hframes);
	int get_hframes() const { return hframes_; }
	void set_fps(float p_fps);
	float get_fps() const { return fps_; }
	void set_modulate(const Color &p_modulate);
	Color get_modulate() const { return modulate_; }

	void set_radius(float p_radius);
	float get_radius() const { return params_.radius; }
	void set_max_speed(float p_max_speed);
	float get_max_speed() const { return params_.max_speed; }
	void set_acceleration(float p_acceleration);
	float get_acceleration() const { return params_.acceleration; }
	void set_max_hp(float p_max_hp);
	float get_max_hp() const { return params_.max_hp; }
	void set_separation_weight(float p_weight);
	float get_separation_weight() const { return params_.separation_weight; }
	void set_seek_weight(float p_weight);
	float get_seek_weight() const { return params_.seek_weight; }

	const horde::TypeParams &get_params() const { return params_; }

	// Size of one animation frame in pixels; falls back to the body diameter when
	// there is no texture.
	Vector2 get_frame_size() const;

protected:
	static void _bind_methods();

private:
	Ref<Texture2D> texture_;
	int hframes_ = 1;
	float fps_ = 8.0f;
	Color modulate_ = Color(1, 1, 1, 1);
	horde::TypeParams params_;
};

} // namespace godot
