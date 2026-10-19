/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/* NPR EdgeOutline node — screen-space inner outline band with two-tone color.
 *
 * The band is detected by edge_outline_eval() (engine side, eevee_nodetree_lib):
 * a ring test over the scene depth copy at radius = Width pixels. Output Mask is
 * the band factor; Color is Lit/Shadow mixed by the Light Mask input (wire the
 * Half-Lambert Mask here so lit/shadow regions get different outline colors).
 *
 * Typical wiring:
 *   HalfLambert.Mask -> EdgeOutline.Light Mask
 *   EdgeOutline.Mask  -> Mix.Fac, EdgeOutline.Color -> Mix.Color2  (over base color)
 *
 * The outline post pass (scene npr_outline_enable) extends the band OUTWARD by
 * sampling this color, so the full line straddles the silhouette. Requires
 * EEVEE (sets GPU_MATFLAG_RAYCAST for the depth copy).
 */
[[node]]
void node_edge_outline(float4 lit_color,
                       float4 shadow_color,
                       float lit_mask,
                       float width,
                       float threshold,
                       out float mask,
                       out float4 color)
{
#ifdef GPU_VERTEX_SHADER
  mask = 0.0f;
  color = float4(0.0f);
#else
  edge_outline_eval(width, threshold, lit_color, shadow_color, lit_mask, mask, color);
#endif
}
