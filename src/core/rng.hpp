// Small deterministic PRNG (PCG32, O'Neill 2014). Same sequence on every platform,
// unlike std:: distributions whose output is implementation-defined.
#pragma once

#include <cstdint>

namespace horde {

class Pcg32 {
public:
	explicit Pcg32(uint64_t p_seed = 0x853c49e6748fea9bULL) { seed(p_seed); }

	void seed(uint64_t p_seed) {
		state_ = 0u;
		next_u32();
		state_ += p_seed;
		next_u32();
	}

	uint32_t next_u32() {
		const uint64_t old = state_;
		state_ = old * 6364136223846793005ULL + INCREMENT;
		const uint32_t xorshifted = static_cast<uint32_t>(((old >> 18u) ^ old) >> 27u);
		const uint32_t rot = static_cast<uint32_t>(old >> 59u);
		return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
	}

	// Uniform float in [0, 1). Uses the top 24 bits so every value is exactly representable.
	float next_float() { return static_cast<float>(next_u32() >> 8) * (1.0f / 16777216.0f); }

	// Uniform float in [p_min, p_max).
	float range(float p_min, float p_max) { return p_min + (p_max - p_min) * next_float(); }

private:
	static constexpr uint64_t INCREMENT = 1442695040888963407ULL;
	uint64_t state_ = 0;
};

} // namespace horde
