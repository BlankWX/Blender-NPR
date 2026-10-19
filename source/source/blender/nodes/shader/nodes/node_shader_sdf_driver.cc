/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "node_shader_util.hh"
#include "node_util.hh"

#include "BKE_image.hh"
#include "BKE_node_legacy_types.hh"
#include "BKE_node_runtime.hh"
#include "BKE_texture.h"

#include "DEG_depsgraph_query.hh"
#include "DNA_light_types.h"
#include "DNA_object_types.h"

namespace blender {

namespace nodes::node_shader_sdf_driver_cc {

/* Input socket indices (order of declaration in node_declare). */
enum eInSocket : int {
  SOCK_IN_SDF = 0,
  SOCK_IN_REF_ANGLE = 1,
  SOCK_IN_GAIN = 2,
  SOCK_IN_BIAS = 3,
  SOCK_IN_SOFTEN = 4,
  SOCK_IN_SUN = 5,
};

static void node_declare(NodeDeclarationBuilder &b)
{
  b.add_input<decl::Color>("SDF"_ustr).description(
      "Link the (externally merged) SDF Image Texture's Color. The node "
      "samples that image itself at the sun-mirrored UV — no downstream UV "
      "chain needed. Use Non-Color data; the internal sampler is forced to "
      "EXTEND so the field never wraps");
  b.add_input<decl::Float>("Ref Angle"_ustr)
      .default_value(1.5708f) /* 90 degrees in radians. */
      .subtype(PROP_ANGLE)
      .description("Light azimuth where Threshold crosses 0.5 (plus Bias)");
  b.add_input<decl::Float>("Gain"_ustr)
      .default_value(0.31f)
      .min(0.0f)
      .max(3.0f)
      .description("Threshold change per radian of light azimuth |theta|");
  b.add_input<decl::Float>("Bias"_ustr)
      .default_value(0.0f)
      .min(-1.0f)
      .max(1.0f)
      .description("Manual threshold offset (same feel as the Greater Than node's threshold)");
  b.add_input<decl::Float>("Soften"_ustr)
      .default_value(0.1f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description(
          "Feather width centered on the threshold, same feel as the "
          "Half-Lambert node. 0 = hard edge");
  b.add_input<decl::Object>("Sun"_ustr)
      .optional_label()
      .description("Only use this sun light to drive the SDF direction. "
                   "Leave empty to use all visible lights");
  b.add_output<decl::Float>("Mask"_ustr).description(
      "Two-tone mask with a value-domain feather (Half-Lambert-style "
      "Soften): 1 = lit, 0 = shadow (back-lit pixels forced to shadow; "
      "no light = full shadow)");
}

/* 24-bit session UID of the selected sun, stored as a float so it can be passed
 * to the shader as a node constant. 0 means "no selection" (use all lights). */
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

static int node_shader_gpu_sdf_driver(GPUMaterial *mat,
                                      bNode *node,
                                      bNodeExecData * /*execdata*/,
                                      GPUNodeStack *in,
                                      GPUNodeStack *out)
{
  /* Force the hybrid surface pipeline so the light resources needed by
   * calc_sdf_light_dir() are bound when this material is shaded. Same mechanism
   * used by the Shader to RGB and Half-Lambert nodes. */
  GPU_material_flag_set(mat, GPU_MATFLAG_SHADER_TO_RGBA);

  /* Upstream image reconnaissance: borrow the Image Texture's image (found by
   * walking the SDF Color link, skipping reroutes) so the shader can sample it
   * at the sun-mirrored UV. The incoming Color value itself is only a locator.
   * 5.2 stores the node type in type_legacy. */
  Image *ima = nullptr;
  ImageUser *iuser = nullptr;
  bNodeLink *lnk = node->input_socket(SOCK_IN_SDF).link;
  bNode *from = lnk ? lnk->fromnode : nullptr;
  while (from && from->is_reroute()) {
    bNodeLink *up = from->input_socket(0).link;
    from = up ? up->fromnode : nullptr;
  }
  if (from && from->type_legacy == SH_NODE_TEX_IMAGE && from->id && from->storage) {
    /* We get the image user from the original node, since GPU image keeps
     * a pointer to it and the dependency refreshes the original. */
    bNode *orig = from->runtime->original ? from->runtime->original : from;
    ima = id_cast<Image *>(from->id);
    iuser = &static_cast<NodeTexImage *>(orig->storage)->iuser;
  }

  /* Resolve the optional Sun object socket to a 24-bit light ID. The socket is
   * an ID socket (GPU_NONE), so it is not part of the stack inputs. `custom3`
   * lives in the node, keeping the constant pointer valid for codegen. */
  node->custom3 = npr_selected_sun_id(*node);

  if (!ima || !iuser) {
    /* No upstream image (value-only chain): degraded path that compares the
     * incoming value with hemisphere inversion instead of UV mirroring. */
    return GPU_stack_link(
        mat, node, "node_sdf_driver_fallback", in, out, GPU_constant(&node->custom3));
  }

  /* Field textures must never wrap: force EXTEND. Filtering is linear only —
   * no mip / aniso: the shader samples at LOD 0 so the 8-bit field never
   * quantizes into visible texel-block stripes in the feather band. */
  GPUSamplerState sampler_state = GPUSamplerState::default_sampler();
  sampler_state.extend_x = GPU_SAMPLER_EXTEND_MODE_EXTEND;
  sampler_state.extend_yz = GPU_SAMPLER_EXTEND_MODE_EXTEND;
  sampler_state.filtering = GPU_SAMPLER_FILTERING_LINEAR;

  /* Auto UV: active render UV, same default as an unlinked Image Texture. */
  GPUNodeLink *texco = GPU_attribute(mat, CD_AUTO_FROM_NAME, "");
  node_shader_gpu_bump_tex_coord(mat, node, &texco);

  GPUNodeLink *gpu_image = GPU_image(mat, ima, iuser, sampler_state);
  /* Arg order: node inputs, then extras (sun_id, co, ima), then outputs. */
  return GPU_stack_link(
      mat, node, "node_sdf_driver", in, out, GPU_constant(&node->custom3), texco, gpu_image);
}

}  // namespace nodes::node_shader_sdf_driver_cc

/* node type definition */
void register_node_type_sh_sdf_driver()
{
  namespace file_ns = nodes::node_shader_sdf_driver_cc;

  static bke::bNodeType ntype;

  sh_node_type_base(&ntype, "ShaderNodeSDFDriver"_ustr, SH_NODE_SDF_DRIVER);
  ntype.ui_name = "SDF Driver";
  ntype.ui_description =
      "Drives an SDF terminator field from the sun position (mihoyo-style "
      "face shading): samples the linked SDF Image Texture at the "
      "sun-mirrored UV and outputs a feathered two-tone Mask (Soften "
      "controls the edge). Optional Sun input drives the SDF from one scene sun. "
      "Only supported in EEVEE";
  ntype.enum_name_legacy = "SDF_DRIVER";
  ntype.nclass = NODE_CLASS_INPUT;
  ntype.declare = file_ns::node_declare;
  ntype.add_ui_poll = object_eevee_shader_nodes_poll;
  ntype.default_width = bke::NodeWidth::_160;
  ntype.gpu_fn = file_ns::node_shader_gpu_sdf_driver;

  bke::node_register_type(ntype);
}

}  // namespace blender
