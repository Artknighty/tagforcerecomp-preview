"""Treina a FUNCAO DE VALOR (probabilidade de o jogador do ponto de vista vencer) com o conjunto de tools/ia_valor/dataset.py.
MLP pequena (35 -> 64 -> 32 -> 1, tanh) para ser reimplementada em C++ no plugin. Avalia em DUELOS separados (nao em amostras soltas): acuracia, AUC e comparacao com linhas de base
(so a diferenca de LP; so a diferenca de LP + poder do campo). Salva gen/ia_valor/valor_mlp.json (pesos) e imprime o resumo.
Uso: python tools/ia_valor/treinar.py [valor.npz=gen/ia_valor/valor.npz]"""
import sys, os, json
import numpy as np
import torch
RAIZ = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
arq = sys.argv[1] if len(sys.argv) > 1 else os.path.join(RAIZ, 'gen', 'ia_valor', 'valor.npz')
D = np.load(arq); X, y, duelo, turno = D['X'], D['y'].astype(np.float32), D['duelo'], D['turno']
ud = np.unique(duelo); rng = np.random.RandomState(1); rng.shuffle(ud)
nval = max(10, len(ud) // 5); val_d = set(ud[:nval].tolist())
mv = np.array([d in val_d for d in duelo]); Xt, yt, Xv, yv = X[~mv], y[~mv], X[mv], y[mv]
print('treino %d amostras (%d duelos) | validacao %d amostras (%d duelos)' % (len(yt), len(ud) - nval, len(yv), nval))


def auc(p, t):
    o = np.argsort(p); r = np.empty(len(p)); r[o] = np.arange(1, len(p) + 1)
    pos = t == 1; n1, n0 = pos.sum(), (~pos).sum()
    return float((r[pos].sum() - n1 * (n1 + 1) / 2) / (n1 * n0))


def acc(p, t): return float(((p > 0.5) == (t == 1)).mean())


# linhas de base
IDX_LP = 34          # (lp_me - lp_op)/8000 (ultimo feature global)
IDX_PODER_ME, IDX_PODER_OP = 13, 19 + 13
base1 = Xv[:, IDX_LP]
base2 = Xv[:, IDX_LP] * 3 + (Xv[:, IDX_PODER_ME] - Xv[:, IDX_PODER_OP]) * 3
print('linha de base LP:         acc %.3f  AUC %.3f' % (acc(1 / (1 + np.exp(-base1 * 4)), yv), auc(base1, yv)))
print('linha de base LP + poder: acc %.3f  AUC %.3f' % (acc(1 / (1 + np.exp(-base2)), yv), auc(base2, yv)))

torch.manual_seed(0)
net = torch.nn.Sequential(torch.nn.Linear(X.shape[1], 64), torch.nn.Tanh(), torch.nn.Linear(64, 32), torch.nn.Tanh(), torch.nn.Linear(32, 1))
opt = torch.optim.Adam(net.parameters(), lr=3e-3, weight_decay=1e-4)
xt, ytt, xv = torch.tensor(Xt), torch.tensor(yt), torch.tensor(Xv)
melhor = (0, None)
for ep in range(1, 301):
    perm = torch.randperm(len(xt))
    for i in range(0, len(xt), 256):
        idx = perm[i:i + 256]
        loss = torch.nn.functional.binary_cross_entropy_with_logits(net(xt[idx]).squeeze(1), ytt[idx])
        opt.zero_grad(); loss.backward(); opt.step()
    if ep % 10 == 0:
        with torch.no_grad(): pv = torch.sigmoid(net(xv).squeeze(1)).numpy()
        a = auc(pv, yv)
        if a > melhor[0]: melhor = (a, {k: v.clone() for k, v in net.state_dict().items()}, ep)
        if ep % 50 == 0: print('epoca %3d  AUC val %.3f  acc %.3f  (melhor %.3f na epoca %d)' % (ep, a, acc(pv, yv), melhor[0], melhor[2]))
net.load_state_dict(melhor[1])
with torch.no_grad(): pv = torch.sigmoid(net(xv).squeeze(1)).numpy()
print('MLP (melhor): acc %.3f  AUC %.3f' % (acc(pv, yv), auc(pv, yv)))
for lo, hi in ((2, 5), (6, 10), (11, 20), (21, 60)):
    m = (turno[mv] >= lo) & (turno[mv] <= hi)
    if m.sum() > 50: print('   turnos %2d-%2d: acc %.3f AUC %.3f (%d amostras)' % (lo, hi, acc(pv[m], yv[m]), auc(pv[m], yv[m]), m.sum()))
pesos = {'camadas': [[net[k].weight.detach().numpy().tolist(), net[k].bias.detach().numpy().tolist()] for k in (0, 2, 4)], 'nf': int(X.shape[1])}
json.dump(pesos, open(os.path.join(RAIZ, 'gen', 'ia_valor', 'valor_mlp.json'), 'w'))
print('pesos salvos em gen/ia_valor/valor_mlp.json')
