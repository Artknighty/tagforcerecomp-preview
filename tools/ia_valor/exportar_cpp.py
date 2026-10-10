"""Exporta os pesos da funcao de valor (gen/ia_valor/valor_mlp.json) para mods_src/IAAprimorada/valor_pesos.h (arrays float): o plugin calcula a mesma MLP em C++.
Uso: python tools/ia_valor/exportar_cpp.py [json=gen/ia_valor/valor_mlp.json] [saida=mods_src/IAAprimorada/valor_pesos.h]"""
import sys, os, json
RAIZ = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
arq = sys.argv[1] if len(sys.argv) > 1 else os.path.join(RAIZ, 'gen', 'ia_valor', 'valor_mlp.json')
saida = sys.argv[2] if len(sys.argv) > 2 else os.path.join(RAIZ, 'mods_src', 'IAAprimorada', 'valor_pesos.h')
P = json.load(open(arq))
(w1, b1), (w2, b2), (w3, b3) = P['camadas']
n_in, n1, n2 = len(w1[0]), len(w1), len(w2)
assert len(w3) == 1 and len(w3[0]) == n2


def arr(nome, v):
    plano = [x for r in v for x in r] if isinstance(v[0], list) else v
    return 'static const float %s[%d] = {%s};\n' % (nome, len(plano), ', '.join('%.8ff' % x for x in plano))


with open(saida, 'w', newline='\n') as f:
    f.write('/* GERADO por tools/ia_valor/exportar_cpp.py: pesos da funcao de valor (MLP %d -> %d -> %d -> 1, tanh). Nao editar. */\n#pragma once\n' % (n_in, n1, n2))
    f.write('#define VALOR_NIN %d\n#define VALOR_N1 %d\n#define VALOR_N2 %d\n' % (n_in, n1, n2))
    f.write(arr('VALOR_W1', w1)); f.write(arr('VALOR_B1', b1)); f.write(arr('VALOR_W2', w2)); f.write(arr('VALOR_B2', b2)); f.write(arr('VALOR_W3', w3[0])); f.write(arr('VALOR_B3', b3))
print('escrito', saida, '(entradas %d, camadas %d e %d)' % (n_in, n1, n2))
