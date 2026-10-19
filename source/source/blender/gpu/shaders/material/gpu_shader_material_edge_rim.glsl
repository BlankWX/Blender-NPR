/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/* NPR EdgeRim node — screen-space rim light band inside the silhouette.
 *
 * Band construction (pixel-disjoint from the outline band):
 *   cross(r) = depth ring at radius r crosses a silhouette
 *   outline band : d < outline_width            (Edge Outline node)
 *   rim band     : outline_width+1 <= d < outline_width+1+rim_width
 *                = cross(d2) AND NOT cross(d1),  d1=outline_width, d2=d1+rim_width+1
 * so outlineMask * rimMask == 0 by construction — the two effects never fight
 * for the same pixels.
 *
 * Color = mix(Shadow, Lit, Light Mask) — wire the same two-tone mask chain the
 * outline node uses.
 */
[[node]]
void node_edge_rim(float4 lit_color,
                   float4 shadow_color,
                   float lit_mask,
                   float outline_width,
                   float rim_width,
                   float threshold,
                   out float mask,
                   out float4 color)
{
#ifdef GPU_VERTEX_SHADER
  mask = 0.0f;
  color = float4(0.0f);
#else
  edge_rim_eval(
      outline_width, rim_width, threshold, lit_color, shadow_color, lit_mask, mask, color);
#endif
}
