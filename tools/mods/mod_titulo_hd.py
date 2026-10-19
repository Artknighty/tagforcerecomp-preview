"""Gera o mod gen/mods/TituloHD: o 'PRESS START BUTTON' e a barra rosa da tela inicial refeitos em 8x.

Entrada: as duas texturas ORIGINAIS do jogo, despejadas com TF_TEX_DUMP (sem substituta), em gen/dump_tex/titulo/:
  e8aa3934.png (256x16, texto 'PRESS START BUTTON')  e  1fe2e572.png (256x32, barra rosa).
Saida: gen/mods/TituloHD/textures/<hash8>.png e mod.txt. O mod se chama 'TituloHD' para vencer o pacote
PPSSPP_MikeyMK na ordem alfabetica das pastas (T > P). O pacote de terceiros nao e tocado.

Modos (argumento):
  (padrao) fonte    texto redesenhado com Arial Black (ariblk.ttf do Windows), ocupando exatamente a caixa do original
                    (mesma posicao e altura de maiusculas), com degrade verde e sombra escura. Bordas nitidas em 8x.
  original          mesma forma das letras do original, ampliada: contorno por limiar suave (mais fiel ao desenho,
                    mas as bordas continuam um pouco moles porque a origem tem 16 px de altura).
A barra rosa: cantos e bordas nitidos (limiar suave) + brilho suave por fora (o 'glow' do original).

Por que nao so ampliar por IA/bicubico: o pacote traz essas texturas em 4x, mas com bordas moles; a logo do titulo
(em HD) fica nitida ao lado.

Uso: python tools/mods/mod_titulo_hd.py [fonte|original]
"""
import os
import sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
ORIG = os.path.join(ROOT, 'gen', 'dump_tex', 'titulo')
MOD = os.path.join(ROOT, 'gen', 'mods', 'TituloHD')
FONTE = 'C:/Windows/Fonts/ariblk.ttf'
K = 8  # fator de ampliacao (1 px do jogo = 8 texels = 1 pixel na tela em 8x)
TEXTO = 'PRESS START BUTTON'


def up(a, w, h):
    """Bicubico de uma matriz float."""
    return np.asarray(Image.fromarray(a.astype(np.float32), mode='F').resize((w, h), Image.BICUBIC), dtype=np.float32)


def blur(a, sigma):
    """Desfoque gaussiano separavel (numpy puro)."""
    r = int(np.ceil(sigma * 3))
    x = np.arange(-r, r + 1, dtype=np.float32)
    k = np.exp(-x * x / (2 * sigma * sigma))
    k /= k.sum()
    for axis in (0, 1):
        pad = [(0, 0), (0, 0)]
        pad[axis] = (r, r)
        p = np.pad(a, pad, mode='edge')
        out = np.zeros_like(a)
        for i, kv in enumerate(k):
            sl = [slice(None), slice(None)]
            sl[axis] = slice(i, i + a.shape[axis])
            out += kv * p[tuple(sl)]
        a = out
    return a


def smooth_step(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)


def carregar(nome):
    rgba = np.asarray(Image.open(os.path.join(ORIG, nome + '.png')).convert('RGBA'), dtype=np.float32) / 255
    return rgba[..., :3], rgba[..., 3]


def salvar(rgb, a, nome):
    img = np.dstack([np.clip(rgb, 0, 1), np.clip(a, 0, 1)])
    os.makedirs(os.path.join(MOD, 'textures'), exist_ok=True)
    Image.fromarray((img * 255 + 0.5).astype(np.uint8), 'RGBA').save(os.path.join(MOD, 'textures', nome + '.png'))
    print(nome, img.shape[1], 'x', img.shape[0])


def degrade_e_sombra(mascara, sombra_dx=6, sombra_dy=6):
    """Mascara da letra (0..1, 8x) -> (cor, alfa) com degrade verde do topo ao pe e sombra escura deslocada."""
    h, w = mascara.shape
    t = np.linspace(0, 1, h, dtype=np.float32)[:, None, None]
    topo = np.array([0.84, 1.0, 0.25], np.float32)
    base = np.array([0.55, 0.95, 0.0], np.float32)
    cor = np.broadcast_to((1 - t) * topo + t * base, (h, w, 3))
    sombra = np.clip(np.roll(np.roll(blur(mascara, 2.0), sombra_dy, axis=0), sombra_dx, axis=1), 0, 1)
    a = mascara + sombra * (1 - mascara)
    rgb = (cor * mascara[..., None] + np.array([0.2, 0.0, 0.2], np.float32) * (sombra * (1 - mascara))[..., None])
    return rgb / np.maximum(a, 1e-4)[..., None], a


def texto_fonte():
    """Redesenho com a fonte, na caixa exata do texto original (posicao e altura de maiusculas)."""
    C, A = carregar('e8aa3934')
    H, W = A.shape
    w, h = W * K, H * K
    g = np.clip(C[..., 1] / 0.78, 0, 1)      # letra verde (G alto); sombra escura (G baixo)
    La = A * g
    ys, xs = np.where(La > 0.5)
    x0, x1, y0, y1 = xs.min() * K, (xs.max() + 1) * K, ys.min() * K, (ys.max() + 1) * K
    # renderiza grande, mede a TINTA (nao a caixa de texto) e encaixa a tinta exatamente na caixa do original
    # (mesma posicao, altura de maiusculas e largura). A fonte e mais larga que a do jogo: fica levemente comprimida.
    grande = Image.new('L', (4000, 600), 0)   # 200 px de fonte = ~2500 px de largura: cabe com folga
    ImageDraw.Draw(grande).text((100, 50), TEXTO, font=ImageFont.truetype(FONTE, 200), fill=255)
    tinta = grande.getbbox()
    recorte = grande.crop(tinta).resize((x1 - x0, y1 - y0), Image.LANCZOS)
    img = Image.new('L', (w, h), 0)
    img.paste(recorte, (x0, y0))
    mascara = np.asarray(img, dtype=np.float32) / 255
    rgb, a = degrade_e_sombra(mascara)
    salvar(rgb, a, 'e8aa3934')


def texto_original():
    """Mesma forma do original, ampliada: contorno por limiar suave da alfa (sem redesenhar a fonte)."""
    C, A = carregar('e8aa3934')
    H, W = A.shape
    w, h = W * K, H * K
    g = np.clip(C[..., 1] / 0.78, 0, 1)
    La, Sa = A * g, A * (1 - g)
    # a alfa ampliada e suavizada (2,5 texels) antes do limiar: com 4 as letras comecam a colar (P/R, BUT)
    Lu = blur(up(La, w, h), 2.5)
    L = blur(smooth_step(0.4, 0.6, Lu), 0.6)
    Cp = np.stack([up(C[..., c] * La, w, h) for c in range(3)], -1)
    Cl = np.clip(Cp / np.maximum(Lu, 1e-3)[..., None], 0, 1)
    escuro = Sa > 0.5
    cor_sombra = C[escuro].mean(0) if escuro.any() else np.array([0.25, 0.0, 0.25], np.float32)
    Su = blur(up(Sa, w, h), 2.0)
    fora = Su * (1 - L)
    a = L + fora
    rgb = (Cl * L[..., None] + cor_sombra * fora[..., None]) / np.maximum(a, 1e-4)[..., None]
    salvar(rgb, a, 'e8aa3934')


def barra():
    """Barra rosa: contorno nitido (limiar suave) + brilho suave por fora, como o original."""
    C, A = carregar('1fe2e572')
    H, W = A.shape
    w, h = W * K, H * K
    Cu = np.stack([up(C[..., c], w, h) for c in range(3)], -1)
    Au = up(A, w, h)
    nitida = blur(smooth_step(0.4, 0.6, blur(Au, 2.5)), 0.6)
    brilho = blur(Au, 6.0) * 0.35
    a = np.maximum(nitida, brilho)
    salvar(Cu, a, '1fe2e572')


if __name__ == '__main__':
    modo = sys.argv[1] if len(sys.argv) > 1 else 'fonte'
    if modo == 'fonte':
        texto_fonte()
    elif modo == 'original':
        texto_original()
    else:
        sys.exit('modo desconhecido: %s (use fonte ou original)' % modo)
    barra()
    mt = os.path.join(MOD, 'mod.txt')
    if not os.path.exists(mt):
        with open(mt, 'w', encoding='utf-8') as f:
            f.write('name=HD Title Screen (Press Start)\nauthor=Tag Force Recomp\nversion=1.0\n'
                    'description=Press Start text and bar of the title screen redrawn at 8x (sharp edges)\n')
