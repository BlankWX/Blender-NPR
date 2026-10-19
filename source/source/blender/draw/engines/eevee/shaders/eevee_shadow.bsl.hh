/* SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include "draw_view.bsl.hh"
#include "eevee_sampling_lib.bsl.hh"
#include "eevee_shadow_shared.hh"
#include "eevee_shadow_tilemap_lib.bsl.hh"
#include "eevee_uniform.bsl.hh"
#include "eevee_utility_tx.bsl.hh"
#include "gpu_shader_utildefines_lib.glsl"

namespace eevee {

/* -------------------------------------------------------------------- */
/** \name NPR caster-ID filtering (legacy, disabled)
 *
 * The shadow atlas depth pass is followed by an ID resolve pass that previously
 * recorded the identity of the depth winner in a sidecar atlas. That path is kept
 * only for the stock disabled filter; no NPR node enables `IgnoreSelf` anymore.
 *
 * Fringe Shadow reuses the same sidecar with different contents: marked hair
 * materials write their light-space depth during the ID-resolve stage, and the face
 * node samples it as a hair-only shadow map. `read_hair_depth()` is the current
 * consumer; `read_caster_id()` and `shadow_id_sample_filtered()` are legacy.
 * \{ */

enum class ShadowIdMode : uint {
  /* No identity filtering: stock EEVEE-Next behaviour. */
  Disabled = 0u,
  /* Reject taps whose caster IS the receiver (cast/contact shadows only). */
  IgnoreSelf = 1u,
};

struct ShadowIdFilter {
  ShadowIdMode mode;
  uint receiver_id;
};

ShadowIdFilter shadow_id_filter_disabled()
{
  return ShadowIdFilter{ShadowIdMode::Disabled, 0u};
}

ShadowIdFilter shadow_id_filter_ignore_self(uint receiver_id)
{
  return ShadowIdFilter{ShadowIdMode::IgnoreSelf, receiver_id};
}

/* SHADOW_ID_INVALID (the "identity unknown" sentinel) is defined in eevee_shadow_shared.hh
 * so the page-clear pass shares it. */

/** \} */

/* Any entry point function using this should also use `[[texture_atomic]]`. */
struct ShadowRenderData {
  [[sampler(SHADOW_ATLAS_TEX_SLOT)]] usampler2DArrayAtomic shadow_atlas_tx;
  [[sampler(SHADOW_TILEMAPS_TEX_SLOT)]] usampler2D shadow_tilemaps_tx;
  /* NPR: caster-ID sidecar mirroring shadow_atlas_tx layout (UINT resource id).
   * Resolved through the same atlas page math as the depth, so identity and depth
   * always describe the same texel. */
  [[sampler(SHADOW_ID_TEX_SLOT)]] usampler2DArray shadow_id_tx;

  [[compilation_constant]] bool shadow_random;

  [[resource_table]] srt_t<Uniform> uniforms;
  [[resource_table]] srt_t<draw::View> views;
  [[resource_table, condition(shadow_random)]] srt_t<Sampling> sampling;
  [[resource_table, condition(shadow_random)]] srt_t<UtilityTexture> util_tx;

  /* Virtual shadow coordinates -> physical atlas texel. Shared by every sidecar read so
   * they cannot drift apart. Zero-initialised texel when the tile is invalid. */
  int3 atlas_texel(ShadowCoordinates coord, bool &is_valid) const
  {
    ShadowSamplingTile tile = shadow_tile_load(
        shadow_tilemaps_tx, coord.tilemap_tile, coord.tilemap_index);
    if (!tile.is_valid) {
      is_valid = false;
      return int3(0);
    }
    /* Using bitwise ops is way faster than integer ops. */
    constexpr uint page_shift = uint(SHADOW_PAGE_LOD);
    constexpr uint page_mask = ~(0xFFFFFFFFu << uint(SHADOW_PAGE_LOD));

    uint2 texel = coord.tilemap_texel;
    /* Shift LOD0 pixels so that they get wrapped at the right position for the given LOD. */
    texel += uint2(tile.lod_offset << SHADOW_PAGE_LOD);
    /* Scale to LOD pixels (merge LOD0 pixels together) then mask to get pixel in page. */
    uint2 texel_page = (texel >> tile.lod) & page_mask;
    texel = (uint2(tile.page.xy) << page_shift) | texel_page;

    is_valid = true;
    return int3(int2(texel), int(tile.page.z));
  }

  float read_depth(ShadowCoordinates coord) const
  {
    bool is_valid;
    int3 texel = atlas_texel(coord, is_valid);
    if (!is_valid) {
      return -1.0f;
    }
    return uintBitsToFloat(texelFetch(shadow_atlas_tx, texel, 0).r);
  }

  /* Caster identity recorded by the ID resolve pass. SHADOW_ID_INVALID where the depth
   * winner was never resolved (the sentinel itself keeps idle padding texels quiet). */
  uint read_caster_id(ShadowCoordinates coord) const
  {
    bool is_valid;
    int3 texel = atlas_texel(coord, is_valid);
    if (!is_valid) {
      return SHADOW_ID_INVALID;
    }
    return texelFetch(shadow_id_tx, texel, 0).r;
  }

  /* Two independent nearest depths. Channel 2 is their union, not a tag on
   * a single winning fragment (which would lose an occluded hair/cloth layer). */
  float read_occlusion_depth(ShadowCoordinates coord, int channel) const
  {
    bool is_valid;
    int3 texel = atlas_texel(coord, is_valid);
    if (!is_valid) {
      return -1.0f;
    }
    texel.z *= 2;
    uint raw = SHADOW_ID_INVALID;
    if (channel != 1) {
      raw = texelFetch(shadow_id_tx, texel, 0).r;
    }
    if (channel != 0) {
      texel.z += 1;
      raw = min(raw, texelFetch(shadow_id_tx, texel, 0).r);
    }
    return raw == SHADOW_ID_INVALID ? -1.0f : uintBitsToFloat(raw);
  }

  /* Filter comparison results, never the depths: mixing a hair depth with an
   * empty texel would create false blockers. Resolve each tap through the
   * virtual page table; adjacent atlas texels need not be adjacent in the map. */
  float directional_occlusion_pcf(LightData light,
                                  float3 lP,
                                  int level,
                                  float receiver_dist,
                                  float bias,
                                  int channel,
                                  float radius) const
  {
    const float2 uv = shadow_directional_uv_at_level(light, lP, level);
    const int map_index = light.tilemap_index + level - light.sun().clipmap_lod_min;
    const ShadowCoordinates center = shadow_coordinate_from_uvs(map_index, uv);
    const ShadowSamplingTile tile = shadow_tile_load(
        shadow_tilemaps_tx, center.tilemap_tile, center.tilemap_index);
    if (!tile.is_valid) {
      return 0.0f;
    }
    const float texel_scale = exp2(float(tile.lod));
    const float map_res = float(SHADOW_MAP_MAX_RES) - 1e-2f;
    const float2 texel = uv * (map_res / texel_scale) - 0.5f;
    const float2 base = floor(texel);
    const float2 fraction = fract(texel);
    const int map_lod = light.type == LIGHT_SUN_ORTHO ? light.sun().clipmap_lod_min : level;
    const float world_texel = exp2(float(map_lod)) * texel_scale / map_res;
    /* Reconstruct under-resolved hard silhouettes with a positive cubic
     * B-spline. An isolated missing texel no longer turns into a white dot
     * after the cel threshold. Fade to bilinear PCF once the artistic disk
     * spans a texel; that disk already performs the reconstruction. */
    const float reconstruct = 1.0f - smoothstep(0.0f, world_texel, radius);
    const float2 f2 = fraction * fraction;
    const float2 f3 = f2 * fraction;
    const float2 one_minus_f = 1.0f - fraction;
    float4 wx = float4(one_minus_f.x * one_minus_f.x * one_minus_f.x,
                      3.0f * f3.x - 6.0f * f2.x + 4.0f,
                      -3.0f * f3.x + 3.0f * f2.x + 3.0f * fraction.x + 1.0f,
                      f3.x) * (1.0f / 6.0f);
    float4 wy = float4(one_minus_f.y * one_minus_f.y * one_minus_f.y,
                      3.0f * f3.y - 6.0f * f2.y + 4.0f,
                      -3.0f * f3.y + 3.0f * f2.y + 3.0f * fraction.y + 1.0f,
                      f3.y) * (1.0f / 6.0f);
    wx = mix(float4(0.0f, 1.0f - fraction.x, fraction.x, 0.0f), wx, reconstruct);
    wy = mix(float4(0.0f, 1.0f - fraction.y, fraction.y, 0.0f), wy, reconstruct);
    const int first = reconstruct > 0.0f ? 0 : 1;
    const int last = reconstruct > 0.0f ? 4 : 3;
    float coverage = 0.0f;
    for (int y = first; y < last; y++) {
      for (int x = first; x < last; x++) {
        const float2 tap_uv = saturate((base + float2(x, y) - 0.5f) *
                                      (texel_scale / map_res));
        const ShadowCoordinates coord = shadow_coordinate_from_uvs(map_index, tap_uv);
        const float depth = read_occlusion_depth(coord, channel);
        if (depth >= 0.0f) {
          coverage += wx[x] * wy[y] * smoothstep(bias, bias + 1e-5f, receiver_dist - depth);
        }
      }
    }
    return coverage;
  }

  /* Return true when this depth winner must behave as an empty tracing sample for the
   * given receiver. Turning a rejected sample into "empty" (rather than "lit") keeps the
   * hit history untouched, which is the documented meaning of an empty atlas texel. */
  bool shadow_id_sample_filtered(ShadowCoordinates coord, ShadowIdFilter id_filter) const
  {
    [[resource_table]] const Uniform &uni = this->uniforms;
    if (id_filter.mode == ShadowIdMode::Disabled || !uni.uniform_buf.shadow.use_shadow_id) {
      return false;
    }

    uint caster_id = read_caster_id(coord);
    if (caster_id == SHADOW_ID_INVALID) {
      /* Identity unknown (unresolved texel): never filter it away — an unresolved
       * texel must keep occluding so the filter cannot punch light leaks. */
      return false;
    }
    /* IgnoreSelf: drop the receiver's own contributions.
     *
     * Both operands are the BARE resource index, extracted by the helper that matches
     * each pipeline's raw packing: `npr_receiver_identity()` on the surface side
     * (un-shifted index) and `npr_shadow_identity()` on the stored shadow-side value
     * (index<<6 | view, written by the ID resolve pass). Comparing the raw values
     * directly can never match — the shadow pipe packs its view index into the low
     * bits while the surface pipe does not. */
    return caster_id == id_filter.receiver_id;
  }

  float punctual_sample_get(LightData light, float3 P, ShadowIdFilter id_filter) const
  {
    float3 shadow_position = light.local().local.shadow_position;
    float3 lP = transform_point_inversed(light.object_to_world, P);
    lP -= shadow_position;
    int face_id = shadow_punctual_face_index_get(lP);
    lP = shadow_punctual_local_position_to_face_local(face_id, lP);
    ShadowCoordinates coord = shadow_punctual_coordinates(light, lP, face_id);

    /* Caster-identity filter: when the depth winner at this tap is the receiving
     * object itself, it must read as NOT occluding (cast-only semantics). Without
     * this, a receiver on the far side of its own body trivially "sees" its own
     * front surface everywhere — the depth delta is positive for every displaced
     * tap, the disk counts 16/16 blocked at any radius, and the penumbra flattens
     * to a hard edge (back-side patches then ignore Soften completely). */
    if (shadow_id_sample_filtered(coord, id_filter)) {
      return -1.0f;
    }

    float radial_dist = read_depth(coord);
    if (radial_dist == -1.0f) {
      return 1e10f;
    }
    float receiver_dist = length(lP);
    float occluder_dist = radial_dist;
    return receiver_dist - occluder_dist;
  }

  float directional_sample_get(LightData light, float3 P, ShadowIdFilter id_filter) const
  {
    float3 lP = transform_direction_transposed(light.object_to_world, P);
    ShadowCoordinates coord = shadow_directional_coordinates(light, lP);

    /* See punctual_sample_get: identity-filtered self taps read as not-occluding. */
    if (shadow_id_sample_filtered(coord, id_filter)) {
      return -1.0f;
    }

    float depth = read_depth(coord);
    if (depth == -1.0f) {
      return 1e10f;
    }
    /* Use increasing distance from the light. */
    float receiver_dist = -lP.z - orderedIntBitsToFloat(light.clip_near);
    float occluder_dist = depth;
    return receiver_dist - occluder_dist;
  }

  /* Stock entry point (deferred thickness amend, volume): no identity filtering —
   * bit-identical to the unfiltered behaviour. */
  float shadow_sample(const bool is_directional, LightData light, float3 P) const
  {
    return shadow_sample_filtered(is_directional, light, P, shadow_id_filter_disabled());
  }

  /* Identity-filtered occlusion distance, used by the NPR PCSS disk. */
  float shadow_sample_filtered(const bool is_directional,
                               LightData light,
                               float3 P,
                               ShadowIdFilter id_filter) const
  {
    if (is_directional) {
      return directional_sample_get(light, P, id_filter);
    }
    return punctual_sample_get(light, P, id_filter);
  }
};

}  // namespace eevee
