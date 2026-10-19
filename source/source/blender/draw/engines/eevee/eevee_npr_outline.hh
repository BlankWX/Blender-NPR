/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup eevee
 *
 * Screen-space outline (EdgeOutline) post-process: OUTER-band extender.
 *
 * Detects the far side of each depth silhouette and extends the color painted
 * there by the EdgeOutline MATERIAL node (two-tone aware) outward by Width
 * pixels. Runs as the LAST post-fx stage (before film accumulation).
 *
 * Driven by scene custom properties (set from Python / tests), no DNA:
 * - scene["npr_outline_enable"]  : int/bool, master switch (default off)
 * - scene["npr_outline_width"]   : float, extension reach in pixels (default 1.5)
 *                                   — keep equal to the material node Width
 * - scene["npr_outline_depth_thresh"] : float, absolute view-z delta (default 0.05)
 * - scene["npr_outline_depth_rel"]    : float, relative view-z delta (default 0.02)
 * - scene["npr_outline_alpha"]  : float 0..1 (default 1.0)
 */

#pragma once

#include "eevee_npr_outline_shared.hh"

#include "draw_pass.hh"

namespace blender::eevee {

using namespace draw;

class Instance;

using OutlineDataBuf = draw::UniformBuffer<OutlineData>;

/* -------------------------------------------------------------------- */
/** \name NprOutline
 * \{ */

class NprOutline {

 private:
  class Instance &inst_;

  PassSimple outline_ps_ = {"NprOutline"};
  Framebuffer out_fb_ = {"npr_outline"};

  /** Input/Output texture references. Owned by ShadingView::render_postfx ping-pong. */
  gpu::Texture *input_color_tx_ = nullptr;
  gpu::Texture *output_color_tx_ = nullptr;

  OutlineDataBuf data_;

  bool enabled_ = false;

 public:
  NprOutline(Instance &inst) : inst_(inst) {};
  ~NprOutline() {};

  void init();

  /** Read scene ID properties and (re)build the pass. Called every sync. */
  void sync();

  /**
   * Will swap input and output texture if rendering happens. The actual output of this function
   * is in input_tx.
   */
  void render(View &view, gpu::Texture **input_tx, gpu::Texture **output_tx);

  bool postfx_enabled() const
  {
    return enabled_;
  }

 private:
  void pass_sync();
};

/** \} */

}  // namespace blender::eevee
