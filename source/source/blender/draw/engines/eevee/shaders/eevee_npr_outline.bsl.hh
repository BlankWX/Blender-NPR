/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/**
 * Screen-space outline (EdgeOutline) OUTER-band extender.
 *
 * NPR outline band contract:
 * - The INNER band (object side) is drawn by the EdgeOutline MATERIAL node,
 *   which owns the two-tone (lit/shadow) outline colors.
 * - This pass only EXTENDS that color OUTWARD across the far side of each
 *   silhouette (background / far object), up to `width` pixels.
 *
 * Per-pixel rule: find the NEAREST view-z among the 8 ring samples
 * (radius = width px; view-z is negative toward the camera far side, so
 * "nearest" = greatest z). When the ring contains a surface NEARER than the
 * center pixel by more than the threshold, this pixel is on the FAR side of a
 * silhouette: repaint it with the color of that nearest ring sample (the near
 * side already painted its outline in-material). Near-side and same-surface
 * pixels pass through untouched, so the material's two-tone band is never
 * overwritten and grazing-plane false positives become color-neutral repaints.
 *
 * Requires materials with the EdgeOutline node (scene npr_outline_enable to
 * arm this pass); without the node the "extended" color is just the base
 * surface color (halo) — unsupported combination.
 */

#pragma once

#include "draw_view.bsl.hh"
#include "eevee_npr_outline_shared.hh"
#include "gpu_shader_fullscreen_lib.glsl"

namespace eevee::npr {

struct NprOutline {
  [[uniform(0)]] const OutlineData &outline_buf;
  [[sampler(0)]] sampler2DDepth depth_tx;
  [[sampler(1)]] sampler2D in_color_tx;
};

[[vertex]]
void fullscreen_vert([[vertex_id]] const int vert_id, [[position]] float4 &out_position)
{
  fullscreen_vertex(vert_id, out_position);
}

struct NprOutlineFragOut {
  [[frag_color(0)]] float4 color;
};

[[fragment]]
void outline_frag([[resource_table]] const NprOutline &srt,
                  [[resource_table]] const draw::View &views,
                  [[frag_coord]] const float4 frag_co,
                  [[out]] NprOutlineFragOut &frag_out)
{
  int2 texel = int2(frag_co.xy);
  float4 color = texelFetch(srt.in_color_tx, texel, 0);

  const ViewMatrices view = views.get(0);
  const int2 size = textureSize(srt.depth_tx, 0);

  /* reverse_z::read equivalent: buffer stores reversed depth. */
  float center_z = view.depth_screen_to_view(1.0f - texelFetch(srt.depth_tx, texel, 0).r);

  int radius = max(1, int(round(srt.outline_buf.width)));
  const int2 offsets[8] = {int2(1, 0),
                           int2(-1, 0),
                           int2(0, 1),
                           int2(0, -1),
                           int2(1, 1),
                           int2(-1, -1),
                           int2(1, -1),
                           int2(-1, 1)};

  /* Nearest surface in the ring: view-z grows toward the camera. */
  float nearest_z = center_z;
  int2 nearest_texel = texel;
  for (int i = 0; i < 8; i++) {
    int2 s_texel = clamp(texel + offsets[i] * radius, int2(0), size - int2(1));
    float s_z = view.depth_screen_to_view(1.0f - texelFetch(srt.depth_tx, s_texel, 0).r);
    if (s_z > nearest_z) {
      nearest_z = s_z;
      nearest_texel = s_texel;
    }
  }

  float thresh = max(srt.outline_buf.depth_abs, srt.outline_buf.depth_rel * abs(center_z));
  /* Positive delta => a nearer surface (silhouette) in the ring => far side. */
  float delta = nearest_z - center_z;
  float extend = smoothstep(thresh, thresh * 1.5f, delta) * srt.outline_buf.alpha;

  float4 nearest_color = texelFetch(srt.in_color_tx, nearest_texel, 0);
  frag_out.color = float4(mix(color.rgb, nearest_color.rgb, clamp(extend, 0.0f, 1.0f)), color.a);
}

}  // namespace eevee::npr

PipelineGraphic eevee_npr_outline(eevee::npr::fullscreen_vert, eevee::npr::outline_frag);
