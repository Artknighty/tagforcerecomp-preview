# Changelog

Formato: [Keep a Changelog](https://keepachangelog.com/pt-BR/1.1.0/). Versionamento: [SemVer](https://semver.org/lang/pt-BR/).

## [0.3.0] - 2026-10-18

### Adicionado
- Página **Mods** no lançador: liga/desliga mods e muda a ordem de prioridade (`mods/load_order.txt`).
- Página **Cheats** no lançador; "Unlock everything" também na tecla F2.
- Modo de janela (janela / tela cheia) no lançador e no menu Graphics.
- Marcas rápidas no controle, sem caixa de nota: L3 = defeito gráfico, R3 = performance, Start+Select = áudio (salva os últimos 8 s em WAV).
- Gravação contínua do monitor para caçar piscadas (`jogar_gravando.bat`).

### Alterado
- Cheats, CPU vs CPU e Monstros no Campo saem da lista de mods (têm menu próprio).
- Transição de música/áudio entre áreas mais suave.
- O cursor do mouse some em 3 s com controle ligado.
- A marca F8 mostra o modo de FPS real (120/60/30).

### Corrigido
- 120 FPS: letras de texto nascendo cinza ou deslizando na troca de mensagem; cabeça do NPC solta do corpo; barra do item do menu entortando; boneco do mapa congelando ou cortando ao trocar de pose.

## [0.2.0] - 2026-10-17

### Adicionado
- Opção de 120 FPS (interpolação com ritmo medido) e 60 FPS mais suave durante a sessão inteira.
- Texturas HD comprimidas em BC7 (um quarto da memória), com cache em disco e carregamento em segundo plano.
- Menu do título com Continue, New Game e Exit to Desktop (pergunta antes de sair); nome do jogador digitado dentro do jogo.
- Opção Graphics dentro do jogo e no lançador, com seletor de texturas Original/Enhanced (atalho F5).
- Cheat "Unlock everything": cartas, pacotes e duelistas do Free Duel.
- Marca de problema F8 (ou F7 / L3+R3) única: grava gráficos e estado da CPU com uma só nota.
- `tools/tf.py`: joga o jogo sozinho (botões, capturas, leitura de memória) para testes automáticos.
- Decomp legível em C++ do título, boot, modo história, mapa e tela de resultado dos duelos.
- Mapa dos 351 decks por duelista.

### Alterado
- Menus, caixas de diálogo e tela de Duel Result redesenhados em azul-marinho/dourado no pacote Enhanced Textures.
- A fonte HD agora faz parte do Enhanced Textures (sem opção própria).
- Só uma instância por pasta de saves.

### Corrigido
- Linhas e costuras na cabeça dos retratos em HD; linha no meio da arte das cartas em resolução alta.
- Cartas da Polymerization sumindo; deck errado no Free Duel (Zane recebia o do Jaden).
- Piscadas na troca de mensagem e na entrada de personagem com 60/120 FPS; quedas de desempenho na primeira execução.
- Crash no mapa da história; agenda dos personagens (jogador e diretor) errada no mapa.

### Removido
- Opções "Integer scale" e "Texture Upscaling" (substituídas pelo pacote Enhanced).
