"""Junta o LoRA ao modelo base, converte para GGUF e quantiza (Q4_K_M). Uso: python tools/ai/ia_treino/exportar.py [lora=gen/ia_treino/lora] [base=...] [saida=gen/ia_treino/modelo.gguf]
Precisa do codigo do llama.cpp em $TF_LLM_DIR/llama.cpp (convert_hf_to_gguf.py) e do llama-quantize.exe em $TF_LLM_DIR/llama (TF_LLM_DIR, padrao ../tf_llm ao lado do repositorio)."""
import sys, os, subprocess, torch
LLM_DIR = os.environ.get('TF_LLM_DIR', os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', '..', 'tf_llm'))   # llama.cpp builds + base model (default: ../tf_llm next to the repository)
from transformers import AutoModelForCausalLM, AutoTokenizer
from peft import PeftModel
lora = sys.argv[1] if len(sys.argv) > 1 else 'gen/ia_treino/lora'
base = sys.argv[2] if len(sys.argv) > 2 else os.path.join(LLM_DIR, 'base_0.5b')
saida = sys.argv[3] if len(sys.argv) > 3 else 'gen/ia_treino/modelo.gguf'
LL = LLM_DIR
m = PeftModel.from_pretrained(AutoModelForCausalLM.from_pretrained(base, torch_dtype=torch.bfloat16), lora).merge_and_unload()
fundido = os.path.join(os.path.dirname(saida), 'fundido'); m.save_pretrained(fundido); AutoTokenizer.from_pretrained(base).save_pretrained(fundido)
f16 = os.path.join(os.path.dirname(saida), 'modelo_f16.gguf')
subprocess.check_call([sys.executable, LL + '/llama.cpp/convert_hf_to_gguf.py', fundido, '--outfile', f16, '--outtype', 'f16'])
subprocess.check_call([LL + '/llama/llama-quantize.exe', f16, saida, 'Q4_K_M'])
print('GGUF:', saida, os.path.getsize(saida) // 1000000, 'MB')
