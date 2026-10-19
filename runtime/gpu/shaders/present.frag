#version 450
// apresentacao: p0 = (modo 0 nitido / 1 nitido suavizado / 2 bilinear, k, largura da fonte, altura da fonte)
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outC;
layout(set = 0, binding = 0) uniform texture2D src;
layout(set = 1, binding = 0) uniform sampler smp[8];
layout(push_constant) uniform PC { vec4 p0; } pc;
void main() {
    int mode = int(pc.p0.x);
    if (mode == 0) { outC = vec4(textureLod(sampler2D(src, smp[3]), uv, 0.0).rgb, 1.0); return; }
    if (mode == 2) { outC = vec4(textureLod(sampler2D(src, smp[7]), uv, 0.0).rgb, 1.0); return; }
    vec2 sz = pc.p0.zw;
    if (mode == 3) {   // reducao (fonte maior que a janela): media de n x n amostras bilineares dentro do pixel de destino (supersampling)
        float r = pc.p0.y; int n = clamp(int(ceil(r)), 2, 8); vec3 acc = vec3(0.0);
        for (int y = 0; y < n; y++) for (int x = 0; x < n; x++) acc += textureLod(sampler2D(src, smp[7]), uv + ((vec2(x, y) + 0.5) / float(n) - 0.5) * r / sz, 0.0).rgb;
        outC = vec4(acc / float(n * n), 1.0); return;
    }
    vec2 p = uv * sz - 0.5; vec2 fl = floor(p); vec2 f = p - fl;
    f = clamp((f - 0.5) * pc.p0.y + 0.5, 0.0, 1.0);
    outC = vec4(textureLod(sampler2D(src, smp[7]), (fl + 0.5 + f) / sz, 0.0).rgb, 1.0);
}
