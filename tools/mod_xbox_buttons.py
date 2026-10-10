"""Gera o mod gen/mods/XboxButtons: troca os icones de botao do PSP pelos do controle de Xbox.

Entrada: as imagens dos botoes placed by the user in gen/mods/XboxButtons/*.png e, como base do layout, a folha de botoes em alta
resolucao do pacote de texturas (gen/mods/PPSSPP_MikeyMK, 512x256) - sem o pacote, usa a textura original despejada (128x64).
Saida: gen/mods/XboxButtons/textures/<hash8>.png (o mod de nome maior em ordem alfabetica vence o pacote) e mod.txt.

Texturas trocadas:
  b505bc0f (128x64): folha com triangulo/circulo/xis/quadrado, direcional, START, SELECT, L e R
  6d527b56 (16x16):  triangulo pequeno (dicas de tela, ex. 'Field')
  09317d14 (128x64): 'START ?' da ajuda no canto do duelo
  347976f6 (64x64):  folha pequena da tela de Help (Y/B/A, Start, LB/RB, analogico)
  92db36a5 (256x128): HUD do campo (menu 'R +' com os 4 botoes, aba R -> RB, botao quadrado do PDA); base = date_and_time.png do pacote
Mapeamento (igual ao controle no runtime, display.cpp): A = xis, B = circulo, X = quadrado, Y = triangulo, Start = START,
View = SELECT, LB/RB = L/R. O direcional fica como esta.
Uso: python tools/mod_xbox_buttons.py
"""
import os, sys
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
MOD = os.path.join(ROOT, 'gen', 'mods', 'XboxButtons')
PACK = os.path.join(ROOT, 'gen', 'mods', 'PPSSPP_MikeyMK', 'textures', 'extras', '00000000b2d8182eb505bc0f.png')
DUMP = os.path.join(ROOT, 'gen', 'dump_tex', 'faltam', 'b505bc0f.png')


def src(name):
    return Image.open(os.path.join(MOD, name)).convert('RGBA')


def trim(im):
    bb = im.getchannel('A').point(lambda a: 255 if a > 8 else 0).getbbox()
    return im.crop(bb) if bb else im


def fit(im, w, h):
    """redimensiona mantendo a proporcao para caber em w x h (Lanczos)"""
    im = trim(im); s = min(w / im.width, h / im.height)
    return im.resize((max(1, round(im.width * s)), max(1, round(im.height * s))), Image.LANCZOS)


def paste_center(dst, im, box):
    x0, y0, x1, y1 = box
    dst.alpha_composite(im, (x0 + (x1 - x0 - im.width) // 2, y0 + (y1 - y0 - im.height) // 2))


def clear(dst, box):
    dst.paste((0, 0, 0, 0), box)


def font(size):
    for f in ('segoeuib.ttf', 'arialbd.ttf', 'seguisb.ttf'):
        try:
            return ImageFont.truetype(f, size)
        except OSError:
            pass
    return ImageFont.load_default()


def make_lb(rb):
    """LB a partir da imagem do RB: apaga o texto e escreve 'LB' no mesmo estilo"""
    im = rb.copy(); px = im.load(); w, h = im.size
    face = px[w // 6, h // 2]   # cor do corpo do botao, longe do texto
    d = ImageDraw.Draw(im)
    tb = (int(w * 0.28), int(h * 0.18), int(w * 0.72), int(h * 0.82))
    for y in range(tb[1], tb[3]):
        for x in range(tb[0], tb[2]):
            if px[x, y][3] > 0: px[x, y] = face
    f = font(int(h * 0.42)); t = 'LB'; bb = d.textbbox((0, 0), t, font=f)
    d.text(((w - (bb[2] - bb[0])) / 2 - bb[0], (h - (bb[3] - bb[1])) / 2 - bb[1]), t, font=f, fill=(255, 255, 255, 255))
    return im


def make_view(start):
    """botao View (o SELECT do PSP) a partir do botao Start: troca as 3 linhas por dois retangulos sobrepostos"""
    im = start.copy(); px = im.load(); w, h = im.size
    # as linhas do Start sao os pixels claros e pouco saturados no meio do topo do botao
    ys = [y for y in range(h // 6, h * 2 // 3) for x in range(w // 4, w * 3 // 4, 4) if sum(px[x, y][:3]) > 450 and px[x, y][3] > 200]
    xs = [x for y in range(h // 6, h * 2 // 3, 4) for x in range(w // 4, w * 3 // 4) if sum(px[x, y][:3]) > 450 and px[x, y][3] > 200]
    if not ys or not xs: return im
    x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
    face = px[(x0 + x1) // 2, max(0, y0 - (y1 - y0) // 3)]
    pad = (y1 - y0) // 4
    ImageDraw.Draw(im).rectangle((x0 - pad, y0 - pad, x1 + pad, y1 + pad), fill=face)
    d = ImageDraw.Draw(im); cw, ch = (x1 - x0) * 0.62, (y1 - y0) * 0.78; lw = max(2, int((y1 - y0) * 0.11))
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2; off = (x1 - x0) * 0.17
    back = (cx - cw / 2 + off, cy - ch / 2 - off * 0.6, cx + cw / 2 + off, cy + ch / 2 - off * 0.6)
    front = (cx - cw / 2 - off, cy - ch / 2 + off * 0.6, cx + cw / 2 - off, cy + ch / 2 + off * 0.6)
    col = (225, 225, 225, 255)
    d.rectangle(back, outline=col, width=lw)
    d.rectangle(front, fill=face, outline=col, width=lw)
    return im


def hud(tex, rb):
    """92db36a5 (256x128 nativa; 1024x512 no pacote): folha do HUD do campo ('date_and_time'): relogio, calendario, os 4 botoes de acao do
    menu 'R +' (Duel/Item/Map/Talk), a aba 'R' e o botao quadrado do PDA. So existe no pacote; sem ele, nada a fazer"""
    pk = os.path.join(ROOT, 'gen', 'mods', 'PPSSPP_MikeyMK', 'textures', 'ui', 'menu', 'date_and_time.png')
    if not os.path.exists(pk):
        print('aviso: sem o pacote (ui/menu/date_and_time.png); HUD do campo nao trocado'); return
    sh = Image.open(pk).convert('RGBA')
    assert sh.size == (1024, 512), sh.size
    face = {'tri': (233, 127, 297, 182), 'sq': (167, 171, 233, 233), 'ci': (289, 171, 356, 233), 'x': (229, 217, 298, 278)}
    btn = {'tri': 'Xbox Y Btn.png', 'sq': 'Xbox X Btn.png', 'ci': 'Xbox B Btn.png', 'x': 'Xbox A Btn.png'}
    for b in face.values(): clear(sh, b)
    for k, b in face.items(): paste_center(sh, fit(src(btn[k]), b[2] - b[0] - 2, b[3] - b[1] - 2), b)
    tab = (354, 126, 449, 172)   # aba 'R' (o R do PSP vira o RB)
    clear(sh, tab); paste_center(sh, fit(rb, tab[2] - tab[0] - 4, tab[3] - tab[1] - 4), tab)
    # botao quadrado do PDA (canto do telefone): cobre o disco original sem apagar o telefone atras dele
    paste_center(sh, fit(src('Xbox X Btn.png'), 60, 60), (950, 421, 1010, 482))
    sh.save(os.path.join(tex, '92db36a5.png'))


def help64(tex, start, rb):
    """347976f6 (64x64): folha pequena de botoes da tela de Help (triangulo/circulo/xis, START, L/R, direcional, '?', bolinha do analogico).
    Nao existe no pacote: a base e o despejo original ampliado 4x e as caixas abaixo (coordenadas nativas) recebem os icones do Xbox"""
    orig = os.path.join(ROOT, 'gen', 'dump_tex', 'faltam', '347976f6.png')
    if not os.path.exists(orig):
        print('aviso: sem gen/dump_tex/faltam/347976f6.png (despeje com TF_TEX_DUMP); botoes da tela de Help nao trocados'); return
    sh = Image.open(orig).convert('RGBA'); assert sh.size == (64, 64), sh.size
    sh = sh.resize((256, 256), Image.LANCZOS)
    boxes = {'tri': ((13, 18, 27, 32), 'Xbox Y Btn.png'), 'ci': ((28, 18, 42, 32), 'Xbox B Btn.png'), 'x': ((43, 18, 57, 32), 'Xbox A Btn.png'),
             'stick': ((46, 35, 63, 52), 'LeftStick.png')}
    for b, name in boxes.values():
        x0, y0, x1, y1 = [v * 4 for v in b]; clear(sh, (max(0, x0 - 4), max(0, y0 - 4), x1 + 4, y1 + 4))
        paste_center(sh, fit(src(name), x1 - x0 - 2, y1 - y0 - 2), (x0, y0, x1, y1))
    for b, im in (((0, 35, 28, 46), start), ((0, 46, 23, 57), make_lb(rb)), ((23, 46, 46, 57), rb)):
        x0, y0, x1, y1 = [v * 4 for v in b]; clear(sh, (max(0, x0 - 4), max(0, y0 - 4), x1 + 4, y1 + 4))
        paste_center(sh, fit(im, x1 - x0 - 4, y1 - y0 - 4), (x0, y0, x1, y1))
    sh.save(os.path.join(tex, '347976f6.png'))


def main():
    os.makedirs(os.path.join(MOD, 'textures'), exist_ok=True)
    if os.path.exists(PACK):
        sheet = Image.open(PACK).convert('RGBA')
    elif os.path.exists(DUMP):
        sheet = Image.open(DUMP).convert('RGBA').resize((512, 256), Image.LANCZOS)
    else:
        sys.exit('sem a folha de botoes base (pacote PPSSPP_MikeyMK ou despejo TF_TEX_DUMP em gen/dump_tex)')
    assert sheet.size == (512, 256), sheet.size
    # caixas na folha 512x256 (4x a original 128x64): a do icone visivel, para os botoes redondos; celulas de 11 linhas nativas para
    # START/SELECT/L/R (x 94..120 nativo)
    face = {'tri': (65, 0, 127, 54), 'sq': (0, 45, 63, 100), 'ci': (125, 48, 186, 102), 'x': (65, 93, 127, 148)}
    btn = {'tri': 'Xbox Y Btn.png', 'sq': 'Xbox X Btn.png', 'ci': 'Xbox B Btn.png', 'x': 'Xbox A Btn.png'}
    for k, b in face.items():
        clear(sheet, b)
        paste_center(sheet, fit(src(btn[k]), b[2] - b[0] - 2, b[3] - b[1] - 2), b)
    cells = {'start': (376, 0, 480, 44), 'select': (376, 44, 480, 88), 'l': (376, 88, 480, 132), 'r': (376, 132, 480, 176)}
    for b in cells.values(): clear(sheet, b)
    start = src('Xbox Start Btn.png'); rb = src('RB Normal.png')
    m = 3   # margem (px HD) dentro da celula: a amostragem do jogo pode encostar na borda
    for k, im in (('start', start), ('select', make_view(start)), ('l', make_lb(rb)), ('r', rb)):
        b = cells[k]; paste_center(sheet, fit(im, b[2] - b[0] - 2 * m, b[3] - b[1] - 2 * m), b)
    tex = os.path.join(MOD, 'textures')
    sheet.save(os.path.join(tex, 'b505bc0f.png'))
    small = Image.new('RGBA', (64, 64), (0, 0, 0, 0)); paste_center(small, fit(src('Xbox Y.png'), 60, 60), (0, 0, 64, 64))
    small.save(os.path.join(tex, '6d527b56.png'))
    # 09317d14 (128x64): 'START ?' do canto do duelo (o sprite usa x 0..43, y ~51..62 nativos; 'Help' e texto da fonte). O pacote deixou
    # essa area preta; aqui vira o botao Start + o '?' amarelo da textura original
    pk = os.path.join(ROOT, 'gen', 'mods', 'PPSSPP_MikeyMK', 'textures', 'ui', 'start_help_text.png')
    orig = os.path.join(ROOT, 'gen', 'dump_tex', 'faltam', '09317d14.png')
    if os.path.exists(orig):
        o = Image.open(orig).convert('RGBA')
        sh = Image.open(pk).convert('RGBA') if os.path.exists(pk) else o.resize((512, 256), Image.LANCZOS)
        assert sh.size == (512, 256), sh.size
        clear(sh, (0, 200, 172, 256))
        paste_center(sh, fit(start, 46, 46), (78, 206, 126, 254))   # encostado no '?' (o START original era uma pilula larga)
        q = o.crop((32, 52, 43, 63)).resize((44, 44), Image.LANCZOS); sh.alpha_composite(q, (126, 207))
        sh.save(os.path.join(tex, '09317d14.png'))
    else:
        print('aviso: sem gen/dump_tex/faltam/09317d14.png (despeje com TF_TEX_DUMP); START do canto do duelo nao trocado')
    hud(tex, rb)
    help64(tex, start, rb)
    with open(os.path.join(MOD, 'mod.txt'), 'w', encoding='utf-8') as f:
        f.write('name=Xbox Buttons\nauthor=Tag Force Recomp\nversion=1.0\n'
                'description=Replaces the PSP button icons with Xbox controller ones (A=cross, B=circle, X=square, Y=triangle, Start, View, LB/RB)\n')
    print('ok:', tex)


if __name__ == '__main__':
    main()
