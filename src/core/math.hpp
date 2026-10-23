// Minimal math types for the engine-agnostic simulation core.
#pragma once

#include <cmath>
#include <cstdint>

namespace horde {

struct Vec2 {
	float x = 0.0f;
	float y = 0.0f;

	constexpr Vec2() = default;
	constexpr Vec2(float p_x, float p_y) :
			x(p_x), y(p_y) {}

	constexpr Vec2 operator+(Vec2 o) const { return Vec2(x + o.x, y + o.y); }
	constexpr Vec2 operator-(Vec2 o) const { return Vec2(x - o.x, y - o.y); }
	constexpr Vec2 operator*(float s) const { return Vec2(x * s, y * s); }
	constexpr bool operator==(Vec2 o) const { return x == o.x && y == o.y; }

	float length() const { return std::sqrt(x * x + y * y); }
	constexpr float length_squared() const { return x * x + y * y; }
};

// Axis-aligned rectangle: position is the top-left corner, size is always positive.
struct Rect2f {
	Vec2 position;
	Vec2 size;

	constexpr Vec2 end() const { return position + size; }
};

// Exact overlap predicate shared by the spatial hash and the brute-force reference:
// a circle of radius `p_radius` at `p_center` overlaps an agent of radius `p_agent_radius`
// at `p_agent` when the distance between centers is <= the sum of radii.
inline bool circles_overlap(Vec2 p_center, float p_radius, Vec2 p_agent, float p_agent_radius) {
	const float dx = p_agent.x - p_center.x;
	const float dy = p_agent.y - p_center.y;
	const float r = p_radius + p_agent_radius;
	return dx * dx + dy * dy <= r * r;
}

// Converts a world coordinate to an integer cell coordinate. Saturates instead of
// overflowing, and maps NaN to the lowest cell, so it never invokes undefined behaviour.
inline int32_t cell_coord(float p_value, float p_inv_cell_size) {
	constexpr float LIMIT = 1073741824.0f; // 2^30, leaves headroom for neighbour offsets.
	const float f = std::floor(p_value * p_inv_cell_size);
	if (!(f > -LIMIT)) {
		return -(1 << 30);
	}
	if (!(f < LIMIT)) {
		return 1 << 30;
	}
	return static_cast<int32_t>(f);
}

} // namespace horde
