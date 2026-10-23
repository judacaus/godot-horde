// The spatial hash must return exactly what a brute-force scan returns.
#include "core/rng.hpp"
#include "core/simulation.hpp"
#include "core/spatial_hash.hpp"

#include <doctest.h>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace horde;

namespace {

enum class Layout {
	UNIFORM,
	CLUSTERED,
	SAME_POINT,
	LINE,
	FAR_AWAY,
	COUNT
};

Vec2 random_point(Pcg32 &rng, Layout layout, Vec2 cluster, float extent) {
	switch (layout) {
		case Layout::UNIFORM:
			return Vec2(rng.range(-extent, extent), rng.range(-extent, extent));
		case Layout::CLUSTERED: {
			// Sum of uniforms approximates a gaussian blob.
			const float s = extent * 0.05f;
			const float dx = (rng.next_float() + rng.next_float() + rng.next_float() - 1.5f) * s;
			const float dy = (rng.next_float() + rng.next_float() + rng.next_float() - 1.5f) * s;
			return cluster + Vec2(dx, dy);
		}
		case Layout::SAME_POINT:
			return cluster;
		case Layout::LINE:
			return Vec2(rng.range(-extent, extent), cluster.y);
		case Layout::FAR_AWAY:
			// Large coordinates stress float rounding in cell computation and margins.
			return Vec2(1.0e6f + rng.range(-extent, extent), -3.0e5f + rng.range(-extent, extent));
		case Layout::COUNT:
			break;
	}
	return Vec2();
}

std::vector<AgentId> sorted(std::vector<AgentId> v) {
	std::sort(v.begin(), v.end());
	return v;
}

} // namespace

TEST_CASE("SpatialHash: box queries match a brute-force cell scan (1000 seeded cases)") {
	Pcg32 rng(0xC0FFEEu);
	int checked = 0;
	for (int c = 0; c < 1000; c++) {
		const uint32_t n = rng.next_u32() % 600;
		const float extent = rng.range(10.0f, 2000.0f);
		const float cell = rng.range(0.5f, 120.0f);
		const Layout layout = static_cast<Layout>(rng.next_u32() % static_cast<uint32_t>(Layout::COUNT));
		const Vec2 cluster(rng.range(-extent, extent), rng.range(-extent, extent));
		std::vector<float> xs(n), ys(n);
		for (uint32_t i = 0; i < n; i++) {
			const Vec2 p = random_point(rng, layout, cluster, extent);
			xs[i] = p.x;
			ys[i] = p.y;
		}
		SpatialHash hash;
		hash.build(xs.data(), ys.data(), n, cell);
		REQUIRE(hash.size() == n);

		const Vec2 center = n > 0 && rng.next_float() < 0.5f ? Vec2(xs[rng.next_u32() % n], ys[rng.next_u32() % n]) : random_point(rng, layout, cluster, extent);
		const float half = rng.next_float() < 0.1f ? extent * 3.0f : rng.range(0.0f, extent * 0.3f);
		const Vec2 lo = center - Vec2(half, half);
		const Vec2 hi = center + Vec2(half, half);

		std::vector<AgentId> got;
		std::vector<int> visits(n, 0);
		hash.for_each_in_box(lo, hi, [&](uint32_t e) {
			const uint32_t i = hash.indices()[e];
			visits[i]++;
			got.push_back(static_cast<AgentId>(i));
		});

		const float inv = 1.0f / cell;
		const int32_t x0 = cell_coord(lo.x, inv), x1 = cell_coord(hi.x, inv);
		const int32_t y0 = cell_coord(lo.y, inv), y1 = cell_coord(hi.y, inv);
		std::vector<AgentId> expected;
		for (uint32_t i = 0; i < n; i++) {
			const int32_t cx = cell_coord(xs[i], inv), cy = cell_coord(ys[i], inv);
			if (cx >= x0 && cx <= x1 && cy >= y0 && cy <= y1) {
				expected.push_back(static_cast<AgentId>(i));
			}
		}
		for (uint32_t i = 0; i < n; i++) {
			REQUIRE(visits[i] <= 1);
		}
		REQUIRE(sorted(got) == expected);
		checked++;
	}
	CHECK(checked == 1000);
}

// Builds a simulation in a random state: several agent types, random layout, some
// steps (so positions come from the actual crowd dynamics), kills that are not yet
// compacted, and agents spawned after the hash was last built.
TEST_CASE("Simulation::query_circle matches brute force (1500 seeded cases)") {
	Pcg32 rng(20261023u);
	int cases = 0;
	int non_empty = 0;
	for (int world = 0; world < 500; world++) {
		Simulation sim;
		sim.set_seed(rng.next_u32());
		const uint32_t type_count = 1 + rng.next_u32() % 3;
		for (uint32_t t = 0; t < type_count; t++) {
			TypeParams params;
			params.radius = rng.next_float() < 0.1f ? 0.0f : rng.range(0.5f, 24.0f);
			params.max_speed = rng.range(10.0f, 200.0f);
			sim.add_type(params);
		}
		if (rng.next_float() < 0.3f) {
			sim.set_cell_size(rng.range(1.0f, 100.0f));
		}
		const float extent = rng.range(20.0f, 1500.0f);
		const Layout layout = static_cast<Layout>(rng.next_u32() % static_cast<uint32_t>(Layout::COUNT));
		const Vec2 cluster(rng.range(-extent, extent), rng.range(-extent, extent));
		sim.set_target(cluster);
		sim.set_target_radius(rng.next_float() < 0.5f ? rng.range(0.0f, 40.0f) : 0.0f);

		const uint32_t n = rng.next_u32() % 1500;
		for (uint32_t i = 0; i < n; i++) {
			sim.spawn(static_cast<uint16_t>(rng.next_u32() % type_count), random_point(rng, layout, cluster, extent));
		}
		const uint32_t steps = rng.next_u32() % 4;
		for (uint32_t s = 0; s < steps; s++) {
			sim.step(1.0f / 60.0f);
		}

		std::vector<AgentId> probe;
		for (int q = 0; q < 3; q++) {
			// Mutate between queries: kills (flag only) and spawns (hash tail).
			if (rng.next_float() < 0.5f && sim.agent_count() > 0) {
				probe.clear();
				sim.query_circle_brute_force(cluster, extent * 4.0f, probe);
				for (AgentId id : probe) {
					if (rng.next_float() < 0.2f) {
						sim.kill(id);
					}
				}
			}
			if (rng.next_float() < 0.5f) {
				const uint32_t extra = rng.next_u32() % 40;
				for (uint32_t i = 0; i < extra; i++) {
					sim.spawn(static_cast<uint16_t>(rng.next_u32() % type_count), random_point(rng, layout, cluster, extent));
				}
			}

			Vec2 center = random_point(rng, layout, cluster, extent);
			Vec2 probe_position;
			if (!probe.empty() && rng.next_float() < 0.3f && sim.get_position(probe[0], probe_position)) {
				center = probe_position;
			}
			const float roll = rng.next_float();
			const float radius = roll < 0.1f ? 0.0f : (roll < 0.2f ? extent * 5.0f : rng.range(0.0f, extent * 0.25f));

			std::vector<AgentId> got;
			std::vector<AgentId> expected;
			sim.query_circle(center, radius, got);
			sim.query_circle_brute_force(center, radius, expected);
			const std::vector<AgentId> got_sorted = sorted(got);
			REQUIRE(std::adjacent_find(got_sorted.begin(), got_sorted.end()) == got_sorted.end());
			REQUIRE(got_sorted == sorted(expected));
			for (AgentId id : got) {
				REQUIRE(sim.is_alive(id));
			}
			non_empty += got.empty() ? 0 : 1;
			cases++;
		}
	}
	CHECK(cases == 1500);
	// Make sure the generator produces meaningful cases, not just empty results.
	CHECK(non_empty > 700);
}

TEST_CASE("Simulation::query_circle includes agents exactly at the boundary") {
	Simulation sim;
	TypeParams params;
	params.radius = 4.0f;
	sim.add_type(params);
	const AgentId a = sim.spawn(0, Vec2(14.0f, 0.0f)); // Distance 14 = 10 + 4.
	const AgentId b = sim.spawn(0, Vec2(14.5f, 0.0f));
	std::vector<AgentId> got;
	sim.query_circle(Vec2(0.0f, 0.0f), 10.0f, got);
	REQUIRE(got.size() == 1);
	CHECK(got[0] == a);
	CHECK(b != a);
}
