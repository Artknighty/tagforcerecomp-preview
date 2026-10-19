"""Gera gen/app.ico (icone do .exe, janela e barra de tarefas) a partir do ICON0.PNG da PROPRIA ISO do usuario (o Ojama Yellow do
icone do jogo). O resultado fica em gen/ (fora do git: e arte do jogo) e o build.sh o embute se existir; sem ele o .exe fica sem icone.
Uso: python tools/game/make_icon.py [caminho da ISO]   (padrao: a ISO gravada em gen/tagforce_recomp.ini, chave iso=)"""
import io, os, re, sys
sys.path.insert(0, os.path.dirname(__file__))
import isofs
from PIL import Image

raiz = os.path.join(os.path.dirname(__file__), '..', '..')
iso = sys.argv[1] if len(sys.argv) > 1 else None
if not iso and not os.path.isfile(os.path.join(raiz, 'gen', 'jogo', 'PSP_GAME', 'ICON0.PNG')):
    ini = os.path.join(raiz, 'gen', 'tagforce_recomp.ini')
    for l in open(ini, encoding='utf-8', errors='replace'):
        m = re.match(r'^iso\s*=\s*(.+?)\s*$', l)
        if m: iso = m.group(1)
jogo = os.path.join(raiz, 'gen', 'jogo', 'PSP_GAME', 'ICON0.PNG')   # dados instalados (preferido: nao precisa da ISO)
if os.path.isfile(jogo): png = open(jogo, 'rb').read()
else:
    cls = [v for v in vars(isofs).values() if isinstance(v, type)][0]
    png = cls(iso).ler('PSP_GAME/ICON0.PNG')
im = Image.open(io.BytesIO(png)).convert('RGBA')               # 144x80: logo + Ojama Yellow a esquerda
sq = im.crop((0, 2, 62, 64)).resize((256, 256), Image.LANCZOS)    # recorte quadrado do Ojama
out = os.path.join(raiz, 'gen', 'app.ico')
sq.save(out, format='ICO', sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
print('icone gravado em', os.path.abspath(out))
