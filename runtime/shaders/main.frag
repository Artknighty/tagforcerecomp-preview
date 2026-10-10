#version 450
layout(location = 0) in vec3 vUvw;
layout(location = 1) in vec4 vCol;
layout(location = 2) in float vZ;
layout(location = 0) out vec4 outC;
layout(set = 0, binding = 0) uniform texture2D tex;
layout(set = 1, binding = 0) uniform sampler smp[8];
layout(push_constant) uniform PC {
    vec4 tenv;
    uint flags; uint tfn; uint afunc; uint aref;
    uint amask; uint aover; uint sidx; uint aconst;
    vec2 texsz; vec2 pad2;
    vec4 sfix;
} pc;
vec4 fetch(vec2 uv) {
    switch (pc.sidx) {
    case 0: return texture(sampler2D(tex, smp[0]), uv);
    case 1: return texture(sampler2D(tex, smp[1]), uv);
    case 2: return texture(sampler2D(tex, smp[2]), uv);
    case 3: return texture(sampler2D(tex, smp[3]), uv);
    case 4: return texture(sampler2D(tex, smp[4]), uv);
    case 5: return texture(sampler2D(tex, smp[5]), uv);
    case 6: return texture(sampler2D(tex, smp[6]), uv);
    default: return texture(sampler2D(tex, smp[7]), uv);
    }
}
bool tf(uint f, uint a, uint b) {
    switch (f) {
    case 0: return false; case 1: return true; case 2: return a == b; case 3: return a != b;
    case 4: return a < b; case 5: return a <= b; case 6: return a > b; default: return a >= b;
    }
}
void main() {
    vec3 rgb = vCol.rgb; float a = vCol.a;
    if ((pc.flags & 16u) == 0u) {
        if ((pc.flags & 1u) != 0u) {
            vec2 uv = (vUvw.xy / vUvw.z) / pc.texsz;
            vec4 t = fetch(uv);
            bool ta = (pc.flags & 2u) != 0u;
            vec3 vc = rgb; float va = a;
            if (pc.tfn == 0u) { rgb = t.rgb * vc; if ((pc.flags & 4u) != 0u) rgb = min(rgb * 2.0, vec3(1.0)); a = ta ? t.a * va : va; }
            else if (pc.tfn == 1u) { rgb = ta ? t.rgb * t.a + vc * (1.0 - t.a) : t.rgb; a = va; }
            else if (pc.tfn == 2u) { rgb = vc * (vec3(1.0) - t.rgb) + pc.tenv.rgb * t.rgb; a = ta ? t.a * va : va; }
            else if (pc.tfn == 3u) { rgb = t.rgb; a = ta ? t.a : va; }
            else { rgb = min(vc + t.rgb, vec3(1.0)); a = ta ? t.a * va : va; }
        }
        if ((pc.flags & 8u) != 0u) {
            uint a8 = uint(a * 255.0 + 0.5) & pc.amask;
            if (!tf(pc.afunc, a8, pc.aref)) discard;
        }
        if (pc.aover == 3u) rgb *= pc.sfix.rgb;
        if (pc.aover == 1u) a = min(1.0, 2.0 * a);
        else if (pc.aover == 2u) a = 1.0 - min(1.0, 2.0 * (1.0 - a));
    }
    if ((pc.flags & 32u) != 0u) a = float(pc.aconst) / 255.0;   // desenho com stencil sem blend: o alfa gravado e o novo valor do stencil
    outC = vec4(rgb, a);
    gl_FragDepth = floor(clamp(vZ, 0.0, 65535.0)) / 65535.0;
}
