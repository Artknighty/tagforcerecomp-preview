# Better AI mod (`IAAprimorada`) and the AI toolchain

The CPU duelists in the original game choose plays with a table of hand-written rules per card and a greedy attack simulation. This
mod (`mods_src/IAAprimorada`, a behavior plugin) improves those decisions without replacing the engine: it hooks the game's AI
routines, adds a few decisions the original never makes, and can optionally ask a **small language model** or a **learned board
evaluation** to choose. Everything falls back to rules, so it can never freeze the game.

> Status: experimental. The numbers below are measured with the arena described in [`TESTING.md`](TESTING.md). Where an approach
> did not beat the baseline, this document says so.

## Install

Copy the `IAAprimorada` folder into `mods/` and check it in the **Mods** menu. Turn it off there to get the original AI back.
The optional model files (`llm/`, about 500 MB) are distributed separately from the source repository.

## What the plugin does

- **Adaptive posture** (`postura()`): from LP, monsters, hand and face-down cards it picks defensive / neutral / aggressive.
- **Monster to summon**: the original takes the *last* eligible card in hand; the plugin compares all of them.
- **Attack-position summons** are vetoed when the monster cannot beat the opponent's strongest attacker; the game then sets it
  face-down in defense instead.
- **Attack planner veto**: with one monster, an empty hand and several face-down opposing cards it holds its ground unless the attack
  is lethal or clearly winning.
- **Dark Hole** only when it pays off; **extra spell rules** for cards the original AI never plays (Lightning Vortex, Brain Control,
  Premature Burial, Monster Reborn, Call of the Haunted, ...). See `g_regras[]` in `ia_aprimorada.cpp`.

## Optional: a small language model that chooses (`decisor.cpp`)

At the key decisions (which monster to summon or set, summon in attack position or not, attack with this monster or not, activate a
card or not) the plugin lists the **legal** options with consequences computed by the game (ATK/DEF/LP outcomes) and asks a model for
a single digit. Because the options are already legal, the model cannot make an illegal move, and because the numbers are computed
by the engine it does not have to do arithmetic.

- The model is a LoRA fine-tune of a ~0.5 B-parameter instruction model (Qwen2.5-0.5B-Instruct, Apache-2.0), quantized to Q4
  (~400 MB). It runs in a hidden **`llama-server`** process ([llama.cpp](https://github.com/ggml-org/llama.cpp), MIT) that the plugin
  starts on a free local port and that is killed together with the game (Windows job object).
- The plugin looks for `mods/IAAprimorada/llm/vulkan/` (GPU, any Vulkan GPU, ~40 ms per decision), then `llm/cpu/` (~1 s), and
  falls back to the rules if neither starts, if a request takes more than 3.5 s, or if the answer is not a valid option.
- `config.ini` in the mod folder: `llm=0/1`, `tempo_ms=3500`, `threads=4`, `url=http://127.0.0.1:8080` (use a server you already run).
  Environment: `TF_IA_LLM`, `TF_IA_LLM_TEMPO`, `TF_IA_LLM_URL`, `TF_IA_OFF=1` (disable the whole mod), `TF_IA_LADO=0/1` (only one
  player; for A/B tests).
- **Measured result**: the trained model plays **about as well as the rule layer** (A/B on a paired arena: 15/32 vs 16/32 wins for the
  rules; a later model 12/32). It is a drop-in, learned replacement for the rules — not yet a stronger player. Imitating rules in
  unclear situations gives rules; getting stronger needs a better training signal (see the next section).

## Learned board evaluation (experimental, `valor_pesos.h`)

The game picks which monster to summon in attack position by comparing a **score of the hypothetical board** (function `09e75cc0`,
originally "total damage the attack phase would cause") with the score of the current board. With `TF_IA_VALOR=1` the plugin returns,
instead, **P(win)** predicted by a small MLP (35 inputs → 64 → 32 → 1) trained on boards from hundreds of arena duels
(`tools/ia_valor/`). On duels not used for training it predicts the winner with **75 % accuracy, AUC 0.84** (LP difference alone:
AUC 0.66). `TF_IA_VALOR_LADO=0/1` enables it for one player only (A/B). Integration results are tracked in the repository's
test reports.

## Training toolchain

| Tool | Purpose |
|---|---|
| `tools/arena.sh`, `tools/dados_lote.sh`, `tools/ab_lote.sh`, `tools/ab_valor.sh` | CPU-vs-CPU duels at high speed, hidden window, no sound; data collection and paired A/B batches (see [`TESTING.md`](TESTING.md)) |
| `TF_IA_DADOS=1`, `TF_IA_EXPLORAR=<%>` | log every decision (`ia_decisoes.jsonl`) and optionally pick random options to explore |
| `tools/ia_treino/dataset.py` | build training examples from arena logs (labels: clear-cut tactical cases + duel outcome) |
| `tools/ia_treino/synth.py` | synthetic attack/summon situations in the plugin's exact prompt format |
| `tools/ia_treino/treinar.py`, `exportar.py` | LoRA fine-tuning on a consumer GPU, merge, convert to GGUF Q4 (needs a llama.cpp checkout) |
| `tools/ia_treino/empacotar.sh` | assemble the `llm/` folder (CPU and Vulkan servers + model) |
| `tools/ia_valor/dataset.py`, `treinar.py`, `exportar_cpp.py` | learned board evaluation: dataset from `[mon]` log lines, MLP training, export to C++ |
| `tools/ia_monitor.py`, `tools/ia_analise.py` | read the `[mon]`/`[ia]` log lines with card names |
| `tools/llm_mock.py` | fake model server for tests without a GPU |

The prompt template is defined twice and **must stay identical**: `montar_prompt()` in `decisor.cpp` and `tools/ia_treino/prompt.py`.
The 35 input features of the value network are also defined twice: `extrair_lado()` in `ia_aprimorada.cpp` and `tools/ia_valor/dataset.py`.

## Environment variables (research)

`TF_IA_OFF`, `TF_IA_LADO`, `TF_IA_SONDA=1` (log the original AI's decisions), `TF_IA_FORCAR_REGRAS`, `TF_IA_DAR_CARTA=<id>`,
`TF_IA_LLM*`, `TF_IA_DADOS`, `TF_IA_EXPLORAR`, `TF_IA_SEMENTE`, `TF_IA_VALOR`, `TF_IA_VALOR_LADO`.

## How the game's AI is organized (for contributors)

- Duel state lives at fixed PSP addresses (`0x09f79cec + player*0xfac`: LP, hand, field slots of 0x14 bytes each).
- Every decision point does `if (control[player] == 1) { CPU decides } else { open the UI }`; `0x09f77888[player] == 1` means CPU.
- Spells and traps are played from a **rule table** `{card id, predicate}` (`09f6a478`, `09f6b470`) through `09e87760`; cards outside
  the table are never played by the original AI. The plugin appends its own entries after the last one.
- The attack planner is `09e75258` (flag `-1` = simulation); the hand-card chooser is `09e762e0`; `09e75994/09e75b1c/09e759d8` save,
  place a hypothetical card in, and restore the board.
