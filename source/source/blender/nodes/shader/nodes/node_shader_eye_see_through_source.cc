/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "node_shader_util.hh"

namespace blender {

namespace nodes::node_shader_eye_see_through_source_cc {

static void node_declare(NodeDeclarationBuilder & /*b*/)
{
  /* Marker node: no sockets. Its presence in the material node tree is what the
   * EEVEE material sync scans for. Keeping it socket-less also keeps it out of
   * the shader weight tree (a pass-through shader node there is unsupported). */
}

}  // namespace nodes::node_shader_eye_see_through_source_cc

/* node type definition */
void register_node_type_sh_eye_see_through_source()
{
  namespace file_ns = nodes::node_shader_eye_see_through_source_cc;

  static bke::bNodeType ntype;

  sh_node_type_base(
      &ntype, "ShaderNodeEyeSeeThroughSource"_ustr, SH_NODE_EYE_SEE_THROUGH_SOURCE);
  ntype.ui_name = "Eye See-Through Source";
  ntype.ui_description =
      "Marks the material it is placed in as an eye see-through source. Put this "
      "node anywhere in the eye/lash/brow material you want visible through the "
      "hair; it has no sockets and does not change the shading. The Eye "
      "See-Through node in the hair reads only materials containing this marker. "
      "The material must be rendered as Blended. EEVEE only";
  ntype.enum_name_legacy = "EYE_SEE_THROUGH_SOURCE";
  ntype.nclass = NODE_CLASS_INPUT;
  ntype.declare = file_ns::node_declare;
  ntype.add_ui_poll = object_eevee_shader_nodes_poll;
  ntype.default_width = bke::NodeWidth::_180;

  bke::node_register_type(ntype);
}

}  // namespace blender
