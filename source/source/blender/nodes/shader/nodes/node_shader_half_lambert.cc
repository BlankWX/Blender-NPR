/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "node_shader_util.hh"

#include "DEG_depsgraph_query.hh"
#include "DNA_light_types.h"
#include "DNA_object_types.h"

namespace blender {

namespace nodes::node_shader_half_lambert_cc {

/* Input socket indices (order of declaration in node_declare). */
enum eInSocket : int {
  SOCK_IN_NORMAL = 0,
  SOCK_IN_THRESHOLD = 1,
  SOCK_IN_SOFTEN = 2,
  SOCK_IN_SUN = 3,
};

static void node_declare(NodeDeclarationBuilder &b)
{
  b.add_input<decl::Vector>("Normal"_ustr).hide_value();
  b.add_input<decl::Float>("Threshold"_ustr)
      .default_value(0.5f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("Two-tone split point of the half-Lambert factor");
  b.add_input<decl::Float>("Soften"_ustr)
      .default_value(0.1f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("Feather width centered on the threshold. 0 = hard edge");
  b.add_input<decl::Object>("Sun"_ustr)
      .optional_label()
      .description("Only use this sun light for the two-tone mask. "
                   "Leave empty to use all visible lights");
  b.add_output<decl::Float>("Mask"_ustr)
      .description("Two-tone mask: hard step by default, feathered when Soften > 0. "
                   "Independent per light (multi-light safe)");
}

/* 24-bit session UID of the selected sun, stored as a float so it can be passed
 * to the shader as a node constant. 0 means "no selection" (use all lights).
 * 24 bits are exact in float32 and make collisions between scene lights
 * practically impossible. */
static float npr_selected_sun_id(bNode &node)
{
  if (node.input_sockets().size() <= SOCK_IN_SUN) {
    return 0.0f;
  }
  bNodeSocket &socket = node.input_socket(SOCK_IN_SUN);
  Object *ob = socket.default_value_typed<bNodeSocketValueObject>()->value;
  if (ob == nullptr || ob->type != OB_LAMP || ob->data == nullptr) {
    return 0.0f;
  }
  const Light *light = id_cast<Light *>(ob->data);
  if (light->type != LA_SUN) {
    return 0.0f;
  }
  const Object *orig = DEG_get_original(ob);
  const uint id = orig->id.session_uid & 0xFFFFFFu;
  return (id != 0) ? float(id) : 1.0f;
}

static int node_shader_gpu_half_lambert(GPUMaterial *mat,
                                        bNode *node,
                                        bNodeExecData * /*execdata*/,
                                        GPUNodeStack *in,
                                        GPUNodeStack *out)
{
  if (!in[SOCK_IN_NORMAL].link) {
    GPU_link(mat, "world_normals_get", &in[SOCK_IN_NORMAL].link);
  }

  /* Force the hybrid surface pipeline so the light resources needed by
   * calc_half_lambert() are bound when this material is shaded. Same mechanism
   * used by the Shader to RGB node. */
  GPU_material_flag_set(mat, GPU_MATFLAG_SHADER_TO_RGBA);

  /* The Sun object socket is an ID socket (GPU_NONE), so it is not part of the
   * GPU stack inputs. Resolve it to a stable 24-bit light ID and pass it as an
   * extra node constant. `custom3` lives in the node, so the pointer stays valid
   * until shader code generation. */
  node->custom3 = npr_selected_sun_id(*node);
  return GPU_stack_link(mat, node, "node_half_lambert", in, out, GPU_constant(&node->custom3));
}

}  // namespace nodes::node_shader_half_lambert_cc

/* node type definition */
void register_node_type_sh_half_lambert()
{
  namespace file_ns = nodes::node_shader_half_lambert_cc;

  static bke::bNodeType ntype;

  sh_node_type_base(&ntype, "ShaderNodeHalfLambert"_ustr, SH_NODE_HALF_LAMBERT);
  ntype.ui_name = "Half-Lambert Two-Tone";
  ntype.ui_description =
      "Half-Lambert wrap lighting split into two tones for NPR shading. "
      "Outputs a hard two-tone mask; each light contributes independently. "
      "Optional Sun input restricts the mask to one scene sun. "
      "Only supported in EEVEE";
  ntype.enum_name_legacy = "HALF_LAMBERT";
  ntype.nclass = NODE_CLASS_INPUT;
  ntype.declare = file_ns::node_declare;
  ntype.add_ui_poll = object_eevee_shader_nodes_poll;
  ntype.default_width = bke::NodeWidth::_160;
  ntype.gpu_fn = file_ns::node_shader_gpu_half_lambert;

  bke::node_register_type(ntype);
}

}  // namespace blender
