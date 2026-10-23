#include "godot/horde_type.h"

#include <godot_cpp/core/class_db.hpp>

namespace godot {

void HordeType::set_texture(const Ref<Texture2D> &p_texture) {
	texture_ = p_texture;
	emit_changed();
}

void HordeType::set_hframes(int p_hframes) {
	hframes_ = MAX(p_hframes, 1);
	emit_changed();
}

void HordeType::set_fps(float p_fps) {
	fps_ = MAX(p_fps, 0.0f);
	emit_changed();
}

void HordeType::set_modulate(const Color &p_modulate) {
	modulate_ = p_modulate;
	emit_changed();
}

void HordeType::set_radius(float p_radius) {
	params_.radius = MAX(p_radius, 0.0f);
	emit_changed();
}

void HordeType::set_max_speed(float p_max_speed) {
	params_.max_speed = MAX(p_max_speed, 0.0f);
	emit_changed();
}

void HordeType::set_acceleration(float p_acceleration) {
	params_.acceleration = MAX(p_acceleration, 0.0f);
	emit_changed();
}

void HordeType::set_max_hp(float p_max_hp) {
	params_.max_hp = MAX(p_max_hp, 0.0f);
	emit_changed();
}

void HordeType::set_separation_weight(float p_weight) {
	params_.separation_weight = p_weight;
	emit_changed();
}

void HordeType::set_seek_weight(float p_weight) {
	params_.seek_weight = p_weight;
	emit_changed();
}

Vector2 HordeType::get_frame_size() const {
	if (texture_.is_valid()) {
		const Vector2 size = texture_->get_size();
		return Vector2(size.x / static_cast<real_t>(hframes_), size.y);
	}
	const real_t diameter = MAX(params_.radius * 2.0f, 1.0f);
	return Vector2(diameter, diameter);
}

void HordeType::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_texture", "texture"), &HordeType::set_texture);
	ClassDB::bind_method(D_METHOD("get_texture"), &HordeType::get_texture);
	ClassDB::bind_method(D_METHOD("set_hframes", "hframes"), &HordeType::set_hframes);
	ClassDB::bind_method(D_METHOD("get_hframes"), &HordeType::get_hframes);
	ClassDB::bind_method(D_METHOD("set_fps", "fps"), &HordeType::set_fps);
	ClassDB::bind_method(D_METHOD("get_fps"), &HordeType::get_fps);
	ClassDB::bind_method(D_METHOD("set_modulate", "modulate"), &HordeType::set_modulate);
	ClassDB::bind_method(D_METHOD("get_modulate"), &HordeType::get_modulate);
	ClassDB::bind_method(D_METHOD("set_radius", "radius"), &HordeType::set_radius);
	ClassDB::bind_method(D_METHOD("get_radius"), &HordeType::get_radius);
	ClassDB::bind_method(D_METHOD("set_max_speed", "max_speed"), &HordeType::set_max_speed);
	ClassDB::bind_method(D_METHOD("get_max_speed"), &HordeType::get_max_speed);
	ClassDB::bind_method(D_METHOD("set_acceleration", "acceleration"), &HordeType::set_acceleration);
	ClassDB::bind_method(D_METHOD("get_acceleration"), &HordeType::get_acceleration);
	ClassDB::bind_method(D_METHOD("set_max_hp", "max_hp"), &HordeType::set_max_hp);
	ClassDB::bind_method(D_METHOD("get_max_hp"), &HordeType::get_max_hp);
	ClassDB::bind_method(D_METHOD("set_separation_weight", "weight"), &HordeType::set_separation_weight);
	ClassDB::bind_method(D_METHOD("get_separation_weight"), &HordeType::get_separation_weight);
	ClassDB::bind_method(D_METHOD("set_seek_weight", "weight"), &HordeType::set_seek_weight);
	ClassDB::bind_method(D_METHOD("get_seek_weight"), &HordeType::get_seek_weight);
	ClassDB::bind_method(D_METHOD("get_frame_size"), &HordeType::get_frame_size);

	ADD_GROUP("Appearance", "");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "texture", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"), "set_texture", "get_texture");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "hframes", PROPERTY_HINT_RANGE, "1,256,1"), "set_hframes", "get_hframes");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fps", PROPERTY_HINT_RANGE, "0,60,0.1,or_greater"), "set_fps", "get_fps");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "modulate"), "set_modulate", "get_modulate");
	ADD_GROUP("Body", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "radius", PROPERTY_HINT_RANGE, "0,256,0.1,or_greater,suffix:px"), "set_radius", "get_radius");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_hp", PROPERTY_HINT_RANGE, "0,1000,0.1,or_greater"), "set_max_hp", "get_max_hp");
	ADD_GROUP("Movement", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_speed", PROPERTY_HINT_RANGE, "0,1000,0.1,or_greater,suffix:px/s"), "set_max_speed", "get_max_speed");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "acceleration", PROPERTY_HINT_RANGE, "0,10000,1,or_greater,suffix:px/s²"), "set_acceleration", "get_acceleration");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "separation_weight", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater"), "set_separation_weight", "get_separation_weight");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "seek_weight", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater"), "set_seek_weight", "get_seek_weight");
}

} // namespace godot
