/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/* NPR Eye See-Through Source (眼透源).
 *
 * A pass-through shader node. It marks the material it lives in as an eye
 * see-through source (GPU_MATFLAG_NPR_EYE_SOURCE set in the node's gpu_fn).
 * The EEVEE forward pipeline renders these materials in the dedicated source
 * stage; the Eye See-Through hair node samples that stage. No AOV/compositor. */

[[node]]
void node_eye_see_through_source(Closure shader_in, Closure &shader_out)
{
  shader_out = shader_in;
}
