#ifndef ZN_RUNE_NOISE_H
#define ZN_RUNE_NOISE_H

#include "../math/constants.h"
#include "../math/funcs.h"
#include "../math/vector2f.h"
#include "../math/vector3f.h"

namespace zylann::voxel {

struct RuneNoiseParams {
	int32_t seed;
	float erosion_scale;
	float erosion_strength;
	float erosion_slope_power;
	float erosion_cell_scale;
	float erosion_height_offset;
	int erosion_octaves;
	float erosion_gain;
	float erosion_lacunarity;
	float height_tiles;
	int height_octaves;
	float height_amp;
	float height_gain;
	float height_lacunarity;
	float water_height;
};

struct RuneNoiseOutput {
	float height;
	float erosion;
};

inline Vector2f rune_fract(Vector2f v) {
	return Vector2f(math::fract(v.x), math::fract(v.y));
}

inline Vector2f rune_hash(Vector2f x, int32_t seed) {
	const Vector2f k(0.3183099f, 0.3678794f);
	x += Vector2f(seed * 0.1031f, seed * 0.11369f);
	x = x * k + Vector2f(k.y, k.x);
	const float h = math::fract(x.x * x.y * (x.x + x.y));
	return Vector2f(-1.f) + (2.f * rune_fract(16.f * k * h));
}

// Gradient noise with analytical derivatives, ported from the Shadertoy common.txt `noised` function.
inline Vector3f rune_noised(Vector2f p, int32_t seed) {
	const Vector2f i = math::floor(p);
	const Vector2f f = rune_fract(p);

	const Vector2f f2 = f * f;
	const Vector2f f3 = f2 * f;
	const Vector2f u = f3 * (f * (f * 6.f - Vector2f(15.f)) + Vector2f(10.f));
	const Vector2f du = 30.f * f2 * (f * (f - Vector2f(2.f)) + Vector2f(1.f));

	const Vector2f ga = rune_hash(i + Vector2f(0.f, 0.f), seed);
	const Vector2f gb = rune_hash(i + Vector2f(1.f, 0.f), seed);
	const Vector2f gc = rune_hash(i + Vector2f(0.f, 1.f), seed);
	const Vector2f gd = rune_hash(i + Vector2f(1.f, 1.f), seed);

	const float va = math::dot(ga, f - Vector2f(0.f, 0.f));
	const float vb = math::dot(gb, f - Vector2f(1.f, 0.f));
	const float vc = math::dot(gc, f - Vector2f(0.f, 1.f));
	const float vd = math::dot(gd, f - Vector2f(1.f, 1.f));
	const float k = va - vb - vc + vd;

	const Vector2f derivative =
			ga + u.x * (gb - ga) + u.y * (gc - ga) + (u.x * u.y) * (ga - gb - gc + gd) +
			du * (Vector2f(u.y, u.x) * k + Vector2f(vb, vc) - Vector2f(va));

	return Vector3f(va + u.x * (vb - va) + u.y * (vc - va) + u.x * u.y * k, derivative.x, derivative.y);
}

inline Vector3f rune_gullies(Vector2f p, Vector2f slope, int32_t seed) {
	const Vector2f side_dir = Vector2f(-slope.y, slope.x) * (2.f * math::PI<float>);
	const Vector2f p_int = math::floor(p);
	const Vector2f p_frac = rune_fract(p);
	Vector3f height_and_slope;
	float weight_sum = 0.f;

	for (int i = -1; i <= 2; ++i) {
		for (int j = -1; j <= 2; ++j) {
			const Vector2f grid_offset(i, j);
			const Vector2f grid_point = p_int + grid_offset;
			const Vector2f random_offset = rune_hash(grid_point, seed) * 0.5f;
			const Vector2f vector_from_cell_point = p_frac - grid_offset - random_offset;
			const float sqr_dist = math::dot(vector_from_cell_point, vector_from_cell_point);
			const float weight = math::max(0.f, Math::exp(-sqr_dist * 2.f) - 0.01111f);
			weight_sum += weight;

			const float wave_input = math::dot(vector_from_cell_point, side_dir);
			const float slope_scale = -Math::sin(wave_input);
			height_and_slope +=
					Vector3f(Math::cos(wave_input), slope_scale * side_dir.x, slope_scale * side_dir.y) * weight;
		}
	}

	if (weight_sum == 0.f) {
		return Vector3f();
	}
	return height_and_slope / weight_sum;
}

inline Vector3f rune_fractal_noise(
		Vector2f p,
		float freq,
		int octaves,
		float lacunarity,
		float gain,
		float height_amp,
		int32_t seed
) {
	Vector3f n;
	float nf = freq;
	float na = 1.f;
	for (int i = 0; i < octaves; ++i) {
		n += rune_noised(p * nf, seed) * na * Vector3f(1.f, nf, nf);
		na *= gain;
		nf *= lacunarity;
	}
	return n * height_amp;
}

inline float rune_magnitude_sum(int octaves, float gain) {
	if (octaves <= 0) {
		return 0.f;
	}
	if (Math::is_equal_approx(gain, 1.f)) {
		return octaves;
	}
	return (1.f - Math::pow(gain, static_cast<float>(octaves))) / (1.f - gain);
}

inline Vector3f rune_erosion(
		Vector2f p,
		Vector3f height_and_slope,
		float scale,
		float strength,
		float slope_power,
		float cell_scale,
		int octaves,
		float gain,
		float lacunarity,
		int32_t seed
) {
	const Vector3f input_height_and_slope = height_and_slope;
	float freq = 1.f / (scale * cell_scale);
	strength *= scale;
	for (int i = 0; i < octaves; ++i) {
		const float sqr_len = height_and_slope.y * height_and_slope.y + height_and_slope.z * height_and_slope.z;
		const float slope_factor = sqr_len > 0.f ? Math::pow(sqr_len, 0.5f * (slope_power - 1.f)) : 0.f;
		const Vector2f input_slope(height_and_slope.y * slope_factor, height_and_slope.z * slope_factor);

		height_and_slope +=
				rune_gullies(p * freq, input_slope * cell_scale, seed) * strength * Vector3f(1.f, freq, freq);

		strength *= gain;
		freq *= lacunarity;
	}
	return height_and_slope - input_height_and_slope;
}

inline RuneNoiseOutput get_rune_noise_2d(Vector2f p, const RuneNoiseParams &params) {
	Vector3f n = rune_fractal_noise(
			p,
			params.height_tiles,
			params.height_octaves,
			params.height_lacunarity,
			params.height_gain,
			params.height_amp,
			params.seed
	);
	n = n * 0.5f + Vector3f(0.5f, 0.f, 0.f);

	const float strength = params.erosion_strength *
			math::smoothstep(params.water_height - 0.1f, params.water_height + 0.1f, n.x);
	const Vector3f h = rune_erosion(
			p,
			n,
			params.erosion_scale,
			strength,
			params.erosion_slope_power,
			params.erosion_cell_scale,
			params.erosion_octaves,
			params.erosion_gain,
			params.erosion_lacunarity,
			params.seed
	);
	const float erosion_magnitude =
			params.erosion_scale * strength * rune_magnitude_sum(params.erosion_octaves, params.erosion_gain);

	RuneNoiseOutput out;
	out.height = n.x + h.x + erosion_magnitude * params.erosion_height_offset;
	out.erosion = erosion_magnitude > 0.f ? h.x / erosion_magnitude : 0.f;
	return out;
}

} // namespace zylann::voxel

#endif // ZN_RUNE_NOISE_H
