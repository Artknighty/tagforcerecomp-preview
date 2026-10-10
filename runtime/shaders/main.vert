#version 450
// vertices ja projetados (coordenadas nativas 480x272); u/w, v/w e 1/w sao interpolados na tela (perspectiva correta)
layout(location = 0) in vec3 pos;
layout(location = 1) in vec3 uvw;
layout(location = 2) in vec4 col;
layout(push_constant) uniform PCV { layout(offset = 80) vec2 fbsize; } pcv;   // tamanho nativo do quadro sombra (480x272 na tela; alvos de textura tem outros)
layout(location = 0) out vec3 vUvw;
layout(location = 1) out vec4 vCol;
layout(location = 2) out float vZ;
void main() {
    gl_Position = vec4(pos.x * (2.0 / pcv.fbsize.x) - 1.0, pos.y * (2.0 / pcv.fbsize.y) - 1.0, 0.5, 1.0);
    vUvw = vec3(uvw.xy * uvw.z, uvw.z);
    vCol = col;
    vZ = pos.z;
}
