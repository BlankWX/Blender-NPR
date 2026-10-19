/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "node_shader_util.hh"

namespace blender {

namespace nodes::node_shader_edge_rim_cc {

/* Input socket indices (order of declaration in node_declare). */
enum eInSocket : int {
  SOCK_IN_LIT_COLOR = 0,
  SOCK_IN_SHADOW_COLOR = 1,
  SOCK_IN_LIGHT_MASK = 2,
  SOCK_IN_OUTLINE_WIDTH = 3,
  SOCK_IN_RIM_WIDTH = 4,
  SOCK_IN_THRESHOLD = 5,
};

static void node_declare(NodeDeclarationBuilder &b)
{
  b.add_input<decl::Color>("Lit Color"_ustr).default_value({1.0f, 1.0f, 1.0f, 1.0f});
  b.add_input<decl::Color>("Shadow Color"_ustr).default_value({0.0f, 0.0f, 0.0f, 1.0f});
  b.add_input<decl::Float>("Light Mask"_ustr)
      .default_value(1.0f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("Tone factor selecting the rim color: wire the same Half-Lambert/"
                   "shadow mask chain the outline node uses. 0 = Shadow Color, "
                   "1 = Lit Color");
  b.add_input<decl::Float>("Outline Width"_ustr)
      .default_value(1.0f)
      .min(0.0f)
      .max(32.0f)
      .subtype(PROP_PIXEL)
      .description("Outline width in output pixels; the rim starts one pixel further inward. "
                   "Keep equal to the Edge Outline node Width so the bands tile "
                   "without overlap");
  b.add_input<decl::Float>("Rim Width"_ustr)
      .default_value(2.0f)
      .min(0.0f)
      .max(32.0f)
      .subtype(PROP_PIXEL)
      .description("Rim band width in output pixels after the one-pixel gap; 0 disables the rim");
  b.add_input<decl::Float>("Threshold"_ustr)
      .default_value(0.05f)
      .min(0.0f)
      .max(10.0f)
      .description("One-pixel depth discontinuity after slope rejection (scene units). "
                   "Keep equal to the outline node Threshold");
  b.add_output<decl::Float>("Mask"_ustr)
      .description("Rim band factor 0..1 — the pixel interval [Outline Width + 1px, "
                   "+ Rim Width) from the silhouette, disjoint from the outline band. "
                   "Use as Mix factor over the base color");
  b.add_output<decl::Color>("Color"_ustr)
      .description("Rim color = mix(Shadow Color, Lit Color, Light Mask). "
                   "Combine with Mask via a Mix node, chained BEFORE the outline mix");
}

static int node_shader_gpu_edge_rim(GPUMaterial *mat,
                                    bNode *node,
                                    bNodeExecData * /*execdata*/,
                                    GPUNodeStack *in,
                                    GPUNodeStack *out)
{
  /* Arms the raycast depth-copy chain (same as Edge Outline / stock Raycast). */
  GPU_material_flag_set(mat, GPU_MATFLAG_RAYCAST);

  return GPU_stack_link(mat, node, "node_edge_rim", in, out);
}

}  // namespace nodes::node_shader_edge_rim_cc

/* node type definition */
void register_node_type_sh_edge_rim()
{
  namespace file_ns = nodes::node_shader_edge_rim_cc;

  static bke::bNodeType ntype;

  sh_node_type_base(&ntype, "ShaderNodeEdgeRim"_ustr, SH_NODE_EDGE_RIM);
  ntype.ui_name = "Edge Rim (NPR)";
  ntype.ui_description =
      "Screen-space rim light band inside the silhouette, one pixel INWARD after the "
      "outline band (pixel-disjoint: they never fight for the same pixels). Colors "
      "follow the two-tone mask like the outline node. Only supported in EEVEE";
  ntype.enum_name_legacy = "EDGE_RIM";
  ntype.nclass = NODE_CLASS_INPUT;
  ntype.declare = file_ns::node_declare;
  ntype.add_ui_poll = object_eevee_shader_nodes_poll;
  ntype.default_width = bke::NodeWidth::_180;
  ntype.gpu_fn = file_ns::node_shader_gpu_edge_rim;

  bke::node_register_type(ntype);
}

}  // namespace blender
