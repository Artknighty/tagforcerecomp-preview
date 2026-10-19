"""Gera o MOD de texturas "CartasEU" (gen/mods/CartasEU) com as variantes de hash das artes das cartas que um pacote de
texturas no formato do PPSSPP (ex.: MikeyMK) nao cobre. O pacote NUNCA e alterado: o mod so aponta para os PNGs dele. Uso:
  python tools/mods/cartas_variantes.py [pasta_textures_do_pacote] [pasta_do_jogo] [idioma] [pasta_do_mod]
  padrao: gen/mods/PPSSPP_MikeyMK/textures  gen/jogo  e  gen/mods/CartasEU   (idioma: e f g i s = cardh_<idioma>.cip / cardm_<idioma>.cip)
  --teste: so mostra o que faria, sem gravar

Por que faltam: a arte grande (128x64 DXT1, chave ...0607) fica num arquivo GIM de 0x1000 bytes cuja imagem comeca no
byte 0x80, entao os ultimos 0x80 bytes que o jogo (e o PPSSPP) usam no hash sao o que vier DEPOIS na memoria: o cabecalho
da carta da posicao seguinte ou zeros (posicao vazia). O pacote so registrou algumas dessas variantes; e algumas artes da
versao europeia diferem das que o autor do pacote viu. Este script le os arquivos do jogo (card/cardh_*.cip e
card/cardm_*.cip), calcula os hashes de cada arte com as caudas possiveis e associa ao PNG certo:
  1) se alguma variante da arte ja esta no ini do pacote -> o mesmo PNG;
  2) senao, compara a imagem (DXT1 decodificada) so com os PNGs daquela carta (cards/large/<id>*.png), com margem.
O mod gerado tem mod.txt, textures/textures.ini (entradas que apontam para o pacote por ../../<mod do pacote>/textures/...,
entao o mod precisa do pacote instalado ao lado) e textures/small/geradas/ (miniaturas das artes alternativas montadas aqui).
Nenhum dado do jogo vai para o repositorio: tudo e calculado a partir da copia do usuario.
"""
import os, re, struct, sys
import numpy as np
from PIL import Image
import xxhash

# associacoes conferidas a olho (quando a comparacao por imagem fica em duvida): (pasta, id, arte) -> PNG
MANUAL = {
    ('large', 4007, 0): '4007.png',   # Blue-Eyes White Dragon, arte classica (confirmed visually 2026-10-02)
}



def h32(b):
    return '%08x' % (xxhash.xxh64(b, seed=0xBACD7814).intdigest() & 0xffffffff)


def cip_arts(d):
    """(id, k, offset) de cada arte: indice por id em 0x30 (8 bytes por id, unidades de 0x800); bit 31 = lista de alternativas"""
    lo, hi = struct.unpack_from('<II', d, 0x14)
    for cid in range(lo, hi + 1):
        v, = struct.unpack_from('<I', d, 0x30 + (cid - lo) * 8)
        if v == 0 and cid != lo:
            continue
        if not v & 0x80000000:
            yield cid, 0, v * 0x800
            continue
        p, k = v & 0x7fffffff, 0
        while True:
            w, = struct.unpack_from('<I', d, p)
            yield cid, k, (w & 0x7fffffff) * 0x800
            k += 1; p += 8
            if w & 0x80000000:
                break


def dxt1(b, w, h):
    """DXT1 do PSP (por bloco: 4 bytes de indices, cor0, cor1) -> RGB w x h"""
    out = np.zeros((h, w, 3), np.uint8)
    bw = w // 4
    for by in range(h // 4):
        for bx in range(bw):
            o = (by * bw + bx) * 8
            if o + 8 > len(b):
                continue
            idx, c0, c1 = struct.unpack_from('<IHH', b, o)
            def rgb(c): return np.array([((c >> 11) & 31) * 255 // 31, ((c >> 5) & 63) * 255 // 63, (c & 31) * 255 // 31], np.int32)
            p0, p1 = rgb(c0), rgb(c1)
            pal = [p0, p1, (2 * p0 + p1) // 3, (p0 + 2 * p1) // 3] if c0 > c1 else [p0, p1, (p0 + p1) // 2, np.zeros(3, np.int32)]
            for i in range(16):
                out[by * 4 + i // 4, bx * 4 + i % 4] = pal[(idx >> (2 * i)) & 3]
    return out


def sig(img, rows):
    a = np.asarray(Image.fromarray(img) if isinstance(img, np.ndarray) else img.convert('RGB'))
    return np.asarray(Image.fromarray(a).resize((32, 16), Image.BOX)).astype(np.float32)[:rows]


def arte_completa(tex):
    """textura da arte grande (128x64 * s) -> arte 78x78 * s. O jogo desenha a arte em 3 pedacos (GE_PRIM_FRAME na lista da
    loja): (0,0)-(78,60) -> topo; (80,0)-(128,18) -> base esquerda; (80,20)-(110,38) -> base direita"""
    s = tex.shape[1] // 128
    a = np.zeros((78 * s, 78 * s, tex.shape[2]), tex.dtype)
    a[:60 * s, :78 * s] = tex[:60 * s, :78 * s]
    a[60 * s:78 * s, :48 * s] = tex[0:18 * s, 80 * s:128 * s]
    a[60 * s:78 * s, 48 * s:78 * s] = tex[20 * s:38 * s, 80 * s:110 * s]
    return a


JANELA = (7, 5, 250, 187)   # janela da arte nas miniaturas HD do pacote (256x256), achada por ajuste em 5 cartas (erro medio ~10)


def montar_miniatura(pack, out, cid, large_png, dest):
    base = Image.open(os.path.join(pack, 'cards', 'small', '%d.png' % cid)).convert('RGBA')
    sx = base.size[0] / 256.0; x0, y0, x1, y1 = (int(round(v * sx)) for v in JANELA)
    art = arte_completa(np.asarray(Image.open(os.path.join(pack, large_png)).convert('RGBA')))
    base.paste(Image.fromarray(art).resize((x1 - x0, y1 - y0), Image.LANCZOS), (x0, y0))
    os.makedirs(os.path.dirname(os.path.join(out, dest)), exist_ok=True)
    base.save(os.path.join(out, dest))


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    teste = '--teste' in sys.argv
    pack = args[0] if len(args) > 0 else 'gen/mods/PPSSPP_MikeyMK/textures'
    jogo = args[1] if len(args) > 1 else 'gen/jogo'
    lang = args[2] if len(args) > 2 else 'e'
    out = os.path.join(args[3] if len(args) > 3 else 'gen/mods/CartasEU', 'textures')
    pkmod = os.path.basename(os.path.dirname(os.path.abspath(pack)))   # pasta do mod do pacote (ex.: PPSSPP_MikeyMK)
    txt = open(os.path.join(pack, 'textures.ini'), encoding='utf-8', errors='replace').read()
    ini = {}
    for l in txt.splitlines():
        m = re.match(r'([0-9a-f]{8})([0-9a-f]{8})([0-9a-f]{8})\s*=\s*(\S+)', l.strip())
        if m:
            ini.setdefault((m.group(2), m.group(3)), m.group(4))
    card = os.path.join(jogo, 'PSP_GAME', 'USRDIR', 'card')
    novos = {}; stats = {}; grandes = {}   # grandes: (id, arte) -> PNG da arte grande (para montar as miniaturas)
    # (arquivo, chave de dimensao, tamanho da arte no arquivo, deslocamento, w, h, pasta do pacote)
    for nome, dim, off0, w, h, pasta in (('cardh_%s.cip' % lang, '00000607', 0x80, 128, 64, 'large'), ('cardm_%s.cip' % lang, '00000606', 0, 64, 64, 'small')):
        d = open(os.path.join(card, nome), 'rb').read()
        n = w * h // 2
        hdr = d[d.find(b'MIG.00.1PSP'):][:0x80] if off0 else b''
        files = sorted(f for f in os.listdir(os.path.join(pack, 'cards', pasta)) if f.endswith('.png'))
        for cid, k, off in cip_arts(d):
            o = off + off0
            if off0:   # arte grande: os ultimos 0x80 bytes dependem da memoria vizinha
                art = d[o:o + n - 0x80]
                variants = [art + bytes(0x80), art + hdr, d[o:o + n]]
            else:
                variants = [d[o:o + n]]
            hs = [h32(v) for v in variants]
            png = next((ini[(dim, x)] for x in hs if (dim, x) in ini), None)
            how = 'hash'
            if png is None and (pasta, cid, k) in MANUAL:
                png = 'cards/%s/%s' % (pasta, MANUAL[(pasta, cid, k)]); how = 'manual'
            if png is None and pasta == 'small' and (cid, k) in grandes and os.path.exists(os.path.join(pack, 'cards', 'small', '%d.png' % cid)):
                # miniatura (carta inteira 64x64): o pacote so tem a da arte padrao. Mesma arte da padrao -> ela; arte
                # alternativa -> monta uma: moldura da miniatura padrao + arte HD da alternativa (cards/large) na janela
                lp = grandes[(cid, k)]
                if lp == 'cards/large/%d.png' % cid:
                    png = 'cards/small/%d.png' % cid; how = 'base'
                else:
                    png = 'small/geradas/' + os.path.basename(lp); how = 'montada'
                    if not teste:
                        montar_miniatura(pack, out, cid, lp, png)
                    print('  small %d/%d -> %s (moldura %d.png + arte %s)' % (cid, k, png, cid, lp))
            if png is None:   # compara a imagem so com os PNGs desta carta
                cands = [f for f in files if re.fullmatch(r'%d[a-z]?\.png' % cid, f)]
                if cands:
                    rows = 15 if off0 else 16
                    s = sig(dxt1(variants[0], w, h), rows)
                    ds = sorted((float(((sig(Image.open(os.path.join(pack, 'cards', pasta, f)), rows) - s) ** 2).mean()), f) for f in cands)
                    # varias artes da carta: a melhor precisa se destacar; uma so (o pacote pode nao ter a alternativa): bem parecida
                    if (len(ds) > 1 and ds[0][0] < 6000 and ds[0][0] < 0.6 * ds[1][0]) or (len(ds) == 1 and ds[0][0] < 2500):
                        png = 'cards/%s/%s' % (pasta, ds[0][1]); how = 'imagem'
                    else:
                        print('  %s %d/%d: sem certeza (%s)' % (pasta, cid, k, ', '.join('%s %.0f' % (f, v) for v, f in ds[:3])))
            if png is None:
                stats[pasta + ' sem PNG'] = stats.get(pasta + ' sem PNG', 0) + 1
                continue
            stats['%s por %s' % (pasta, how)] = stats.get('%s por %s' % (pasta, how), 0) + 1
            if pasta == 'large':
                grandes[(cid, k)] = png
            for x in hs:
                if (dim, x) not in ini and (dim, x) not in novos:
                    novos[(dim, x)] = png
            if how == 'imagem':
                print('  %s %d/%d -> %s' % (pasta, cid, k, png))
    print(stats)
    print('%d entradas novas' % len(novos))
    if teste or not novos:
        return
    def alvo(p):   # PNG do pacote -> caminho relativo a partir da pasta textures do mod; as montadas ficam no proprio mod
        return p if p.startswith('small/geradas/') else '../../%s/textures/%s' % (pkmod, p)
    os.makedirs(out, exist_ok=True)
    linhas = ['[options]', 'version = 1', 'hash = xxh64', 'ignoreAddress = true', '[hashes]'] + ['00000000%s%s = %s' % (dim, x, alvo(p)) for (dim, x), p in sorted(novos.items())]
    open(os.path.join(out, 'textures.ini'), 'w', encoding='utf-8', newline='\n').write('\n'.join(linhas) + '\n')
    modtxt = os.path.join(os.path.dirname(out), 'mod.txt')
    if not os.path.exists(modtxt):
        open(modtxt, 'w', encoding='utf-8', newline='\n').write(
            'name=EU Cards (hash variants)\nauthor=Tag Force Recomp\nversion=1.0\n'
            'description=Links the art of the European card versions to the texture pack (needs the MikeyMK pack next to it); generated by tools/mods/cartas_variantes.py\n')
    print('gravado em', out)

if __name__ == '__main__':
    main()
