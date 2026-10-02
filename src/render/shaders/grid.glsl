// ============================================================================
// grid.glsl - the floor grid, drawn over the final 8-bit image.
//
// A full-screen pass: each pixel intersects its view ray with the plane y = 0
// and draws anti-aliased lines there (1 m minor, 5 m major, red X / blue Z
// axes). Because it is computed per pixel the grid has no edge - it looks
// infinite - and it fades with distance and at grazing angles to avoid moire.
// It is hidden where the G-buffer has geometry in front of the floor plane.
// ============================================================================

#include "common.glsl"

#ifdef VERTEX_SHADER
void main() {
    gl_Position = fullscreenTrianglePosition();
}
#endif

#ifdef FRAGMENT_SHADER
uniform sampler2D u_gDistance;

out vec4 o_color;

// 1 on a grid line, falling to 0 one line-width (in pixels) away.
float gridLines(vec2 p, float spacing, float widthPx, out float density) {
    vec2 coord = p / spacing;
    vec2 deriv = max(fwidth(coord), vec2(1e-6));
    vec2 distPx = abs(fract(coord - 0.5) - 0.5) / deriv;
    density = max(deriv.x, deriv.y);  // grid cells per pixel: > ~0.3 means the lines merge
    return 1.0 - min(min(distPx.x, distPx.y) / widthPx, 1.0);
}

void main() {
    vec3 ro = frame.cameraPos.xyz;
    vec3 rd = viewRayDirection(gl_FragCoord.xy * frame.viewport.zw);
    if (abs(rd.y) < 1e-5) discard;
    float t = -ro.y / rd.y;
    if (t <= 0.0) discard;

    float sceneDistance = texelFetch(u_gDistance, ivec2(gl_FragCoord.xy), 0).r;
    if (sceneDistance < t * 0.998 - 0.02) discard;  // something stands in front of the floor here

    vec3 p = ro + rd * t;
    float minorDensity, majorDensity;
    float minor = gridLines(p.xz, 1.0, 1.0, minorDensity);
    float major = gridLines(p.xz, 5.0, 1.3, majorDensity);
    minor *= 1.0 - smoothstep(0.15, 0.35, minorDensity);
    major *= 1.0 - smoothstep(0.15, 0.35, majorDensity);

    vec2 axisWidth = max(fwidth(p.xz), vec2(1e-6)) * 1.5;
    float axisX = 1.0 - min(abs(p.z) / axisWidth.y, 1.0);  // the X axis is the line z = 0
    float axisZ = 1.0 - min(abs(p.x) / axisWidth.x, 1.0);

    float fade = exp(-t * 0.035) * smoothstep(0.02, 0.2, abs(rd.y));
    vec3 color = vec3(0.55);
    float alpha = max(minor * 0.16, major * 0.32);
    if (axisX > 0.01 && axisX >= axisZ) {
        color = vec3(0.85, 0.25, 0.22);
        alpha = max(alpha, axisX * 0.55);
    } else if (axisZ > 0.01) {
        color = vec3(0.25, 0.45, 0.95);
        alpha = max(alpha, axisZ * 0.55);
    }
    alpha *= fade;
    if (alpha < 0.002) discard;
    o_color = vec4(color, alpha);
}
#endif
