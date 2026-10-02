// ============================================================================
// bloom_down.glsl - one step of the bloom downsample chain (6 levels, each half
// the size of the previous one). 13-tap filter from Jimenez, "Next Generation
// Post Processing in Call of Duty: Advanced Warfare" (SIGGRAPH 2014): five
// overlapping 2x2 boxes, which avoids the blocky look of a plain 2x2 average.
//
// The first step (full-res HDR -> level 0) also tames extreme values: every
// tap is clamped to kMaxInput and the boxes are combined with a "Karis
// average" (weighted by 1 / (1 + luminance)). Lens cores are thousands of
// times brighter than anything else; unclamped, their energy would spread a
// coloured veil over the whole image and flicker as they move by a pixel.
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

const float kMaxInput = 40.0;  // HDR units

float karisWeight(vec3 c) {
    return 1.0 / (1.0 + dot(c, vec3(0.2126, 0.7152, 0.0722)));
}

vec3 fetch(vec2 uv, bool clampInput) {
    vec3 c = texture(u_source, uv).rgb;
    if (clampInput) {
        float peak = max(c.r, max(c.g, c.b));
        if (peak > kMaxInput) c *= kMaxInput / peak;
    }
    return c;
}

void main() {
    vec2 texel = u_params.xy;
    // The target is half the source size, so target pixel centre (j + 0.5)
    // lands on source texel coordinate 2j + 1, i.e. between four source texels.
    vec2 uv = gl_FragCoord.xy * 2.0 * texel;
    bool first = u_params.z > 0.5;

    vec3 a = fetch(uv + texel * vec2(-2.0, -2.0), first);
    vec3 b = fetch(uv + texel * vec2(0.0, -2.0), first);
    vec3 c = fetch(uv + texel * vec2(2.0, -2.0), first);
    vec3 d = fetch(uv + texel * vec2(-1.0, -1.0), first);
    vec3 e = fetch(uv + texel * vec2(1.0, -1.0), first);
    vec3 f = fetch(uv + texel * vec2(-2.0, 0.0), first);
    vec3 g = fetch(uv, first);
    vec3 h = fetch(uv + texel * vec2(2.0, 0.0), first);
    vec3 i = fetch(uv + texel * vec2(-1.0, 1.0), first);
    vec3 j = fetch(uv + texel * vec2(1.0, 1.0), first);
    vec3 k = fetch(uv + texel * vec2(-2.0, 2.0), first);
    vec3 l = fetch(uv + texel * vec2(0.0, 2.0), first);
    vec3 m = fetch(uv + texel * vec2(2.0, 2.0), first);

    // Five boxes: the centre one (d e i j) weighs 0.5, the four corner ones 0.125 each.
    vec3 box0 = (d + e + i + j) * 0.25;
    vec3 box1 = (a + b + f + g) * 0.25;
    vec3 box2 = (b + c + g + h) * 0.25;
    vec3 box3 = (f + g + k + l) * 0.25;
    vec3 box4 = (g + h + l + m) * 0.25;

    vec3 result;
    if (first) {
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
