/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */


#include "node_shader_util.hh"
#include "DEG_depsgraph_query.hh"
#include "DNA_light_types.h"
#include "DNA_object_types.h"
#include "UI_interface_layout.hh"
#include "UI_resources.hh"

namespace blender {
namespace nodes::node_shader_npr_character_shadow_cc {
static void declare(NodeDeclarationBuilder &b)
{
  b.add_input<decl::Color>("Color"_ustr).default_value({0.8f, 0.8f, 0.8f, 1.0f});
  b.add_input<decl::Color>("Shadow Tint"_ustr)
      .default_value({0.55f, 0.42f, 0.5f, 1.0f})
      .description("Multiplier for the base color in shadow; preserves painted facial details");
  b.add_input<decl::Vector>("Offset"_ustr)
      .default_value({0.0f, 0.0f, 0.0f})
      .description("Translate the projected shadow in world space, in Blender units");
  b.add_input<decl::Float>("Softness"_ustr).default_value(0.3f).min(0.0f).max(10.0f)
      .description("Filter radius in centimetres (1 Blender unit = 1 metre)");
  b.add_input<decl::Float>("Strength"_ustr).default_value(1.0f).min(0.0f).max(1.0f)
      .subtype(PROP_FACTOR);
  b.add_input<decl::Float>("Bias"_ustr).default_value(0.05f).min(0.0f).max(5.0f)
      .description("Receiver depth bias in centimetres; keep small for contact shadows");
  b.add_input<decl::Float>("Threshold"_ustr).default_value(0.5f).min(0.01f).max(0.99f)
      .description("Filtered coverage threshold for the cel silhouette");
  b.add_input<decl::Float>("Edge"_ustr).default_value(0.3f).min(0.001f).max(1.0f)
      .description("Feather width around the coverage threshold");
  b.add_input<decl::Object>("Sun"_ustr).optional_label()
      .description("Directional light with shadows enabled; empty selects the brightest scene Sun");
  b.add_output<decl::Color>("Color"_ustr)
      .description("Base color multiplied by the shadow tint; connect to Base Color or Emission");
  b.add_output<decl::Float>("Shadow"_ustr).description("1 = shadow, 0 = lit; use as a Mix factor");
  b.add_output<decl::Float>("Visibility"_ustr).description("1 = lit, 0 = shadow; multiply by the SDF lit mask");
}

static void draw(ui::Layout &layout, bContext *, PointerRNA *ptr)
{
  layout.prop(ptr, "channel", ui::ITEM_R_SPLIT_EMPTY_NAME, "", ICON_NONE);
}

static int gpu(GPUMaterial *mat, bNode *node, bNodeExecData *, GPUNodeStack *in, GPUNodeStack *out)
{
  GPU_material_flag_set(mat, GPU_MATFLAG_SHADER_TO_RGBA);
  GPU_material_flag_set(mat, GPU_MATFLAG_NPR_OCCLUSION_SHADOW);
  float sun_id = 0.0f;
  Object *ob = node->input_socket(8).default_value_typed<bNodeSocketValueObject>()->value;
  if (ob != nullptr) {
    /* A non-Sun selection must not silently fall back to an unrelated light. */
    sun_id = -1.0f;
    if (ob->type == OB_LAMP && ob->data && id_cast<Light *>(ob->data)->type == LA_SUN) {
      const Object *orig = DEG_get_original(ob);
      if (orig) {
        const uint id = orig->id.session_uid & 0xFFFFFFu;
        sun_id = id ? float(id) : 1.0f;
      }
    }
  }
  const float channel = float(node->custom1);
  return GPU_stack_link(mat, node, "node_npr_character_shadow", in, out,
                        GPU_constant(&sun_id), GPU_constant(&channel));
}
}  // namespace nodes::node_shader_npr_character_shadow_cc

void register_node_type_sh_npr_character_shadow()
{
  namespace file_ns = nodes::node_shader_npr_character_shadow_cc;
  static bke::bNodeType ntype;
  sh_node_type_base(&ntype, "ShaderNodeNPRCharacterShadow"_ustr, SH_NODE_NPR_CHARACTER_SHADOW);
  ntype.ui_name = "Character Shadow";
  ntype.ui_description = "Selective light-space hair/cloth shadows for face and body. EEVEE only";
  ntype.enum_name_legacy = "NPR_CHARACTER_SHADOW";
  ntype.nclass = NODE_CLASS_OP_COLOR;
  ntype.declare = file_ns::declare;
  ntype.draw_buttons = file_ns::draw;
  ntype.gpu_fn = file_ns::gpu;
  ntype.add_ui_poll = object_eevee_shader_nodes_poll;
  ntype.default_width = bke::NodeWidth::_200;
  bke::node_register_type(ntype);
}
}  // namespace blender
