#version 450
#extension GL_EXT_samplerless_texture_functions : require
// quadro nativo (cor + profundidade 16 bits) replicado N x N no quadro sombra
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outC;
layout(set = 0, binding = 0) uniform texture2D natC;
layout(set = 0, binding = 1) uniform texture2D natZ;
layout(push_constant) uniform PC { vec4 p0; } pc;
void main() {
    ivec2 q = ivec2(gl_FragCoord.xy) / int(pc.p0.x);
    outC = texelFetch(natC, q, 0);
    gl_FragDepth = texelFetch(natZ, q, 0).r;
}
