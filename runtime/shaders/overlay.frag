#version 450
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outC;
layout(set = 0, binding = 0) uniform texture2D src;
layout(set = 1, binding = 0) uniform sampler smp[8];
void main() { outC = textureLod(sampler2D(src, smp[7]), uv, 0.0); }
