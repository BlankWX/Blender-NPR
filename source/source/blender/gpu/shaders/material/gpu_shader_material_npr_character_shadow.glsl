/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */


[[node]]
void node_npr_character_shadow(float4 base_color, float4 shadow_tint, float3 offset,
                               float softness, float strength, float bias,
                               float threshold, float edge, float sun_id, float channel,
                               out float4 color, out float shadow, out float visibility)
{
#ifdef GPU_VERTEX_SHADER
  visibility = 1.0;
#else
  visibility = calc_occlusion_shadow(sun_id, offset, softness, strength, bias,
                                    channel, threshold, edge);
#endif
  shadow = 1.0 - visibility;
  color = float4(base_color.rgb * mix(float3(1.0), shadow_tint.rgb, shadow), base_color.a);
}
