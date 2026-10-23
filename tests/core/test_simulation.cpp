#include "core/rng.hpp"
#include "core/simulation.hpp"

#include <doctest.h>

#include <cmath>
#include <cstring>
#include <vector>

using namespace horde;

namespace {

constexpr float DT = 1.0f / 60.0f;

TypeParams basic_type(float radius = 8.0f, float max_speed = 100.0f) {
	TypeParams p;
	p.radius = radius;
	p.max_speed = max_speed;
	p.acceleration = 1000.0f;
	p.max_hp = 10.0f;
	return p;
}

float distance(Vec2 a, Vec2 b) {
	return (a - b).length();
}

// Every live id maps to a dense slot holding that same id, and the alive count matches.
void check_handle_invariants(const Simulation &sim) {
	uint32_t alive = 0;
	for (uint32_t i = 0; i < sim.dense_size(); i++) {
		if (!sim.alive_flags()[i]) {
			continue;
		}
		alive++;
		const AgentId id = sim.id_at(i);
		REQUIRE(sim.is_alive(id));
		REQUIRE(sim.handles().dense_index(id) == i);
		Vec2 p;
		REQUIRE(sim.get_position(id, p));
		REQUIRE(p == Vec2(sim.pos_x()[i], sim.pos_y()[i]));
	}
	REQUIRE(alive == sim.agent_count());
}

bool same_state(const Simulation &a, const Simulation &b) {
	if (a.dense_size() != b.dense_size()) {
		return false;
	}
	const size_t bytes = a.dense_size() * sizeof(float);
	return std::memcmp(a.pos_x(), b.pos_x(), bytes) == 0 && std::memcmp(a.pos_y(), b.pos_y(), bytes) == 0 &&
			std::memcmp(a.vel_x(), b.vel_x(), bytes) == 0 && std::memcmp(a.vel_y(), b.vel_y(), bytes) == 0;
}

} // namespace

TEST_CASE("Seek: agents move towards the target without exceeding max speed") {
	Simulation sim;
	sim.add_type(basic_type(8.0f, 100.0f));
	sim.set_target(Vec2(500.0f, 0.0f));
	const AgentId id = sim.spawn(0, Vec2(0.0f, 0.0f));
	Vec2 previous(0.0f, 0.0f);
	for (int f = 0; f < 120; f++) {
		sim.advance(DT);
		Vec2 p, v;
		REQUIRE(sim.get_position(id, p));
		REQUIRE(sim.get_velocity(id, v));
		CHECK(v.length() <= 100.0f * 1.0001f);
		CHECK(p.x > previous.x);
		CHECK(std::fabs(p.y) < 1e-3f);
		previous = p;
	}
	// 2 seconds at up to 100 px/s, with a short acceleration phase.
	CHECK(previous.x > 180.0f);
	CHECK(previous.x <= 200.0f);
}

TEST_CASE("Acceleration limits how fast the velocity changes") {
	Simulation sim;
	TypeParams p = basic_type();
	p.acceleration = 60.0f; // 1 px/s per frame at 60 FPS.
	sim.add_type(p);
	sim.set_target(Vec2(1000.0f, 0.0f));
	const AgentId id = sim.spawn(0, Vec2());
	sim.advance(DT);
	Vec2 v;
	REQUIRE(sim.get_velocity(id, v));
	CHECK(v.length() == doctest::Approx(1.0f).epsilon(1e-4));
}

TEST_CASE("Separation pushes overlapping agents apart, even when spawned on the same point") {
	Simulation sim;
	sim.add_type(basic_type(8.0f, 100.0f));
	sim.set_target_enabled(false);
	std::vector<AgentId> ids;
	for (int i = 0; i < 2; i++) {
		ids.push_back(sim.spawn(0, Vec2(10.0f, 10.0f)));
	}
	for (int f = 0; f < 180; f++) {
		sim.advance(DT);
	}
	Vec2 a, b;
	REQUIRE(sim.get_position(ids[0], a));
	REQUIRE(sim.get_position(ids[1], b));
	CHECK(distance(a, b) > 15.0f);
}

TEST_CASE("A crowd spreads out instead of collapsing on the target") {
	Simulation sim;
	sim.add_type(basic_type(8.0f, 120.0f));
	sim.set_target(Vec2());
	std::vector<AgentId> ids;
	sim.spawn_ring(0, Vec2(), 400.0f, 200, ids);
	REQUIRE(ids.size() == 200);
	for (int f = 0; f < 600; f++) {
		sim.advance(DT);
	}
	// With perfect packing 200 discs of radius 8 need a disc of radius ~120.
	float max_d = 0.0f;
	uint32_t overlapping_pairs = 0;
	for (size_t i = 0; i < ids.size(); i++) {
		Vec2 pi;
		sim.get_position(ids[i], pi);
		max_d = std::max(max_d, pi.length());
		for (size_t j = i + 1; j < ids.size(); j++) {
			Vec2 pj;
			sim.get_position(ids[j], pj);
			if (distance(pi, pj) < 8.0f) { // Overlapping by more than half a body.
				overlapping_pairs++;
			}
		}
	}
	CHECK(max_d > 80.0f);
	CHECK(overlapping_pairs < 20);
}

TEST_CASE("Bounds keep agents inside the rectangle (inset by their radius)") {
	Simulation sim;
	sim.add_type(basic_type(5.0f, 300.0f));
	sim.set_bounds(Rect2f{ Vec2(0.0f, 0.0f), Vec2(100.0f, 50.0f) });
	sim.set_target(Vec2(1000.0f, -1000.0f)); // Outside: agents press into the corner.
	std::vector<AgentId> ids;
	sim.spawn(0, Vec2(50.0f, 25.0f), 30, ids);
	for (int f = 0; f < 120; f++) {
		sim.advance(DT);
		for (AgentId id : ids) {
			Vec2 p;
			REQUIRE(sim.get_position(id, p));
			REQUIRE(p.x >= 5.0f);
			REQUIRE(p.x <= 95.0f);
			REQUIRE(p.y >= 5.0f);
			REQUIRE(p.y <= 45.0f);
		}
	}
}

TEST_CASE("Static circular obstacles are never penetrated") {
	Simulation sim;
	sim.add_type(basic_type(6.0f, 200.0f));
	sim.add_obstacle(Vec2(200.0f, 0.0f), 50.0f);
	sim.add_obstacle(Vec2(0.0f, 300.0f), 30.0f);
	// A huge obstacle exercises the "large obstacle" path of the obstacle grid.
	sim.add_obstacle(Vec2(-5000.0f, 0.0f), 4000.0f);
	sim.set_target(Vec2(400.0f, 0.0f));
	std::vector<AgentId> ids;
	sim.spawn(0, Vec2(0.0f, 0.0f), 50, ids);
	sim.spawn(0, Vec2(-900.0f, 0.0f), 10, ids);
	for (int f = 0; f < 400; f++) {
		sim.advance(DT);
		for (AgentId id : ids) {
			Vec2 p;
			REQUIRE(sim.get_position(id, p));
			for (const Obstacle &o : sim.get_obstacles()) {
				REQUIRE(distance(p, o.center) >= o.radius + 6.0f - 1e-2f);
			}
		}
	}
}

TEST_CASE("Solid target: agents stop at its edge and are reported as contacts") {
	Simulation sim;
	sim.add_type(basic_type(8.0f, 150.0f));
	sim.set_target(Vec2(0.0f, 0.0f));
	sim.set_target_radius(20.0f);
	std::vector<AgentId> ids;
	sim.spawn_ring(0, Vec2(), 200.0f, 12, ids);
	for (int f = 0; f < 240; f++) {
		sim.advance(DT);
		for (AgentId id : ids) {
			Vec2 p;
			REQUIRE(sim.get_position(id, p));
			REQUIRE(p.length() >= 28.0f - 1e-2f);
		}
	}
	CHECK(sim.contacts().size() >= 8);
	for (AgentId id : sim.contacts()) {
		Vec2 p;
		REQUIRE(sim.get_position(id, p));
		CHECK(p.length() <= 20.0f + 8.0f + sim.get_contact_margin() + 1e-3f);
	}
}

TEST_CASE("Non-solid target lets agents reach the point; zero radius means no contacts") {
	Simulation sim;
	sim.add_type(basic_type(4.0f, 100.0f));
	sim.set_target_solid(false);
	sim.set_target(Vec2(50.0f, 0.0f));
	const AgentId id = sim.spawn(0, Vec2());
	for (int f = 0; f < 120; f++) {
		sim.advance(DT);
	}
	Vec2 p;
	sim.get_position(id, p);
	CHECK(distance(p, Vec2(50.0f, 0.0f)) < 5.0f);
	CHECK(sim.contacts().empty());
}

TEST_CASE("kill(): ids become stale, deaths are queued with positions, survivors keep their ids") {
	Simulation sim;
	sim.add_type(basic_type());
	sim.set_target_enabled(false);
	std::vector<AgentId> ids;
	for (int i = 0; i < 10; i++) {
		ids.push_back(sim.spawn(0, Vec2(static_cast<float>(i) * 100.0f, 0.0f)));
	}
	CHECK(sim.kill(ids[3]));
	CHECK_FALSE(sim.kill(ids[3]));
	CHECK_FALSE(sim.is_alive(ids[3]));
	CHECK(sim.agent_count() == 9);
	REQUIRE(sim.pending_deaths().size() == 1);
	CHECK(sim.pending_deaths()[0].id == ids[3]);
	CHECK(sim.pending_deaths()[0].position == Vec2(300.0f, 0.0f));

	sim.advance(DT); // Compacts the dense arrays.
	CHECK(sim.dense_size() == 9);
	check_handle_invariants(sim);
	for (int i = 0; i < 10; i++) {
		Vec2 p;
		if (i == 3) {
			CHECK_FALSE(sim.get_position(ids[i], p));
		} else {
			REQUIRE(sim.get_position(ids[i], p));
			CHECK(p == Vec2(static_cast<float>(i) * 100.0f, 0.0f));
		}
	}

	// A new agent may reuse the slot later, but the old id must stay stale.
	for (int i = 0; i < 100; i++) {
		sim.spawn(0, Vec2());
	}
	CHECK_FALSE(sim.is_alive(ids[3]));
}

TEST_CASE("damage_circle(): returns hit ids, kills at hp <= 0, ignores agents outside") {
	Simulation sim;
	TypeParams p = basic_type(5.0f);
	p.max_hp = 10.0f;
	sim.add_type(p);
	const AgentId near_a = sim.spawn(0, Vec2(0.0f, 0.0f));
	const AgentId near_b = sim.spawn(0, Vec2(12.0f, 0.0f)); // 12 <= 10 + 5: overlaps.
	const AgentId far = sim.spawn(0, Vec2(16.0f, 0.0f)); // 16 > 15: outside.

	std::vector<AgentId> hits;
	sim.damage_circle(Vec2(), 10.0f, 4.0f, hits);
	CHECK(hits.size() == 2);
	float hp = 0.0f;
	REQUIRE(sim.get_hp(near_a, hp));
	CHECK(hp == doctest::Approx(6.0f));
	REQUIRE(sim.get_hp(far, hp));
	CHECK(hp == doctest::Approx(10.0f));

	hits.clear();
	sim.damage_circle(Vec2(), 10.0f, 6.0f, hits);
	CHECK(hits.size() == 2);
	CHECK_FALSE(sim.is_alive(near_a));
	CHECK_FALSE(sim.is_alive(near_b));
	CHECK(sim.is_alive(far));
	CHECK(sim.pending_deaths().size() == 2);

	// Dead agents are no longer hit or returned by queries, even before compaction.
	hits.clear();
	sim.damage_circle(Vec2(), 10.0f, 1.0f, hits);
	CHECK(hits.empty());
}

TEST_CASE("clear() removes everything silently and invalidates ids") {
	Simulation sim;
	sim.add_type(basic_type());
	std::vector<AgentId> ids;
	sim.spawn(0, Vec2(), 100, ids);
	sim.kill(ids[0]);
	sim.clear();
	CHECK(sim.agent_count() == 0);
	CHECK(sim.dense_size() == 0);
	CHECK(sim.pending_deaths().size() == 1); // Only the explicit kill.
	for (AgentId id : ids) {
		CHECK_FALSE(sim.is_alive(id));
	}
	std::vector<AgentId> found;
	sim.query_circle(Vec2(), 1e6f, found);
	CHECK(found.empty());
	sim.advance(DT);
	CHECK(sim.spawn(0, Vec2()) != INVALID_AGENT_ID);
}

TEST_CASE("Invalid input is rejected") {
	Simulation sim;
	sim.add_type(basic_type());
	CHECK(sim.spawn(7, Vec2()) == INVALID_AGENT_ID); // Unknown type.
	CHECK(sim.spawn(0, Vec2(NAN, 0.0f)) == INVALID_AGENT_ID);
	CHECK(sim.spawn(0, Vec2(INFINITY, 0.0f)) == INVALID_AGENT_ID);
	CHECK_FALSE(sim.kill(INVALID_AGENT_ID));
	CHECK_FALSE(sim.kill(0));
	CHECK_FALSE(sim.kill(123456));
	std::vector<AgentId> out;
	sim.query_circle(Vec2(NAN, 0.0f), 10.0f, out);
	sim.query_circle(Vec2(), -1.0f, out);
	CHECK(out.empty());
	CHECK(sim.advance(NAN) == 0);
	CHECK(sim.advance(-1.0f) == 0);
}

TEST_CASE("Determinism: identical inputs give bit-identical state") {
	auto run = [](Simulation &sim) {
		sim.set_seed(1234);
		TypeParams a = basic_type(6.0f, 90.0f);
		TypeParams b = basic_type(11.0f, 60.0f);
		b.separation_weight = 2.5f;
		sim.add_type(a);
		sim.add_type(b);
		sim.set_target_radius(16.0f);
		sim.add_obstacle(Vec2(100.0f, 100.0f), 40.0f);
		std::vector<AgentId> ids;
		sim.spawn_ring(0, Vec2(), 600.0f, 1500, ids);
		sim.spawn(1, Vec2(300.0f, -200.0f), 500, ids);
		for (int f = 0; f < 90; f++) {
			sim.set_target(Vec2(std::cos(f * 0.05f) * 80.0f, std::sin(f * 0.05f) * 80.0f));
			std::vector<AgentId> hits;
			sim.damage_circle(Vec2(static_cast<float>(f) * 5.0f - 200.0f, 0.0f), 30.0f, 3.0f, hits);
			if (f % 10 == 0) {
				sim.spawn(0, Vec2(-400.0f, 400.0f), 20, ids);
			}
			sim.advance(DT);
		}
	};
	Simulation a, b;
	run(a);
	run(b);
	CHECK(a.agent_count() == b.agent_count());
	CHECK(a.pending_deaths().size() == b.pending_deaths().size());
	CHECK(a.contacts() == b.contacts());
	CHECK(same_state(a, b));
}

TEST_CASE("Fixed timestep: any frame-delta partition equals the same number of raw steps") {
	auto setup = [](Simulation &sim) {
		sim.set_seed(99);
		sim.add_type(basic_type(7.0f, 110.0f));
		sim.set_target(Vec2(40.0f, -30.0f));
		sim.set_target_radius(12.0f);
		std::vector<AgentId> ids;
		sim.spawn_ring(0, Vec2(), 300.0f, 400, ids);
	};
	const float fixed = 1.0f / 60.0f;

	Simulation variable;
	setup(variable);
	variable.set_fixed_timestep(fixed);
	variable.set_max_substeps(64);
	Pcg32 rng(7);
	uint32_t total_steps = 0;
	for (int f = 0; f < 200; f++) {
		total_steps += variable.advance(rng.range(0.0f, 0.05f));
		CHECK(variable.get_interpolation_alpha() >= 0.0f);
		CHECK(variable.get_interpolation_alpha() < 1.0f);
	}
	REQUIRE(total_steps > 100);

	Simulation reference;
	setup(reference);
	for (uint32_t s = 0; s < total_steps; s++) {
		reference.step(fixed);
	}
	CHECK(same_state(variable, reference));
}

TEST_CASE("Fixed timestep: max_substeps bounds the work per frame") {
	Simulation sim;
	sim.add_type(basic_type());
	sim.spawn(0, Vec2());
	sim.set_fixed_timestep(1.0f / 60.0f);
	sim.set_max_substeps(4);
	CHECK(sim.advance(1.0f) == 4); // A 1 s hitch does not run 60 steps.
	CHECK(sim.advance(1.0f / 60.0f) == 1);
}

TEST_CASE("Random operations keep handles and dense storage consistent") {
	Simulation sim;
	sim.set_seed(5);
	sim.add_type(basic_type(5.0f));
	sim.add_type(basic_type(9.0f));
	Pcg32 rng(77);
	std::vector<AgentId> known;
	std::vector<AgentId> dead;
	for (int op = 0; op < 3000; op++) {
		const float r = rng.next_float();
		if (r < 0.4f) {
			const uint16_t type = static_cast<uint16_t>(rng.next_u32() % 2);
			known.push_back(sim.spawn(type, Vec2(rng.range(-300.0f, 300.0f), rng.range(-300.0f, 300.0f))));
		} else if (r < 0.6f && !known.empty()) {
			const AgentId id = known[rng.next_u32() % known.size()];
			if (sim.kill(id)) {
				dead.push_back(id);
			}
		} else if (r < 0.7f) {
			std::vector<AgentId> hits;
			sim.damage_circle(Vec2(rng.range(-300.0f, 300.0f), rng.range(-300.0f, 300.0f)), 40.0f, 4.0f, hits);
			for (AgentId id : hits) {
				if (!sim.is_alive(id)) {
					dead.push_back(id);
				}
			}
		} else {
			sim.advance(DT);
		}
		if (op % 100 == 0) {
			check_handle_invariants(sim);
		}
	}
	check_handle_invariants(sim);
	for (AgentId id : dead) {
		CHECK_FALSE(sim.is_alive(id));
	}
}
