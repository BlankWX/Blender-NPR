/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/**
 * \file
 * \ingroup eevee
 *
 * Shared code between host and client code-bases.
 *
 * Screen-space outline (EdgeOutline) post-process parameters.
 *
 * The post pass only EXTENDS the material-side outline band outward (outer
 * band); the two-tone outline colors live in the EdgeOutline material node.
 */

#pragma once

#include "GPU_shader_shared_utils.hh"

#ifndef GPU_SHADER
namespace blender::eevee {
#endif

struct [[host_shared]] OutlineData {
  /** Extension reach in pixels (> 0 when enabled). Keep equal to the node Width. */
  float width;
  /** Absolute view-space depth delta threshold (scene units). */
  float depth_abs;
  /** Relative view-space depth delta threshold (ratio of center view z). */
  float depth_rel;
  /** Extension strength multiplier. */
  float alpha;
};

#ifndef GPU_SHADER
}  // namespace blender::eevee
#endif
