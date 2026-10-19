#!/bin/sh
# Monta o pacote do mod "IA Aprimorada" para distribuir: pasta pronta para copiar em mods/ (plugin + llama-server + modelo). Uso: sh tools/ai/ia_treino/empacotar.sh [modelo.gguf] [saida=gen/pacote]
# O llama.cpp (MIT) vem de $TF_LLM_DIR/llama (build CPU) e $TF_LLM_DIR/llama_vk (build Vulkan); TF_LLM_DIR padrao = ../tf_llm ao lado do repositorio. Nada do jogo entra no pacote.
cd "$(dirname "$0")/../../.."
M="${1:-gen/ia_treino/modelo.gguf}"; OUT="${2:-gen/pacote}"; TFL="${TF_LLM_DIR:-$(pwd)/../tf_llm}"; LL="$TFL/llama"
[ -f "$M" ] || { echo "sem $M"; exit 1; }
rm -rf "$OUT"; MODS_OUT="$OUT" sh tools/build/build_plugins.sh IAAprimorada CPUvsCPU
VK="$TFL/llama_vk"
mkdir -p "$OUT/IAAprimorada/llm/cpu" "$OUT/IAAprimorada/llm/vulkan"
copia() {   # copia o servidor e as DLLs de uma build do llama.cpp ($1) para $2
  cp $1/llama-server.exe $1/LICENSE* "$2/" 2>/dev/null
  for f in $1/*.dll; do case "$f" in *bench*|*cli*|*completion*|*quantize*|*fit-params*|*perplexity*|*batched*|*tts*) ;; *) cp "$f" "$2/";; esac; done
}
copia $LL "$OUT/IAAprimorada/llm/cpu"       # CPU: roda em qualquer PC (~1 s por decisao)
copia $VK "$OUT/IAAprimorada/llm/vulkan"    # GPU (Vulkan: NVIDIA/AMD/Intel): ~40 ms por decisao; se falhar o plugin cai para a CPU
cp "$M" "$OUT/IAAprimorada/llm/modelo.gguf"
du -sh "$OUT/IAAprimorada"
