# Erosion clean licensing situation

This is a working engineering inventory, not legal advice. It summarizes the local files under `doc/erosion_clean` and the cited reference material supplied in the per-ShaderToy-ID folders.

## Source files reviewed

Primary root implementation files:

- `doc/erosion_clean/buffer_a.txt`
- `doc/erosion_clean/buffer_b.txt`
- `doc/erosion_clean/image.txt`
- `doc/erosion_clean/common.txt`

Reference folders present under `doc/erosion_clean`:

- `7ljcRW`
- `MtGcWh`
- `llsGWl`
- `XdXBRH`
- `Xd23Dh`
- `XlsGDs`
- `XsB3Rm`
- `XlKSDR`

No expected reference folder was empty.

## Comparison with `doc/erosion_shader`

`doc/erosion_shader` contains:

- `buffer_a.txt`
- `buffer_b.txt`
- `common.txt`

Those three files are byte-for-byte identical to the same files in `doc/erosion_clean`.

`doc/erosion_clean/image.txt` has no counterpart in `doc/erosion_shader`.

## Direct citations in the root implementation

| Citation | Local reference | Apparent license | Material code copied or referenced? | Used in erosion or height calculation? | Notes |
|---|---|---:|---|---|---|
| `https://www.shadertoy.com/view/7ljcRW` | `doc/erosion_clean/7ljcRW` | MIT text is present in `7ljcRW/buffer_a.txt`; the root file also carries `Copyright 2023 Fewes` under an MIT grant. | Yes. The root shader says it is originally derived from Fewes' "Terrain Erosion Noise". `buffer_b.txt` is identical to the `7ljcRW` copy; `image.txt` and `common.txt` are close derivatives; `buffer_a.txt` is a substantial refactor of the erosion/height pipeline. | Yes. This is the immediate parent of the root erosion/height implementation. | MIT is compatible with the target licensing direction. Do not replace solely for licensing reasons, but preserve attribution/license text if any MIT-derived code remains. |
| `https://www.shadertoy.com/view/MtGcWh` | `doc/erosion_clean/MtGcWh` | MIT text is present in `MtGcWh/buffer_a.txt`. | Yes. Root comments include Clay John's method description and the code lineage goes through Fewes back to Clay John's `mountain`/`erosion` approach. | Yes. The height/noise/erosion structure is materially descended from this shader. | MIT is compatible with the target licensing direction. Do not replace solely for licensing reasons, but preserve attribution/license text if any MIT-derived code remains. |
| `https://www.shadertoy.com/view/llsGWl` | `doc/erosion_clean/llsGWl` | Creative Commons Attribution-NonCommercial-ShareAlike 3.0 Unported is explicitly stated in `llsGWl/image.txt`; also cited as CC BY-NC-SA in `7ljcRW/buffer_a.txt` and `MtGcWh/buffer_a.txt`. | Yes. Clay/Fewes cite the code as adapted from Gavoronoise, and the erosion/gully function retains the material pattern: jittered cell points, weighted cell contribution, and directional cosine waves. The root `Gullies` function is rewritten and corrected, but still materially references that lineage. | Yes. This is central to the gully/erosion noise calculation. | Highest concern because the explicit license is noncommercial/share-alike. |
| `https://www.shadertoy.com/view/XdXBRH` | `doc/erosion_clean/XdXBRH` | MIT text is present in `XdXBRH/image.txt`. | Yes. `common.txt` contains `hash`/`noised` gradient noise code from this source, with the `noised` function directly cited. | Yes. `FractalNoise` uses `noised` to create the base height and derivatives passed into erosion. `buffer_b.txt` also uses `noised` for detail texture, but that use is rendering/detail only. | MIT is compatible with the target licensing direction. Do not replace solely for licensing reasons, but preserve attribution/license text if any MIT-derived code remains. |
| `https://iquilezles.org/articles/intersectors` | No local folder; cited directly in `common.txt`. | Not determined from local snapshot. | Yes, likely the `boxIntersection` helper is copied or closely adapted. | No. It is rendering/ray-intersection support, not part of height or erosion. | Not relevant to erosion cleanup unless the demo shader as a whole must be cleaned. |
| `https://www.shadertoy.com/view/XsB3Rm` | `doc/erosion_clean/XsB3Rm` | No explicit license found locally; treat as ShaderToy default, i.e. CC BY-NC-SA-style noncommercial/share-alike terms. | Yes. `CameraRay` and `CameraRotation` in `common.txt` closely match `ray_dir` and `rotationXY` in the reference. | No. Camera setup/rendering only. | Not relevant to erosion/height cleanup. |
| `https://www.shadertoy.com/view/XlKSDR` | `doc/erosion_clean/XlKSDR` | No explicit license found locally; treat as ShaderToy default, i.e. CC BY-NC-SA-style noncommercial/share-alike terms. | Yes. BRDF functions such as `pow5`, `D_GGX`, `V_SmithGGXCorrelated`, `F_Schlick`, `Fd_Burley`, `Fd_Lambert`, and `Tonemap_ACES` closely match the reference. | No. Shading/rendering only. | Not relevant to erosion/height cleanup. |

## Transitive citations from cited references

These are not directly cited by the root implementation, but appear in the local references:

| Citation | Local reference | Apparent license | Relationship to root implementation |
|---|---|---:|---|
| `https://www.shadertoy.com/view/Xd23Dh` | `doc/erosion_clean/Xd23Dh` | MIT text is present. | Cited by `llsGWl` as Voronoise by iq. The root code does not directly copy the `voronoise` function, but the Gavoronoise lineage passes through `llsGWl`. |
| `https://www.shadertoy.com/view/XlsGDs` | `doc/erosion_clean/XlsGDs` | No explicit license found locally; treat as ShaderToy default, i.e. CC BY-NC-SA-style noncommercial/share-alike terms. | Cited by `llsGWl` as "Gabor 4: normalized". The root code does not directly copy this file, but it is part of the Gavoronoise ancestry. |

## Other named references in rendering comments

`common.txt` contains comments naming rendering/math references such as Walter 2007, Heitz 2014, Schlick 1994, Burley 2012, Narkowicz 2015, and atmospheric constants. These are attached to BRDF, tone mapping, and atmosphere code. They are not used in the erosion or height calculation.

## Erosion/height dependency chain

The erosion-relevant root code is concentrated in:

- `common.txt`: erosion parameters, base height parameters, `hash`, and `noised`.
- `buffer_a.txt`: `Gullies`, `Erosion`, `FractalNoise`, `MagnitudeSum`, and `Heightmap`.

The core dependency chain is:

1. `Heightmap` builds base terrain with `FractalNoise`.
2. `FractalNoise` calls `noised`, cited from `XdXBRH` and MIT licensed.
3. `Heightmap` passes the base height and derivatives into `Erosion`.
4. `Erosion` repeatedly calls `Gullies`.
5. `Gullies` is the rewritten descendant of the Clay/Fewes erosion function, which itself cites Gavoronoise from `llsGWl`.

The main licensing problem for a clean erosion implementation is therefore not the rendering code. It is the material connection between `Gullies`/`Erosion` and the Gavoronoise-derived ShaderToy chain, especially `llsGWl`'s explicit CC BY-NC-SA license.

## Deep dive: Gavoronoise, Gabor, Voronoise, and `Gullies`

The user concern is whether the implementation actually needs the CC BY-NC-SA Gabor/Gavoronoise material, or whether the useful part can be understood as Voronoi-style cell enumeration covered by iq's MIT Voronoise reference.

### `XlsGDs` Gabor reference

`XlsGDs/image.txt` implements complex Gabor noise with:

- a global point distribution loop over `NB 600`;
- `Gabor(pos, freq, a)`, using a Gaussian envelope and sinusoidal carrier;
- `GaborNoise(uv, freq, dir)`, summing many point kernels;
- optional experiments involving quadrilobes, rings, Bessel approximations, amplitude normalization, and display modes.

I do not see material code from this file in the root implementation:

- no `NB`-style global iteration count;
- no `Gabor` or `GaborNoise` function;
- no `gauss`, `BesselJ0`, `BesselJ1`, `rnd`, or `rndi`;
- no complex two-channel Gabor output or normalization by accumulated amplitude;
- no random global point distribution.

The root `Gullies` function does use cosine waves with a distance weight. Conceptually, that resembles the broad idea of localized sinusoidal kernels, but it does not appear to copy the concrete `XlsGDs` implementation. On the local evidence, `XlsGDs` should be treated as transitive inspiration through `llsGWl`, not as code that must be replaced directly.

### MIT Voronoise reference

`Xd23Dh/image.txt` implements Voronoise under MIT. Relevant implementation elements include:

- `floor(p)` and `fract(p)`;
- enumeration of nearby grid cells;
- per-cell jitter from a hash;
- computing a vector from the sample point to the jittered cell point;
- distance-based weighting;
- normalizing by total weight.

These concepts are enough to explain the cell-enumeration scaffold used by Gavoronoise and the root `Gullies` function. If a replacement only needs "nearby jittered cells with weighted blending," the MIT Voronoise source is a cleaner foundation than `llsGWl`.

### `llsGWl` Gavoronoise reference

`llsGWl/image.txt` combines a Voronoi-like local cell loop with a directional cosine wave. The function most relevant to the root lineage is `gavoronoi4`:

- enumerate a 4x4 neighborhood of grid cells;
- jitter each cell point with `hash2`;
- compute `pp`, the vector from the jittered cell point to the sample;
- compute `w = exp(-d*2.0)`;
- add `cos(dot(pp, h) * f) * w`;
- divide by total weight.

This is not the same as `XlsGDs`'s global 600-blob Gabor implementation. It is a compact local-cell hybrid: Voronoi-style point enumeration plus weighted directional cosine stripes.

### Root `Gullies` function

The root `Gullies` function has removed or changed several `llsGWl`/Gavoronoise details:

- it uses the MIT `hash` function from `XdXBRH` rather than `llsGWl`'s `hash2`;
- it does not use `dv*h + dir` to perturb the stripe direction per cell;
- it supplies direction from terrain slope instead of mouse/global direction;
- it returns analytic derivatives;
- it adjusts derivative magnitude and parameter semantics;
- it subtracts a small amount from the weight to reduce grid-line artifacts;
- it is wrapped in a clearer `Erosion` filter API.

However, the material expression still follows the Gavoronoise-shaped kernel:

- 4x4 jittered-cell neighborhood;
- per-cell vector from jittered point to sample;
- exponential distance weight;
- cosine wave evaluated from a directional dot product;
- weighted sum divided by weight sum.

The conclusion is therefore narrower than "Gabor must be replaced": the root does not appear to require `XlsGDs`'s Gabor implementation. The live licensing issue is the `llsGWl` Gavoronoise expression, specifically its local-cell weighted directional cosine kernel. A clean rewrite could probably keep MIT Voronoise-style cell enumeration, but should avoid copying the Gavoronoise kernel shape from `llsGWl`.

## Deep-dive replacement guidance

The likely clean path is:

1. Keep MIT sources where useful (`7ljcRW`, `MtGcWh`, `XdXBRH`, `Xd23Dh`) with attribution/license preservation.
2. Treat `XlsGDs` as not materially present in the root code unless future review finds a more direct copied fragment.
3. Replace the `llsGWl`-derived part by reimplementing from MIT/first-principles:
   - use MIT Voronoise-style jittered cell enumeration if needed;
   - design a new per-cell contribution function, weight function, and derivative model;
   - avoid the `gavoronoi4` expression pattern of `exp(-d*2)` times `cos(dot(pp, direction)*2*pi)` normalized by total weight;
   - avoid retaining the exact 4x4 loop bounds/offset convention as a recognizable package with the same kernel, unless independently justified.
4. Revisit `Erosion` only where it depends on the Gavoronoise-style `Gullies` output. The broad octave accumulation and slope feedback are more strongly connected to the MIT Clay/Fewes/Rune lineage, so they are not the same licensing blocker as `llsGWl`.

## What would need replacement

To make the reusable erosion/height calculation clean, replace or independently rederive at least:

- `Gullies`: This is the central non-clean part. It should be replaced with an independently designed gully/noise function that can use MIT Voronoise-style cell enumeration, but should not copy the Gavoronoise weighted directional cosine kernel from `llsGWl`.
- The `llsGWl`-derived kernel assumptions inside `Erosion`: the accumulation can probably keep the MIT Clay/Fewes/Rune high-level approach, but it should be reviewed after `Gullies` is replaced because its current behavior is tuned around the Gavoronoise-shaped output.

The following do not need replacement solely for licensing reasons:

- `7ljcRW` / Fewes lineage, where the local reference is MIT.
- `MtGcWh` / Clay John lineage, where the local reference is MIT.
- `XdXBRH` `noised` / `hash`, where the local reference is MIT.
- `Xd23Dh` Voronoise, where the local reference is MIT and can be used as a cleaner basis for cell enumeration.
- `XlsGDs` Gabor, because the concrete implementation does not appear materially present in the root code.

If the goal is only to clean the erosion algorithm for reuse in the Godot project, the rendering-only pieces can be ignored:

- `image.txt`
- `buffer_b.txt`
- `boxIntersection`
- `CameraRay` / `CameraRotation`
- BRDF, tone mapping, sky, atmosphere, water, material-coloring, and detail-texture helpers

If the whole ShaderToy demo under `doc/erosion_clean` must be clean, then the rendering citations with missing or default ShaderToy licenses also need replacement, especially the `XsB3Rm` camera helpers, `XlKSDR` BRDF block, and the iquilez `boxIntersection` helper.
