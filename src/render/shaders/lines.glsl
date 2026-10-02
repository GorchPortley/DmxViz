// ============================================================================
// lines.glsl - debug / gizmo lines (RenderScene::lines), unlit, drawn into the
// final 8-bit image. Drawn twice: once where they are hidden behind geometry
// (depth test GREATER, faint) and once where visible (LESS_EQUAL, full).
// ============================================================================

#include "common.glsl"

#ifdef VERTEX_SHADER
layout(location = 0) in vec4 a_position;
layout(location = 1) in vec4 a_color;  // linear RGBA

out vec4 v_color;

void main() {
    v_color = a_color;
    gl_Position = frame.viewProj * vec4(a_position.xyz, 1.0);
}
#endif

#ifdef FRAGMENT_SHADER
in vec4 v_color;

uniform vec4 u_params;  // x = alpha multiplier (lower for the hidden pass)

out vec4 o_color;

void main() {
    o_color = vec4(linearToSrgb(clamp(v_color.rgb, 0.0, 1.0)), v_color.a * u_params.x);
}
#endif
