"""Tells the game version of an ISO (or of an installed folder) from its UMD_DATA.BIN. Prints the identifier the scripts use (TF_VER):
empty for the European version ULES-00600 (the project default: gen/, gen/jogo) or, e.g., ULUS10136 for the American one
(and the ISOs derived from it, such as fan translations). Usage: python tools/game/id_jogo.py <iso or folder>"""
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
