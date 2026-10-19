/* SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

/**
 * Forward lighting evaluation: Lighting is evaluated during the geometry rasterization.
 *
 * This is used by alpha blended materials and materials using Shader to RGB nodes.
 */

#include "infos/eevee_geom_infos.hh"

#include "draw_model.bsl.hh"
#include "eevee_colorspace_lib.bsl.hh"
#include "eevee_light_eval.bsl.hh"
#include "eevee_lightprobe.bsl.hh"
#include "eevee_lightprobe_plane.bsl.hh"
#include "eevee_nodetree_closures_lib.glsl"
#include "eevee_reverse_z_lib.bsl.hh"
#include "eevee_subsurface_lib.bsl.hh"
#include "gpu_shader_codegen_lib.glsl"

#ifdef GLSL_CPP_STUBS
#  define MAT_REFLECTION
#endif

/* Allow static compilation of forward materials. */
#ifndef CLOSURE_BIN_COUNT
#  define CLOSURE_BIN_COUNT SRT_CONSTANT_light_closure_eval_count
#endif

#ifndef GLSL_CPP_STUBS
#  if CLOSURE_BIN_COUNT != SRT_CONSTANT_light_closure_eval_count && \
      SRT_CONSTANT_light_closure_eval_count != 0
#    error Closure data count and eval count must match
#  endif
#endif

namespace eevee {

/* NPR: Half-Lambert two-tone evaluation (Goo-style direct light loop).
 * Accumulates the wrap-remapped N.L of every view-visible light, weighted by
 * light energy, then splits at `threshold`. Shared by the forward and hybrid
 * surface pipelines (the only two that bind light resources in the material
 * fragment stage). */
struct HalfLambertEval {
  float3 N;
  float shaping_power;
  float best;
  /* 0 = use all visible lights. Otherwise only this sun's 24-bit object ID. */
  float selected_sun_id;

  void accumulate(LightData light, float3 L)
  {
    float weight = max(light.color.x, max(light.color.y, light.color.z)) * light.power[LIGHT_DIFFUSE];
    if (weight <= 0.0f) {
      return;
    }
    float nl = dot(N, L);
    float hl = pow(saturate(nl * 0.5f + 0.5f), shaping_power);
    /* Strongest light wins: keeps each light's terminator independent so
     * moving one light does not shift another light's mask boundary. */
    best = max(best, hl);
  }

  void eval_directional([[resource_table]] LightEvalData & /*res*/,
                        uint /*l_idx*/,
                        LightData light)
  {
    /* Skip the sun extracted from the HDRI world: world strength must not drive NPR shading. */
    if (light.sun().is_world_sun > 0.5f) {
      return;
    }
    /* Optional explicit sun selection (Object socket). 0 = all scene lights. */
    if (selected_sun_id > 0.5f && light.sun().object_id != selected_sun_id) {
      return;
    }
    /* Sun `direction` is the stable shading direction (surface toward light). */
    accumulate(light, light.sun().direction);
  }

  void eval_local([[resource_table]] LightEvalData & /*res*/, uint /*l_idx*/, LightData light)
  {
    /* Explicit sun selection: ignore point/spot/area lights. */
    if (selected_sun_id > 0.5f) {
      return;
    }
    float3 to_light = light.position() - g_data.P;
    accumulate(light, to_light * inversesqrt(max(dot(to_light, to_light), 1e-12f)));
  }
};

template void light::foreach_visible<HalfLambertEval, LightEvalData>(
    const LightRenderData &, float2, float, HalfLambertEval &, LightEvalData &);
/* NPR: SDF terminator light lookup (mihoyo-style face SDF): picks the strongest
 * visible light and reports the world-space direction toward it; the material
 * layer reduces that to an object-space azimuth that drives the SDF Driver
 * node's Threshold / Flip / Gate signals. */
struct SDFDriverEval {
  float best_weight;
  float3 best_dir;
  /* 0 = use all visible lights. Otherwise only this sun's 24-bit object ID. */
  float selected_sun_id;

  void accumulate(LightData light, float3 L)
  {
    float weight = max(light.color.x, max(light.color.y, light.color.z)) *
                   light.power[LIGHT_DIFFUSE];
    if (weight <= best_weight) {
      return;
    }
    best_weight = weight;
    best_dir = L;
  }

  void eval_directional([[resource_table]] LightEvalData & /*res*/,
                        uint /*l_idx*/,
                        LightData light)
  {
    /* Skip the sun extracted from the HDRI world: world strength must not drive NPR shading. */
    if (light.sun().is_world_sun > 0.5f) {
      return;
    }
    /* Optional explicit sun selection (Object socket). 0 = all scene lights. */
    if (selected_sun_id > 0.5f && light.sun().object_id != selected_sun_id) {
      return;
    }
    /* Sun `direction` is the shading direction (surface toward light). */
    accumulate(light, light.sun().direction);
  }

  void eval_local([[resource_table]] LightEvalData & /*res*/, uint /*l_idx*/, LightData light)
  {
    /* Explicit sun selection: ignore point/spot/area lights. */
    if (selected_sun_id > 0.5f) {
      return;
    }
    /* Point/spot lights also drive the lookup when no sun is present. */
    float3 to_light = light.position() - g_data.P;
    accumulate(light, to_light * inversesqrt(max(dot(to_light, to_light), 1e-12f)));
  }
};

template void light::foreach_visible<SDFDriverEval, LightEvalData>(
    const LightRenderData &, float2, float, SDFDriverEval &, LightEvalData &);

float3 calc_sdf_light_dir_impl(float sun_id)
{
  [[resource_table]] LightEvalIterator &lights = resource_table_get(eevee::LightEvalIterator);
  [[resource_table]] const draw::View &views = resource_table_get(draw::View);

#ifdef GLSL_CPP_STUBS
  const float2 pixel = float2(0.0f);
#else
  const float2 pixel = gl_FragCoord.xy;
#endif

  SDFDriverEval cb;
  cb.best_weight = 0.0f;
  cb.best_dir = float3(0.0f);
  cb.selected_sun_id = sun_id;

  const ViewMatrices view = views.get(0);
  float vPz = dot(view.forward(), g_data.P) - dot(view.forward(), view.position());
  [[resource_table]] LightEvalData &eres = lights.inner;
  light::foreach_visible(lights.light_data, pixel, vPz, cb, eres);

  return cb.best_dir;
}

/* NPR selective character shadows.
 * Marked materials write separate hair/cloth nearest-depth layers during the
 * ordinary shadow rasterization. Neither layer depends on the regular atlas
 * winner. Receivers use world position and the selected Sun, then a fixed
 * 32-tap PCF disk and coverage remap. EEVEE still selects virtual page LOD from
 * the view; the geometric projection is light-space, not a screen-space offset.
 */
struct OcclusionShadowEval {
  float selected_sun_id;
  float best_weight;
  float found;
  LightData light;

  /* Node parameters, copied from calc_occlusion_shadow_impl(). */
  float3 offset;
  float soften;
  float bias;
  float channel;
  float threshold;
  float edge;
  /* 0 = lit, 1 = fully occluded. */
  float occlusion;

  void eval_directional([[resource_table]] LightEvalData &res, uint /*l_idx*/, LightData candidate)
  {
    /* Skip the sun extracted from the HDRI world: world strength must not drive
     * character shadows (same rule as Half-Lambert / SDF Driver). */
    if (candidate.sun().is_world_sun > 0.5f || candidate.tilemap_index == LIGHT_NO_SHADOW) {
      return;
    }
    if (selected_sun_id > 0.5f && candidate.sun().object_id != selected_sun_id) {
      return;
    }
    const float weight = max(candidate.color.x,
                             max(candidate.color.y, candidate.color.z)) *
                         candidate.power[LIGHT_DIFFUSE];
    /* A shadow-only sun (diffuse/specular = 0) must still be selectable. */
    if (selected_sun_id <= 0.5f && weight <= best_weight) {
      return;
    }
    if (selected_sun_id > 0.5f && found > 0.5f) {
      return;
    }
    best_weight = weight;
    found = 1.0f;
    light = candidate;

    [[resource_table]] ShadowRenderData &srd = res.shadow_data;

    const float3 P = g_data.P - offset;
    /* Light-space position, shared with ShadowRenderData::directional_sample_get(). */
    const float3 lP = transform_direction_transposed(candidate.object_to_world, P);
    const int level = shadow_directional_level(candidate, lP - candidate.position());
    const float receiver_dist = -lP.z - orderedIntBitsToFloat(candidate.clip_near);
    const float radius = max(soften, 0.0f) * 0.01f;
    const float bias_m = max(bias, 0.0f) * 0.01f + 1e-6f;

    /* Fixed 32-tap Vogel disk: no per-frame random pattern. Keep the depth
     * transition tiny so softness cannot detach contact shadows. */
    const int tap_count = radius > 0.0f ? 32 : 1;
    float occluded = 0.0f;
    for (int i = 0; i < tap_count; i++) {
      const float fi = float(i) + 0.5f;
      const float r = sqrt(fi * (1.0f / 32.0f));
      const float a = fi * 2.39996323f;
      const float2 disk = float2(cos(a), sin(a)) * r;
      const float3 tap_lP = lP + float3(disk * radius, 0.0f);
      occluded += srd.directional_occlusion_pcf(
          candidate, tap_lP, level, receiver_dist, bias_m, int(channel + 0.5f), radius);
    }

    const float occ = occluded / float(tap_count);
    const float th = clamp(threshold, 0.01f, 0.99f);
    /* Cel hardness controls the artistic transition, not pixel coverage.
     * Preserve approximately one pixel of antialiasing even at zero softness
     * and minimum Edge. No frame noise and no world-space blur floor. */
    const float feather = max(max(edge, 0.001f) * 0.5f, gpu_fwidth(occ) * 0.5f);
    occlusion = smoothstep(th - feather, th + feather, occ);
  }

  void eval_local([[resource_table]] LightEvalData & /*res*/,
                  uint /*l_idx*/,
                  LightData /*candidate*/)
  {
    /* The character shadow map is directional only (Sun). */
  }
};

template void light::foreach_visible<OcclusionShadowEval, LightEvalData>(
    const LightRenderData &, float2, float, OcclusionShadowEval &, LightEvalData &);

float calc_occlusion_shadow_impl(float sun_id,
                                 float3 offset,
                                 float soften,
                                 float strength,
                                 float bias,
                                 float channel,
                                 float threshold,
                                 float edge)
{
  [[resource_table]] LightEvalIterator &lights = resource_table_get(eevee::LightEvalIterator);
  [[resource_table]] const draw::View &views = resource_table_get(draw::View);

#ifdef GLSL_CPP_STUBS
  const float2 pixel = float2(0.0f);
#else
  const float2 pixel = gl_FragCoord.xy;
#endif

  if (sun_id < 0.0f || strength <= 0.0f) {
    return 1.0f;
  }

  OcclusionShadowEval cb;
  cb.selected_sun_id = sun_id;
  cb.best_weight = -1.0f;
  cb.found = 0.0f;
  cb.offset = offset;
  cb.soften = soften;
  cb.bias = bias;
  cb.channel = channel;
  cb.threshold = threshold;
  cb.edge = edge;
  cb.occlusion = 0.0f;

  const ViewMatrices view = views.get(0);
  float vPz = dot(view.forward(), g_data.P) - dot(view.forward(), view.position());
  [[resource_table]] LightEvalData &eres = lights.inner;
  light::foreach_visible(lights.light_data, pixel, vPz, cb, eres);

  if (cb.found < 0.5f) {
    /* No selected sun / no scene sun: no occlusion (lit). */
    return 1.0f;
  }
  return clamp(1.0f - clamp(strength, 0.0f, 1.0f) * cb.occlusion, 0.0f, 1.0f);
}

/* NPR: Eye see-through — fade hair/eyelashes that cover the eyes.
 * Per-eye coverage from the view ray vs a world-space sphere; combined with a
 * camera-front gate derived from the eye pair so back-of-head hair is never
 * punched through. Returns Alpha: 1 = opaque, lower where the fragment covers
 * an eye. */
float eye_sphere_coverage(float3 C, float3 D, float t_frag, float3 E, float R, float soften)
{
  if (R <= 0.0f) {
    return 0.0f;
  }
  float3 oc = E - C;
  float t_c = dot(oc, D);
  if (t_c <= 0.0f) {
    return 0.0f;
  }

  float3 closest = C + t_c * D;
  float h = length(E - closest);
  if (h >= R) {
    return 0.0f;
  }

  /* Front surface of the sphere along the ray. */
  float t_near = t_c - sqrt(R * R - h * h);
  /* A fragment covers the eye only when it is IN FRONT of the eye's front
   * surface (t is distance from the camera — smaller t = nearer). Fragments
   * behind the eye (back of the head, viewed from the front) must not fade.
   * The 0.35*R bias keeps eyelashes sitting on the eye surface counted. */
  if (t_frag > t_near + 0.35f * R) {
    return 0.0f;
  }

  if (soften <= 0.0f) {
    return 1.0f;
  }
  /* 1 at the eye center, fading to 0 at the silhouette. smoothstep() requires
   * edge0 < edge1, so invert the result instead of swapping the edges (the
   * swapped form is undefined in GLSL and driver-dependent). */
  float edge0 = R * (1.0f - clamp(soften, 0.0f, 0.99f));
  return 1.0f - smoothstep(edge0, R, h);
}

float calc_eye_see_through_impl(float3 eye_l,
                                float3 eye_r,
                                float radius,
                                float strength,
                                float soften)
{
  [[resource_table]] const draw::View &views = resource_table_get(draw::View);
  const ViewMatrices view = views.get(0);

  float3 C = view.position();
  float3 P = g_data.P;
  float3 D = P - C;
  float t_frag = length(D);
  if (t_frag < 1e-8f) {
    return 1.0f;
  }
  D /= t_frag;

  /* Camera-front gate. The face forward is derived from the eye pair instead
   * of being hardcoded to -Y, so the gate follows a rotated/turned head. Eye L
   * is the character's left (+X at rest), so `eye_r - eye_l` is the character's
   * right; cross(world up, right) = face forward (-Y at rest).
   * `to_face` points from the camera to the face, i.e. against the character's
   * forward, so the camera is in front when dot(to_face, -face_forward) > 0.
   * The gate is 0 at/below a profile view and reaches full strength around 45°
   * off the front axis: the fade stays a front/three-quarter feature instead of
   * turning the cheek into glass when the eye is seen edge-on. */
  float3 face_center = 0.5f * (eye_l + eye_r);
  float3 to_face = face_center - C;
  float to_face_len = length(to_face);
  if (to_face_len < 1e-8f) {
    return 1.0f;
  }
  float3 eye_axis = eye_r - eye_l;
  float3 face_forward;
  if (length(eye_axis) > 1e-6f) {
    float3 right = normalize(eye_axis);
    face_forward = normalize(cross(float3(0.0f, 0.0f, 1.0f), right));
  }
  else {
    face_forward = float3(0.0f, -1.0f, 0.0f);
  }
  float camera_side = dot(to_face / to_face_len, -face_forward);
  float front_gate = smoothstep(0.0f, 0.75f, camera_side);
  if (front_gate <= 0.0f) {
    return 1.0f;
  }

  float cov = eye_sphere_coverage(C, D, t_frag, eye_l, radius, soften);
  cov = max(cov, eye_sphere_coverage(C, D, t_frag, eye_r, radius, soften));

  float s = clamp(strength, 0.0f, 1.0f);
  return 1.0f - s * cov * front_gate;
}

/* NPR: signed front-ness of the camera relative to the world -Y face direction.
 * No per-eye anchors are needed: the camera forward itself gives the view angle.
 * 1 = straight on, 0 = profile, negative = back. The usual anime model faces -Y;
 * a model authored facing another axis would need this axis changed. */
float calc_eye_see_through_camera_side()
{
  [[resource_table]] const draw::View &views = resource_table_get(draw::View);
  const ViewMatrices view = views.get(0);
  /* view.forward() points towards the viewer, so negate it to get the camera
   * look direction (towards the character). */
  return dot(normalize(-view.forward()), float3(0.0f, 1.0f, 0.0f));
}

/* NPR: Eye see-through stencil gate. Only active from the face side so a
 * profile/back view cannot show the eye stage floating over the head. */
float calc_eye_see_through_gate_impl()
{
  return smoothstep(0.0f, 0.75f, calc_eye_see_through_camera_side());
}

/* NPR: Eye see-through — in-shader screen-space overlay.
 *
 * The EEVEE forward pipeline renders EyeSeeSource materials into the source
 * stage first. Its resolved color (previous_radiance_tx) is the full eye color,
 * and its a-channel is the eye coverage. The hair shader mixes that color over
 * itself (Emission/Mix Shader). No AOV or compositor is involved.
 *
 * `transparency` lets the hair show through again at larger view angles while
 * the front stays fully clear. `angle` is the view angle (radians) where that
 * fade starts; it ramps over 20 degrees. At 0 degrees (front) the fade is zero,
 * so the eye is fully visible; at 3/4 and beyond the hair regains the requested
 * fraction. The hard front gate still suppresses profile/back. */
void calc_eye_see_through_overlay_impl(float strength,
                                       float transparency,
                                       float angle,
                                       float3 &color,
                                       float &mask)
{
  color = float3(0.0f);
  mask = 0.0f;
#if defined(CREATE_INFO_eevee_npr_eye)
  const auto &previous_radiance_tx = sampler_get(eevee_npr_eye, previous_radiance_tx);
  const auto &eye_stage_alpha_tx = sampler_get(eevee_npr_eye, eye_stage_alpha_tx);
  const int2 texel = int2(gl_FragCoord.xy);
  /* a_channel_tx stores (radiance_a, transmittance_a) per texel. The source
   * stage covered this pixel when transmittance_a is below 1. */
  const float transmittance = texelFetch(eye_stage_alpha_tx, texel, 0).y;
  const float coverage = 1.0f - transmittance;
  if (coverage <= 0.0f) {
    return;
  }
  const float camera_side = calc_eye_see_through_camera_side();
  const float gate = smoothstep(0.0f, 0.75f, camera_side);
  if (gate <= 0.0f) {
    return;
  }
  const float view_angle = acos(clamp(camera_side, -1.0f, 1.0f));
  const float fade = smoothstep(angle, angle + radians(20.0f), view_angle);
  const float view_transparency = clamp(transparency, 0.0f, 1.0f) * fade;
  mask = clamp(strength, 0.0f, 1.0f) * gate * coverage * (1.0f - view_transparency);
  color = texelFetch(previous_radiance_tx, texel, 0).rgb;
#endif
}

float2 calc_half_lambert_impl(float3 N, float shaping_power, float threshold, float sun_id)
{
  [[resource_table]] LightEvalIterator &lights = resource_table_get(eevee::LightEvalIterator);
  [[resource_table]] const draw::View &views = resource_table_get(draw::View);

#ifdef GLSL_CPP_STUBS
  const float2 pixel = float2(0.0f);
#else
  const float2 pixel = gl_FragCoord.xy;
#endif

  HalfLambertEval cb;
  cb.N = N;
  cb.shaping_power = shaping_power;
  cb.best = 0.0f;
  cb.selected_sun_id = sun_id;

  const ViewMatrices view = views.get(0);
  float vPz = dot(view.forward(), g_data.P) - dot(view.forward(), view.position());
  [[resource_table]] LightEvalData &eres = lights.inner;
  light::foreach_visible(lights.light_data, pixel, vPz, cb, eres);

  float shading = cb.best;
  float mask = (shading >= threshold) ? 1.0f : 0.0f;
  return float2(shading, mask);
}

void forward_lighting_eval(const ViewMatrices view,
                           uint resource_id,
                           Thickness thickness,
                           float2 frag_co,
                           float3 &radiance,
                           float3 &transmittance)
{
  [[resource_table]] LightEvalIterator &lights = resource_table_get(eevee::LightEvalIterator);
  [[resource_table]] UtilityTexture &util_tx = resource_table_get(UtilityTexture);
  [[resource_table]] const Uniform &uni = resource_table_get(eevee::Uniform);
  /* clang-format off */ /* Multi-line macro breaks error line counting. */
  [[resource_table]] LightprobeRenderData &lightprobes = resource_table_get(eevee::LightprobeRenderData);
  [[resource_table]] LightprobePlaneRenderData &lightprobe_planes = resource_table_get(eevee::LightprobePlaneRenderData);
  /* clang-format on */
  [[resource_table]] LightEvalData &srt = lights.inner;
  [[resource_table]] draw::Infos &infos = resource_table_get(draw::Infos);

  float vPz = dot(view.forward(), g_data.P) - dot(view.forward(), view.position());
  float3 V = view.world_incident_vector(g_data.P);

  light::EvalCtx<false> ctx;
  for (uint i = 0u; i < 3; i++) [[unroll]] {
    if (srt.light_closure_eval_count_reflect > i) [[static_branch]] {
      ClosureUndetermined cl = g_closure_get(uchar(i));
      ctx.stack.cl[i] = closure_light_new(util_tx, cl, V);
    }
  }

  ctx.P = g_data.P;
  ctx.Ng = g_data.Ng;
  ctx.V = V;
  ctx.texel = frag_co;
  ctx.thickness = thickness;

  /* TODO(fclem): If transmission (no SSS) is present, we could reduce LIGHT_CLOSURE_EVAL_COUNT
   * by 1 for this evaluation and skip evaluating the transmission closure twice. */
  ObjectInfos object_infos = infos.get(resource_id);
  ctx.receiver_light_set = receiver_light_set_get(object_infos);
  ctx.terminator_normal_offset = object_infos.shadow_terminator_normal_offset;
  ctx.terminator_geometry_offset = object_infos.shadow_terminator_geometry_offset;

  lights.eval_reflection(ctx, vPz);

  if (srt.light_closure_eval_count_transmit > 0) [[static_branch]] {
    ClosureUndetermined cl_transmit = g_closure_get(0);
    if (closure_has_transmission(cl_transmit.type) || cl_transmit.type == CLOSURE_BSSRDF_BURLEY_ID)
    {
      light::EvalCtx<true> ctx_tr = light::init_from_reflect_ctx(ctx);
      ctx_tr.stack.cl[0] = closure_light_new(util_tx, cl_transmit, V, thickness);

      /* NOTE: Only evaluates `stack.cl[0]`. */
      lights.eval_transmission(ctx_tr, vPz);

      if (cl_transmit.type == CLOSURE_BSSRDF_BURLEY_ID) {
#if defined(GLSL_CPP_STUBS) || defined(MAT_SUBSURFACE)
        /* Apply transmission profile onto transmitted light and sum with reflected light. */
        float3 sss_profile = subsurface_transmission(
            util_tx, to_closure_subsurface(cl_transmit).sss_radius, thickness.value());
        ctx.stack.cl[0].light_shadowed += ctx_tr.stack.cl[0].light_shadowed * sss_profile;
        ctx.stack.cl[0].light_unshadowed += ctx_tr.stack.cl[0].light_unshadowed * sss_profile;
#endif
      }
      else {
        ctx.stack.cl[0].light_shadowed = ctx_tr.stack.cl[0].light_shadowed;
        ctx.stack.cl[0].light_unshadowed = ctx_tr.stack.cl[0].light_unshadowed;
      }
    }
  }

  LightProbeSample samp = lightprobes.load(frag_co, g_data.P, g_data.N, V);

  float clamp_indirect_sh = uni.uniform_buf.clamp.surface_indirect;
  samp.volume_irradiance = spherical_harmonics::clamp_energy(samp.volume_irradiance,
                                                             clamp_indirect_sh);

#ifdef MAT_REFLECTION /* Disable if only rough surfaces. */
  /* Planar reflection. */
  float3 planar_probe_radiance = float3(0.0f);
  float3 average_N = g_data.Ng * 0.001f;
  {
    /* Get average normal.  */
    for (uint i = 0u; i < 3; i++) [[unroll]] {
      if (srt.light_closure_eval_count_reflect > i) [[static_branch]] {
        ClosureUndetermined cl = g_closure_get(uchar(i));
        average_N += cl.N * cl.weight;
      }
    }
    average_N = safe_normalize(average_N);

    const int planar_id = lightprobe_planes.select_probe(g_data.P, average_N);

    if (planar_id == -1) {
      average_N = float3(0.0f);
    }
    else {
      float3 P_reflected = lightprobe::plane::parallax(
          lightprobe_planes.probe_planar_buf[planar_id], g_data.P, average_N, V);

      float2 ndc_P_reflected = view.point_world_to_ndc(P_reflected).xy;
      /* Planar probes are rendered upside down. */
      ndc_P_reflected.y = -ndc_P_reflected.y;
      float2 texel = view.ndc_to_screen(ndc_P_reflected);

      planar_probe_radiance =
          textureLod(lightprobe_planes.planar_radiance_tx, float3(texel, planar_id), 0.0).rgb;
      /* Discard background hits. */
      if (textureLod(lightprobe_planes.planar_depth_tx, float3(texel, planar_id), 0.0).r ==
          reverse_z::read(1.0f))
      {
        average_N = float3(0.0f);
      }
    }
  }
#endif

  /* Combine all radiance. */
  float3 radiance_direct = float3(0.0f);
  float3 radiance_indirect = float3(0.0f);

  for (uint i = 0u; i < 3; i++) [[unroll]] {
    if (srt.light_closure_eval_count_reflect > i) [[static_branch]] {
      ClosureUndetermined cl = g_closure_get_resolved(uchar(i), 1.0f);
      if (cl.weight > CLOSURE_WEIGHT_CUTOFF) {
        float3 direct_light = ctx.stack.cl[i].light_shadowed;
        float3 indirect_light = lightprobes.eval(samp, cl, g_data.P, V, thickness);

#ifdef MAT_REFLECTION
        if (cl.type == CLOSURE_BSDF_MICROFACET_GGX_REFLECTION_ID) {
          const float blend = saturate(to_closure_reflection(cl).roughness * -10.0f + 1.0f) *
                              saturate(dot(average_N, cl.N) * 100.0f - 99.0f);
          indirect_light = mix(indirect_light, planar_probe_radiance, blend);
        }
#endif

        if ((cl.type == CLOSURE_BSDF_TRANSLUCENT_ID ||
             cl.type == CLOSURE_BSDF_MICROFACET_GGX_REFRACTION_ID) &&
            (thickness.value() != 0.0f))
        {
          /* We model two transmission event, so the surface color need to be applied twice. */
          cl.color *= cl.color;
        }

        radiance_direct += direct_light * cl.color;
        radiance_indirect += indirect_light * cl.color;
      }
    }
  }
  /* Light clamping. */
  float clamp_direct = uni.uniform_buf.clamp.surface_direct;
  float clamp_indirect = uni.uniform_buf.clamp.surface_indirect;

  radiance_direct = colorspace::brightness_clamp_max(radiance_direct, clamp_direct);
  radiance_indirect = colorspace::brightness_clamp_max(radiance_indirect, clamp_indirect);

  radiance_direct *= uni.uniform_buf.clamp.direct_scale;
  radiance_indirect *= uni.uniform_buf.clamp.indirect_scale;

  radiance = radiance_direct + radiance_indirect + g_emission;

  transmittance = g_transmittance;
}

}  // namespace eevee
