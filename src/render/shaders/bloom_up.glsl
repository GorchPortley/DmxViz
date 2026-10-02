// ============================================================================
// bloom_up.glsl - one step of the bloom upsample chain: the smaller level is
// blurred with a 3x3 tent filter, scaled by kScatter and ADDED (blend ONE,
// ONE) to the next larger level. After the last step level 0 holds all six
// blur radii, each wider one weaker (kScatter^k): a bright tight core with a
// soft falloff, much like the glare of a camera lens. composite.glsl divides
// by the sum of the weights.
// ============================================================================

#ifdef VERTEX_SHADER
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
#endif

#ifdef FRAGMENT_SHADER
uniform sampler2D u_source;
uniform vec4 u_params;  // xy = source texel size, zw = target texel size

const float kScatter = 0.65;  // keep in sync with kBloomWeightSum in composite.glsl

out vec4 o_color;

void main() {
    vec2 uv = gl_FragCoord.xy * u_params.zw;
    vec2 t = u_params.xy;  // filter radius: one source texel
    vec3 sum = texture(u_source, uv).rgb * 4.0;
    sum += (texture(u_source, uv + vec2(-t.x, 0.0)).rgb + texture(u_source, uv + vec2(t.x, 0.0)).rgb +
            texture(u_source, uv + vec2(0.0, -t.y)).rgb + texture(u_source, uv + vec2(0.0, t.y)).rgb) * 2.0;
    sum += texture(u_source, uv + vec2(-t.x, -t.y)).rgb + texture(u_source, uv + vec2(t.x, -t.y)).rgb +
           texture(u_source, uv + vec2(-t.x, t.y)).rgb + texture(u_source, uv + vec2(t.x, t.y)).rgb;
    o_color = vec4(sum / 16.0 * kScatter, 1.0);
}
#endif
