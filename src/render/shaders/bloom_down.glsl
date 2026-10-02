// ============================================================================
// bloom_down.glsl - one step of the bloom downsample chain (6 levels, each half
// the size of the previous one). 13-tap filter from Jimenez, "Next Generation
// Post Processing in Call of Duty: Advanced Warfare" (SIGGRAPH 2014): five
// overlapping 2x2 boxes, which avoids the blocky look of a plain 2x2 average.
//
// The first step (full-res HDR -> level 0) uses a "Karis average": each box is
// weighted by 1 / (1 + luminance), so a single extremely bright pixel (a lens
// core) cannot dominate and flicker as the camera moves.
// ============================================================================

#ifdef VERTEX_SHADER
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
#endif

#ifdef FRAGMENT_SHADER
uniform sampler2D u_source;
uniform vec4 u_params;  // xy = source texel size, z = 1 for the first (Karis) step, w = unused

out vec4 o_color;

float karisWeight(vec3 c) {
    return 1.0 / (1.0 + dot(c, vec3(0.2126, 0.7152, 0.0722)));
}

void main() {
    vec2 texel = u_params.xy;
    // The target is half the source size, so target pixel centre (j + 0.5)
    // lands on source texel coordinate 2j + 1, i.e. between four source texels.
    vec2 uv = gl_FragCoord.xy * 2.0 * texel;

    vec3 a = texture(u_source, uv + texel * vec2(-2.0, -2.0)).rgb;
    vec3 b = texture(u_source, uv + texel * vec2(0.0, -2.0)).rgb;
    vec3 c = texture(u_source, uv + texel * vec2(2.0, -2.0)).rgb;
    vec3 d = texture(u_source, uv + texel * vec2(-1.0, -1.0)).rgb;
    vec3 e = texture(u_source, uv + texel * vec2(1.0, -1.0)).rgb;
    vec3 f = texture(u_source, uv + texel * vec2(-2.0, 0.0)).rgb;
    vec3 g = texture(u_source, uv).rgb;
    vec3 h = texture(u_source, uv + texel * vec2(2.0, 0.0)).rgb;
    vec3 i = texture(u_source, uv + texel * vec2(-1.0, 1.0)).rgb;
    vec3 j = texture(u_source, uv + texel * vec2(1.0, 1.0)).rgb;
    vec3 k = texture(u_source, uv + texel * vec2(-2.0, 2.0)).rgb;
    vec3 l = texture(u_source, uv + texel * vec2(0.0, 2.0)).rgb;
    vec3 m = texture(u_source, uv + texel * vec2(2.0, 2.0)).rgb;

    // Five boxes: the centre one (d e i j) weighs 0.5, the four corner ones 0.125 each.
    vec3 box0 = (d + e + i + j) * 0.25;
    vec3 box1 = (a + b + f + g) * 0.25;
    vec3 box2 = (b + c + g + h) * 0.25;
    vec3 box3 = (f + g + k + l) * 0.25;
    vec3 box4 = (g + h + l + m) * 0.25;

    vec3 result;
    if (u_params.z > 0.5) {
        float w0 = karisWeight(box0) * 0.5;
        float w1 = karisWeight(box1) * 0.125;
        float w2 = karisWeight(box2) * 0.125;
        float w3 = karisWeight(box3) * 0.125;
        float w4 = karisWeight(box4) * 0.125;
        result = (box0 * w0 + box1 * w1 + box2 * w2 + box3 * w3 + box4 * w4) / (w0 + w1 + w2 + w3 + w4);
    } else {
        result = box0 * 0.5 + (box1 + box2 + box3 + box4) * 0.125;
    }
    o_color = vec4(result, 1.0);
}
#endif
