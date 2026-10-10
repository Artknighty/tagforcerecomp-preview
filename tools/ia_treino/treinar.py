"""Ajuste fino (LoRA) de um modelo pequeno para escolher a opcao do duelo. A perda conta SO o digito da resposta (o texto da decisao e o contexto).
Uso: python tools/ia_treino/treinar.py [dados=gen/ia_treino/dados.jsonl] [base=$TF_LLM_DIR/base_0.5b] [saida=gen/ia_treino/lora] [epocas=3]
Depois: python tools/ia_treino/exportar.py   (junta o LoRA, converte para GGUF Q4 e testa)"""
import sys, os, json, random, math, time
import torch
LLM_DIR = os.environ.get('TF_LLM_DIR', os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', 'tf_llm'))   # llama.cpp builds + base model (default: ../tf_llm next to the repository)
from transformers import AutoTokenizer, AutoModelForCausalLM
from peft import LoraConfig, get_peft_model
dados = sys.argv[1] if len(sys.argv) > 1 else 'gen/ia_treino/dados.jsonl'
base = sys.argv[2] if len(sys.argv) > 2 else os.path.join(LLM_DIR, 'base_0.5b')
saida = sys.argv[3] if len(sys.argv) > 3 else 'gen/ia_treino/lora'
epocas = int(sys.argv[4]) if len(sys.argv) > 4 else 3
tok = AutoTokenizer.from_pretrained(base)
modelo = AutoModelForCausalLM.from_pretrained(base, torch_dtype=torch.bfloat16).cuda()
modelo.gradient_checkpointing_enable(); modelo.enable_input_require_grads()
modelo = get_peft_model(modelo, LoraConfig(r=32, lora_alpha=64, lora_dropout=0.05, target_modules=['q_proj', 'k_proj', 'v_proj', 'o_proj', 'gate_proj', 'up_proj', 'down_proj'], task_type='CAUSAL_LM'))
ex = [json.loads(l) for l in open(dados, encoding='utf-8')]
random.Random(2).shuffle(ex); nval = max(20, len(ex) // 10); val, trn = ex[:nval], ex[nval:]
dig = {str(i): tok.encode(str(i), add_special_tokens=False)[0] for i in range(10)}
def lote(rs):
    ids = [tok.encode(r['prompt'], add_special_tokens=False)[-1800:] for r in rs]
    L = max(len(i) for i in ids) + 1
    x = torch.full((len(rs), L), tok.pad_token_id or 0); m = torch.zeros((len(rs), L), dtype=torch.long); y = torch.tensor([dig[r['answer']] for r in rs])
    pos = []
    for k, i in enumerate(ids):
        x[k, :len(i)] = torch.tensor(i); m[k, :len(i)] = 1; pos.append(len(i) - 1)   # a previsao do digito sai da ultima posicao do prompt
    return x.cuda(), m.cuda(), y.cuda(), torch.tensor(pos).cuda()
def logits_digito(x, m, pos):
    h = modelo(input_ids=x, attention_mask=m, output_hidden_states=False).logits
    return h[torch.arange(x.shape[0]), pos]
def avaliar(rs):
    modelo.eval(); ok = 0; porfonte = {}
    with torch.no_grad():
        for i in range(0, len(rs), 8):
            b = rs[i:i + 8]; x, m, y, pos = lote(b); lg = logits_digito(x, m, pos)
            for k, r in enumerate(b):
                n = r['n']; cand = [dig[str(j)] for j in range(min(n, 10))]
                p = int(torch.argmax(lg[k, cand])); c = p == int(r['answer']); ok += c
                a = porfonte.setdefault(r['tipo'], [0, 0]); a[0] += c; a[1] += 1
    modelo.train(); return ok / max(1, len(rs)), porfonte
opt = torch.optim.AdamW([p for p in modelo.parameters() if p.requires_grad], lr=2e-4, weight_decay=0.0)
bs = 4; passos = epocas * math.ceil(len(trn) / bs); passo = 0; t0 = time.time()
print('exemplos treino %d, validacao %d; acerto antes do ajuste: %.3f' % (len(trn), len(val), avaliar(val)[0]), flush=True)
modelo.train()
for ep in range(epocas):
    random.Random(ep).shuffle(trn)
    for i in range(0, len(trn), bs):
        b = trn[i:i + bs]; x, m, y, pos = lote(b)
        lr = 2e-4 * min(1, (passo + 1) / 20) * (0.5 * (1 + math.cos(math.pi * passo / passos)))
        for g in opt.param_groups: g['lr'] = lr
        lg = logits_digito(x, m, pos).float()
        # perda so entre os digitos validos (0..n-1): o servidor tambem restringe a gramatica a eles
        mask = torch.full_like(lg, -1e9)
        for k, r in enumerate(b):
            for j in range(min(r['n'], 10)): mask[k, dig[str(j)]] = 0
        loss = torch.nn.functional.cross_entropy(lg + mask, y)
        loss.backward(); torch.nn.utils.clip_grad_norm_(modelo.parameters(), 1.0); opt.step(); opt.zero_grad(); passo += 1
        if passo % 50 == 0: print('passo %d/%d perda %.4f (%.0fs)' % (passo, passos, loss.item(), time.time() - t0), flush=True)
    a, pf = avaliar(val); print('epoca %d: acerto validacao %.3f  %s' % (ep + 1, a, {k: '%d/%d' % tuple(v) for k, v in pf.items()}), flush=True)
modelo.save_pretrained(saida); tok.save_pretrained(saida); print('LoRA salvo em', saida)
