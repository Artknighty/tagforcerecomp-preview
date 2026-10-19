#version 450
#extension GL_EXT_samplerless_texture_functions : require
// media de cada bloco N x N do quadro sombra (para comparar com o nativo)
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outC;
layout(set = 0, binding = 0) uniform texture2D src;
layout(push_constant) uniform PC { vec4 p0; } pc;
void main() {
    int n = int(pc.p0.x);
    ivec2 q = ivec2(gl_FragCoord.xy) * n;
    vec4 s = vec4(0.0);
    for (int y = 0; y < n; y++) for (int x = 0; x < n; x++) s += texelFetch(src, q + ivec2(x, y), 0);
    outC = s / float(n * n);
}
