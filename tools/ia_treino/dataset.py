"""Monta o conjunto de treino a partir dos duelos da arena (gen/replay/*_arena_<rotulo>_*/ia_decisoes.jsonl + log.txt).
Rotulos (resumo):
  - professor (regras de combate bem conhecidas lidas do texto das opcoes: ataque letal/seguro/suicida, invocar em ataque diante de um atacante maior)
  - resultado: decisao tomada por quem VENCEU o duelo conta como boa; desvio aleatorio (exploracao) de quem perdeu vira "ficar com a regra"
Uso: python tools/ia_treino/dataset.py <rotulo> [saida=gen/ia_treino/dados.jsonl]"""
import sys, os, re, json, glob, random
sys.path.insert(0, os.path.dirname(__file__))
from prompt import montar_prompt

def vencedor(pasta):
    try:
        for l in open(os.path.join(pasta, 'log.txt'), errors='ignore'):
            m = re.search(r'FIM quadro.*venceu o jogador (\d)', l)
            if m: return int(m.group(1))
    except OSError: pass
    return None

def _linha(sit, ini):
    for l in sit.split(chr(10)):
        if l.startswith(ini): return l
    return ''
def _contar(l):   # monstros listados numa linha "Your monsters: ..." (virados para baixo contam como 1 cada)
    return l.count('ATK ') + sum(int(x) for x in re.findall(r'(\d+) face-down monster', l))
def _lp(sit):
    m = re.search(r'You have (\d+) LP and the opponent has (\d+) LP', sit); return (int(m.group(1)), int(m.group(2))) if m else (8000, 8000)

def professor(d):
    """devolve o indice que um bom jogador escolheria, ou None se nao ha certeza (regras de Yu-Gi-Oh! lidas do texto das opcoes)"""
    t, ops, sit = d['tipo'], d['opcoes'], d['situacao']
    if t.startswith('attack with this monster'):
        o = ops[0]; meus = _contar(_linha(sit, 'Your monsters:')); deles = _contar(_linha(sit, "Opponent's monsters:")); lp_me, lp_op = _lp(sit)
        if 'LETHAL' in o: return 0
        if 'yours is destroyed and you lose' in o or 'nothing is destroyed and you lose' in o: return 1
        if 'yours survives' in o: return 0                                   # destroi o alvo e o atacante sobrevive: sempre vale o risco
        if 'the opponent (' in o: return 0                                   # ataque direto: dano de graca
        return None   # troca igual, alvo virado, sem ganho: duvidoso -> o RESULTADO dos duelos decide (agressividade das regras ganhou da passividade)
    if t.startswith('summon this monster in attack'):
        o = ops[0]
        if 'would be destroyed and you would lose' in o: return 1
        if 'safe from battle' in o: return 0
        if 'could not attack it profitably' in o and 'could attack it: it would be destroyed' not in o: return 0
        return None
    return None

def main():
    rot = sys.argv[1]; saida = sys.argv[2] if len(sys.argv) > 2 else 'gen/ia_treino/dados.jsonl'
    os.makedirs(os.path.dirname(saida), exist_ok=True)
    n = {'duelos': 0, 'sem_fim': 0, 'professor': 0, 'vencedor': 0, 'regra_perdedor': 0, 'desvio_perdedor': 0, 'ignorado': 0}
    vistos = set(); out = []
    for pasta in sorted(glob.glob('gen/replay/*_arena_%s_*' % rot)):
        f = os.path.join(pasta, 'ia_decisoes.jsonl')
        if not os.path.exists(f): continue
        w = vencedor(pasta); n['duelos'] += 1
        if w is None: n['sem_fim'] += 1
        for l in open(f, encoding='utf-8', errors='ignore'):
            try: d = json.loads(l)
            except ValueError: continue
            if len(d['opcoes']) < 2 or len(d['opcoes']) > 10: continue
            lab = professor(d); src = 'professor'
            if lab is None and w is not None:
                base = d.get('base', d['regra'])                      # escolha antes da exploracao (modelo ou regra)
                if d['origem'] == 'explorar':
                    if d['jog'] == w: lab, src = d['escolha'], 'vencedor'                 # desvio de quem venceu: bom
                    else: lab, src = base, 'desvio_perdedor'                              # desvio de quem perdeu: fica com a base
                elif d['origem'] == 'regra': lab, src = d['escolha'], 'vencedor' if d['jog'] == w else 'regra_perdedor'   # imita a regra (ancora)
                elif d['jog'] == w: lab, src = d['escolha'], 'vencedor'                   # escolha do modelo de quem venceu: reforca
            if lab is None: n['ignorado'] += 1; continue
            p = montar_prompt(d['tipo'], d['situacao'], d['opcoes'])
            if (p, lab) in vistos: continue
            vistos.add((p, lab)); n[src] += 1
            out.append({'prompt': p, 'answer': str(lab), 'fonte': src, 'tipo': d['tipo'].split(' ')[0] + ' ' + d['tipo'].split(' ')[1] if ' ' in d['tipo'] else d['tipo'], 'n': len(d['opcoes'])})
    random.Random(1).shuffle(out)
    with open(saida, 'w', encoding='utf-8') as fo:
        for r in out: fo.write(json.dumps(r, ensure_ascii=False) + '\n')
    print(n, '->', saida, len(out), 'exemplos')
if __name__ == "__main__":
    main()
