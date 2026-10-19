/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup eevee
 */

#include "BKE_idprop.hh"

#include "GPU_debug.hh"

#include "eevee_instance.hh"
#include "eevee_npr_outline.hh"

namespace blender::eevee {

/* -------------------------------------------------------------------- */
/** \name Scene ID property helpers
 *
 * Scene custom properties drive this effect to avoid DNA/RNA churn for v1.
 * All reads are tolerant: missing/mistyped properties fall back to defaults.
 * \{ */

static IDProperty *npr_idp_get(IDProperty *props, const char *name)
{
  return props ? IDP_GetPropertyFromGroup_null(props, name) : nullptr;
}

static bool npr_idp_bool(IDProperty *props, const char *name, const bool def)
{
  IDProperty *prop = npr_idp_get(props, name);
  if (prop == nullptr) {
    return def;
  }
  if (prop->type == IDP_INT || prop->type == IDP_BOOLEAN) {
    return IDP_int_or_bool_get(prop) != 0;
  }
  return def;
}

static float npr_idp_float(IDProperty *props, const char *name, const float def)
{
  IDProperty *prop = npr_idp_get(props, name);
  if (prop == nullptr) {
    return def;
  }
  if (prop->type == IDP_FLOAT) {
    return IDP_float_get(prop);
  }
  if (prop->type == IDP_DOUBLE) {
    return float(IDP_double_get(prop));
  }
  if (prop->type == IDP_INT) {
    return float(IDP_int_get(prop));
  }
  return def;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name NprOutline
 * \{ */

void NprOutline::init()
{
  enabled_ = false;
}

void NprOutline::sync()
{
  IDProperty *props = IDP_GetProperties(&inst_.scene->id);

  /* Stock behavior unless the scene opts in. */
  enabled_ = npr_idp_bool(props, "npr_outline_enable", false);
  if (!enabled_) {
    return;
  }

  data_.width = fmaxf(npr_idp_float(props, "npr_outline_width", 1.5f), 0.5f);
  data_.depth_abs = fmaxf(npr_idp_float(props, "npr_outline_depth_thresh", 0.05f), 1e-5f);
  data_.depth_rel = fmaxf(npr_idp_float(props, "npr_outline_depth_rel", 0.02f), 0.0f);
  data_.alpha = fminf(fmaxf(npr_idp_float(props, "npr_outline_alpha", 1.0f), 0.0f), 1.0f);
  data_.push_update();

  pass_sync();
}

void NprOutline::pass_sync()
{
  outline_ps_.init();
  outline_ps_.state_set(DRW_STATE_WRITE_COLOR);
  outline_ps_.shader_set(inst_.shaders.static_shader_get(NPR_OUTLINE));
  outline_ps_.bind_ubo("outline_buf", data_);
  outline_ps_.bind_texture("depth_tx", &inst_.render_buffers.depth_tx);
  /* Deferred: member pointer is assigned in render() before submit. */
  outline_ps_.bind_texture("in_color_tx", &input_color_tx_);
  outline_ps_.draw_procedural(GPU_PRIM_TRIS, 1, 3);
}

void NprOutline::render(View &view, gpu::Texture **input_tx, gpu::Texture **output_tx)
{
  if (!enabled_) {
    return;
  }

  input_color_tx_ = *input_tx;
  output_color_tx_ = *output_tx;

  out_fb_.ensure(GPU_ATTACHMENT_NONE, GPU_ATTACHMENT_TEXTURE(*output_tx));

  GPU_debug_group_begin("Npr Outline");
  GPU_framebuffer_bind(out_fb_);
  inst_.manager->submit(outline_ps_, view);
  GPU_debug_group_end();

  /* Swap buffers so that next effect has the right input. */
  *input_tx = output_color_tx_;
  *output_tx = input_color_tx_;
}

/** \} */

}  // namespace blender::eevee
