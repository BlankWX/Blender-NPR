/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */


#include "node_shader_util.hh"
#include "UI_interface_layout.hh"
#include "UI_resources.hh"

namespace blender {
namespace nodes::node_shader_npr_shadow_caster_cc {
static void declare(NodeDeclarationBuilder &) {}
static void draw(ui::Layout &layout, bContext *, PointerRNA *ptr)
{
  layout.prop(ptr, "channel", ui::ITEM_R_SPLIT_EMPTY_NAME, "", ICON_NONE);
}
}  // namespace nodes::node_shader_npr_shadow_caster_cc
void register_node_type_sh_npr_shadow_caster()
{
  namespace file_ns = nodes::node_shader_npr_shadow_caster_cc;
  static bke::bNodeType ntype;
  sh_node_type_base(&ntype, "ShaderNodeNPRShadowCaster"_ustr, SH_NODE_NPR_SHADOW_CASTER);
  ntype.ui_name = "Shadow Caster";
  ntype.ui_description = "Place in the hair/skirt material to mark its shadow channel; no wires needed. "
                        "Mute to disable. Material alpha is clipped at 0.5 for the NPR shadow. EEVEE only";
  ntype.enum_name_legacy = "NPR_SHADOW_CASTER";
  ntype.nclass = NODE_CLASS_INPUT;
  ntype.declare = file_ns::declare;
  ntype.draw_buttons = file_ns::draw;
  ntype.add_ui_poll = object_eevee_shader_nodes_poll;
  ntype.default_width = bke::NodeWidth::_180;
  bke::node_register_type(ntype);
}
}  // namespace blender
