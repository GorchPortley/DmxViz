// ============================================================================
// gbuffer.glsl - pass 1: draws every mesh once and stores what the lighting
// passes need to know about the visible surface in each pixel.
//
// Outputs (multiple render targets):
//   0 RGBA8   albedo.rgb, roughness
//   1 RGBA16F octahedral normal.xy, metallic, highlight (0 none, 1 hover, 2 selected)
//   2 R32F    distance from the camera along the view ray (m)
//   3 RGBA16F HDR colour: ambient light + emissive. Spot lights and beams are
//             added on top of this target by the following passes.
//
// Instanced: one draw per mesh, per-instance data comes from a second vertex
// buffer (InstanceGpu in GpuTypes.h).
// ============================================================================

#include "common.glsl"

#ifdef VERTEX_SHADER
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;
layout(location = 3) in vec4 i_world0;  // world matrix, column by column
layout(location = 4) in vec4 i_world1;
layout(location = 5) in vec4 i_world2;
layout(location = 6) in vec4 i_world3;
layout(location = 7) in vec4 i_albedoRoughness;
layout(location = 8) in vec4 i_emissiveMetallic;
layout(location = 9) in vec4 i_extra;  // x = highlight

out vec3 v_worldPos;
out vec3 v_normal;
flat out vec4 v_albedoRoughness;
flat out vec4 v_emissiveMetallic;
flat out float v_highlight;

void main() {
    mat4 world = mat4(i_world0, i_world1, i_world2, i_world3);
    vec4 worldPos = world * vec4(a_position, 1.0);

    // Normals transform with the inverse transpose. The cofactor matrix is the
    // inverse transpose times the determinant - cheaper, and fine because we
    // normalise anyway. Multiplying by sign(det) keeps mirrored instances right.
    mat3 m = mat3(world);
    mat3 cofactor = mat3(cross(m[1], m[2]), cross(m[2], m[0]), cross(m[0], m[1]));
    float det = dot(m[0], cofactor[0]);
    v_normal = cofactor * a_normal * (det < 0.0 ? -1.0 : 1.0);

    v_worldPos = worldPos.xyz;
    v_albedoRoughness = i_albedoRoughness;
    v_emissiveMetallic = i_emissiveMetallic;
    v_highlight = i_extra.x;
    gl_Position = frame.viewProj * worldPos;
    // a_uv is part of the vertex format but unused until textured materials exist.
}
#endif

#ifdef FRAGMENT_SHADER
in vec3 v_worldPos;
in vec3 v_normal;
flat in vec4 v_albedoRoughness;
flat in vec4 v_emissiveMetallic;
flat in float v_highlight;

layout(location = 0) out vec4 o_albedoRoughness;
layout(location = 1) out vec4 o_normal;
layout(location = 2) out float o_distance;
layout(location = 3) out vec4 o_hdr;

void main() {
    vec3 toCamera = frame.cameraPos.xyz - v_worldPos;
    vec3 n = normalize(v_normal);

    // Meshes are drawn without back-face culling (imported models often have
    // inconsistent winding, stage planes are one-sided). If we see the back of
    // a triangle - judged by its true geometric normal from screen derivatives -
    // shade the side that faces us.
    vec3 faceNormal = cross(dFdx(v_worldPos), dFdy(v_worldPos));
    if (dot(faceNormal, toCamera) * dot(faceNormal, n) < 0.0) n = -n;

    vec3 albedo = v_albedoRoughness.rgb;
    float metallic = v_emissiveMetallic.a;

    o_albedoRoughness = vec4(albedo, v_albedoRoughness.a);
    o_normal = vec4(encodeNormal(n), metallic, v_highlight);
    o_distance = length(toCamera);

    // Ambient: a dim, slightly top-lit fill so unlit geometry still reads.
    // Metals reflect rather than diffuse, so they take less of it.
    vec3 ambient = frame.ambientExposure.rgb * (0.7 + 0.3 * n.y);
    o_hdr = vec4(albedo * ambient * (1.0 - 0.7 * metallic) + v_emissiveMetallic.rgb, 1.0);
}
#endif
