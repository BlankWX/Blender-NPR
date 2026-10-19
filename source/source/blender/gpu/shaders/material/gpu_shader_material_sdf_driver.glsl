/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/* NPR SDF Driver node — sun-driven SDF terminator with feathered mask.
 *
 * calc_sdf_light_dir() returns the world-space direction toward the strongest
 * visible light (engine side, see eevee_forward_lib.bsl.hh). In object space
 * the direction reduces to an azimuth
 *   theta = atan(L.x, -L.y)
 * with forward = -Y, +X = character's left (theta > 0 = light from the
 * character's left).
 *
 *   threshold = 0.5 + bias + gain * (|theta| - ref_angle)
 *       t(0 deg) ~ 0 (fully lit), t(90 deg) = 0.5 (half lit),
 *       t(180 deg) ~ 0.99 (full shadow) with the default Gain/Ref.
 *   flip = (theta < 0) ? 1 : 0
 *       sun on the character's right -> the SAMPLE mirrors U (u -> 1 - u).
 *       Geometric mirror, not value inversion: the field is not
 *       anti-symmetric (measured max deviation 0.5), so inversion keeps
 *       baked features (nose shadow) pinned on the wrong side when the
 *       light crosses the meridian. Mirror moves them with the light.
 *   gate = smoothstep(-0.55, -0.15, -L.y)
 *       backlight gate: 0 once the sun passes behind the head, and 0 when
 *       the scene has no light — multiplied into the mask.
 *
 * Anti-aliasing: Soften feathers the comparison with a value-domain
 * smoothstep band of width `soften` centered on the threshold — the
 * same feel as the Half-Lambert two-tone node. Soften = 0 keeps the hard
 * step. (Screen-space gradient-normalized feather is NOT used: at feature
 * edges the gradient spikes blow the band open and smear the nose shadow
 * into blotches — rejected by user. The destriped 16-bit field keeps
 * this band clean without dither.)
 */

/* Shared two-tone shading: Half-Lambert-style value feather + gate. */
float sdf_driver_shade(float s, float t, float soften, float gate)
{
  float m;
  if (soften <= 0.0f) {
    m = (s > t) ? 1.0f : 0.0f;
  }
  else {
    /* Value-domain feather, identical to node_half_lambert. */
    float lo = t - soften * 0.5f;
    float hi = t + soften * 0.5f;
    m = smoothstep(lo, hi, s);
  }
  return m * gate;
}

/* Main path: upstream Image Texture found — sample it ourselves at the
 * sun-mirrored UV. `sdf` is only the link that located the image.
 * Threshold / Flip / Gate are internal only (Mask is the single output). */
[[node]]
void node_sdf_driver(float4 sdf,
                     float ref_angle,
                     float gain,
                     float bias,
                     float soften,
                     float sun_id,
                     float3 co,
                     sampler2D ima,
                     out float mask)
{
#ifdef GPU_VERTEX_SHADER
  mask = 1.0f;
#else
  float3 L = calc_sdf_light_dir(sun_id);
  if (dot(L, L) < 1e-8f) {
    /* No light in the scene: full shadow. */
    mask = 0.0f;
  }
  else {
    float3 Lo;
    normal_transform_world_to_object(L, Lo);

    float theta = atan(Lo.x, -Lo.y);
    float threshold = 0.5f + bias + gain * (abs(theta) - ref_angle);
    float flip = (theta < 0.0f) ? 1.0f : 0.0f;
    float gate = smoothstep(-0.55f, -0.15f, -Lo.y);

    /* Mirror U across the face midline when the sun is on the character's
     * right — baked features follow the light side.
     *
     * LOD 0 + 9-tap gaussian along U (sigma = 2.5 texels, weights
     * exp(-i^2/12.5) normalized). The baked field is columnar (95%
     * identical v-neighbours) with 8-bit level steps every ~4 texels;
     * each level shows as a vertical stripe through the feather band.
     * This kernel attenuates that period to <0.1% while nose-scale
     * features (15-40 texels) stay intact. No mip: its block filter
     * turns the same stripes into 2-4px blocks instead. */
    float2 uv = float2((flip > 0.5f) ? (1.0f - co.x) : co.x, co.y);
    float2 t = 1.0f / float2(textureSize(ima, 0).xy);
    float s = 0.1716f * textureLod(ima, uv, 0.0f).r;
    s += 0.1584f * (textureLod(ima, uv + float2(t.x, 0.0f), 0.0f).r +
                    textureLod(ima, uv - float2(t.x, 0.0f), 0.0f).r);
    s += 0.1246f * (textureLod(ima, uv + float2(2.0f * t.x, 0.0f), 0.0f).r +
                    textureLod(ima, uv - float2(2.0f * t.x, 0.0f), 0.0f).r);
    s += 0.0836f * (textureLod(ima, uv + float2(3.0f * t.x, 0.0f), 0.0f).r +
                    textureLod(ima, uv - float2(3.0f * t.x, 0.0f), 0.0f).r);
    s += 0.0477f * (textureLod(ima, uv + float2(4.0f * t.x, 0.0f), 0.0f).r +
                    textureLod(ima, uv - float2(4.0f * t.x, 0.0f), 0.0f).r);
    mask = sdf_driver_shade(s, threshold, soften, gate);
  }
#endif
}

/* Fallback path: no upstream image (SDF fed as a plain value chain).
 * Degraded hemisphere cover by value inversion (field not mirrored) —
 * link an Image Texture to the SDF input for the correct mirror behavior. */
[[node]]
void node_sdf_driver_fallback(float4 sdf,
                              float ref_angle,
                              float gain,
                              float bias,
                              float soften,
                              float sun_id,
                              out float mask)
{
#ifdef GPU_VERTEX_SHADER
  mask = 1.0f;
#else
  float3 L = calc_sdf_light_dir(sun_id);
  if (dot(L, L) < 1e-8f) {
    mask = 0.0f;
  }
  else {
    float3 Lo;
    normal_transform_world_to_object(L, Lo);

    float theta = atan(Lo.x, -Lo.y);
    float threshold = 0.5f + bias + gain * (abs(theta) - ref_angle);
    float flip = (theta < 0.0f) ? 1.0f : 0.0f;
    float gate = smoothstep(-0.55f, -0.15f, -Lo.y);

    float s = (flip > 0.5f) ? (1.0f - sdf.r) : sdf.r;
    mask = sdf_driver_shade(s, threshold, soften, gate);
  }
#endif
}
