/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/* NPR Eye See-Through (眼透) — in-shader screen-space stencil.
 *
 * No AOV and no compositor. The hair material stays opaque and mixes an
 * already-shaded eye color over itself:
 *
 *   color = previous opaque layer color (the eye, already lit)
 *   mask  = 1 where the opaque prepass object ID is an eye object
 *           and the camera is in front (front gate)
 *   hair_surface = mix(hair_bsdf, emission(color), mask * strength)
 *
 * The eye color comes from the previous layer that EEVEE already binds for
 * Shader-to-RGBA transparent materials. The object ID comes from the opaque
 * prepass (eye materials are tagged as eye sources). The hair itself never
 * changes its alpha.
 */

[[node]]
void node_eye_see_through_overlay(float strength,
                                  float transparency,
                                  float angle,
                                  out float4 color,
                                  out float mask)
{
#ifdef GPU_VERTEX_SHADER
  color = float4(0.0f);
  mask = 0.0f;
#else
  float3 rgb;
  calc_eye_see_through_overlay(strength, transparency, angle, rgb, mask);
  color = float4(rgb, 1.0f);
#endif
}
