/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/* Outputs the two-tone mask of the half-Lambert factor by asking the active
 * EEVEE surface pipeline (see eevee_surf_*.bsl.hh). The shaping exponent is
 * fixed at the classic half-lambert value (1.0).
 *
 * Soften > 0 feathers the transition with a smoothstep band of width `soften`
 * centered on `threshold`; Soften == 0 keeps the exact hard step. Softening is
 * evaluated here (material layer) so the engine-side signature stays stable.
 *
 * NOTE: cast shadows are intentionally NOT folded into this path (R3 revert):
 * self-shadow fought the two-tone terminator and Soften could not feather the
 * vis discontinuity. See shader-logic doc section 11/12 for the separate
 * shadow channel design. */
[[node]]
void node_half_lambert(float3 N, float threshold, float soften, float sun_id, out float mask)
{
#ifdef GPU_VERTEX_SHADER
  mask = 1.0f;
#else
  float2 r = calc_half_lambert(N, 1.0f, threshold, sun_id);
  if (soften <= 0.0f) {
    mask = r.y; /* exact hard step (bit-identical to the pre-soften behavior) */
  }
  else {
    float lo = threshold - soften * 0.5f;
    float hi = threshold + soften * 0.5f;
    mask = smoothstep(lo, hi, r.x);
  }
#endif
}
