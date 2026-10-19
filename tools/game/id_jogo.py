"""Identifica a versao do jogo de uma ISO (ou de uma pasta instalada) pelo UMD_DATA.BIN. Imprime o identificador usado pelos scripts (TF_VER):
vazio para a versao europeia ULES-00600 (o padrao do projeto: gen/, gen/jogo, gen/run.exe) ou, por ex., ULUS10136 para a americana
(e as ISOs derivadas dela, como traducoes). Uso: python tools/game/id_jogo.py <iso ou pasta>"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import isofs

def id_do_jogo(caminho):
    if os.path.isdir(caminho):
        dados = open(os.path.join(caminho, 'UMD_DATA.BIN'), 'rb').read(32)
    else:
        dados = isofs.Iso(caminho).ler('UMD_DATA.BIN')[:32]
    return dados.split(b'|')[0].decode('latin1').strip()   # ex. 'ULUS-10136'

def ver(caminho):
    i = id_do_jogo(caminho).replace('-', '')
    return '' if i == 'ULES00600' else i

if __name__ == '__main__':
    print(ver(sys.argv[1]))
