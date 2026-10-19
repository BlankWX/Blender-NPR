/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "node_shader_util.hh"

namespace blender {

namespace nodes::node_shader_eye_see_through_cc {

static void node_declare(NodeDeclarationBuilder &b)
{
  b.add_input<decl::Float>("Strength"_ustr)
      .default_value(0.5f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("How much of the already-shaded eye color is mixed over the "
                   "hair where the eye source sits behind it");
  b.add_input<decl::Float>("Transparency"_ustr)
      .default_value(0.35f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("How much the hair shows through again at larger view angles. "
                   "0 = eye fully replaces the hair at all angles; 1 = hair fully "
                   "returns after Angle. The front stays fully clear either way");
  b.add_input<decl::Float>("Angle"_ustr)
      .default_value(0.5235988f)
      .min(0.0f)
      .max(1.5707963f)
      .subtype(PROP_ANGLE)
      .description("View angle (0-90 degrees) where the transparency fade starts, "
                   "measured against the hair object's local -Y forward. 0 = straight "
                   "on. The fade reaches full Transparency over the next 20 degrees; "
                   "the hard front gate still hides the eye at profile/back views");
  b.add_output<decl::Color>("Color"_ustr).description(
      "Already-shaded eye color from the EyeSource stage. Wire into an Emission "
      "node and mix it over the hair with Mask");
  b.add_output<decl::Float>("Mask"_ustr).description(
      "Eye-source coverage and view gate. Use as the Mix Shader factor");
}

static int node_shader_gpu_eye_see_through(GPUMaterial *mat,
                                           bNode *node,
                                           bNodeExecData * /*execdata*/,
                                           GPUNodeStack *in,
                                           GPUNodeStack *out)
{
  /* The overlay lives entirely in the hair shader:
   * - TRANSPARENT + SHADER_TO_RGBA bind the previous resolved eye color.
   * - NPR_EYE routes the material to the second (hair) transparent stage and
   *   adds the eye-stage color/alpha sampler resources. */
  GPU_material_flag_set(mat, GPU_MATFLAG_SHADER_TO_RGBA);
  GPU_material_flag_set(mat, GPU_MATFLAG_TRANSPARENT);
  GPU_material_flag_set(mat, GPU_MATFLAG_NPR_EYE);
  return GPU_stack_link(mat, node, "node_eye_see_through_overlay", in, out);
}

}  // namespace nodes::node_shader_eye_see_through_cc

/* node type definition */
void register_node_type_sh_eye_see_through()
{
  namespace file_ns = nodes::node_shader_eye_see_through_cc;

  static bke::bNodeType ntype;

  sh_node_type_base(&ntype, "ShaderNodeEyeSeeThrough"_ustr, SH_NODE_EYE_SEE_THROUGH);
  ntype.ui_name = "Eye See-Through";
  ntype.ui_description =
      "In-shader screen-space eye see-through for anime NPR. Samples the "
      "already-shaded color of materials marked with Eye See-Through Source, "
      "then mixes it over the hair. The hair stays opaque; no AOV or compositor. "
      "Only supported in EEVEE";
  ntype.enum_name_legacy = "EYE_SEE_THROUGH";
  ntype.nclass = NODE_CLASS_INPUT;
  ntype.declare = file_ns::node_declare;
  ntype.add_ui_poll = object_eevee_shader_nodes_poll;
  ntype.default_width = bke::NodeWidth::_180;
  ntype.gpu_fn = file_ns::node_shader_gpu_eye_see_through;

  bke::node_register_type(ntype);
}

}  // namespace blender
