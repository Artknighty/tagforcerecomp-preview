# Better AI (behavior mod for Tag Force Recomp)

Makes CPU duelists play better. On = improved AI; off (Mods menu) = the original AI. Full documentation: `docs/AI.md` in the repository.

**Install:** copy the `BetterAI` folder into `mods/` and check it in the **Mods** menu (applies on restart).

**What it does**
- Adaptive posture: from LP, monsters, hand and the opponent's face-down cards, the CPU picks defensive, neutral or aggressive.
- Monster to summon: the original picks the *last* eligible card in hand; now all are compared.
- Summons in attack position are vetoed when the monster cannot beat the opponent's strongest attacker.
- Dark Hole only when it pays off; it holds the card when ahead or when there is a single opposing monster.
- Attack order: the strongest monster attacks first (the original AI sorted attackers weakest-first).
- Holding the line: with one monster, an empty hand and several face-down opposing cards it does not attack blindly (unless lethal).
- Cards the original AI never plays (it only plays spells from a list of 148): Lightning Vortex, Brain Control, Premature Burial and others
  (one row per card in `g_regras` in `ia_aprimorada.cpp`).

**Optional language model** (the `llm/` folder: a ~400 MB fine-tuned model and the `llama-server` of llama.cpp, MIT license). The game starts the
server by itself (hidden process, killed with the game); at each key decision the plugin lists the **legal** moves with their consequences
(computed by the game) and the model picks one. If the model does not load, takes more than ~3.5 s or answers garbage, the plugin's rules decide.
The plugin tries `llm/vulkan` (GPU, ~40 ms) and then `llm/cpu` (~1 s). **Off by default since 2026-10-06** (the rules alone played better); turn it on with `config.ini` → `llm=1`.
`config.ini` (optional): `llm=0/1`, `tempo_ms=3500`, `threads=4`, `url=http://127.0.0.1:8080` (use a server you already run).

**Research variables:** `TF_IA_OFF=1` (off), `TF_IA_LADO=0/1` (improve only one player, for A/B with the CPU vs CPU mod), `TF_IA_SONDA=1`
(log the original decisions), `TF_IA_DADOS=1` (log every decision to `ia_decisoes.jsonl`), `TF_IA_EXPLORAR=25` (random option in 25 % of
decisions, for training data), `TF_IA_VALOR=1` (learned board evaluation, experimental). See `docs/TESTING.md` for the arena.
