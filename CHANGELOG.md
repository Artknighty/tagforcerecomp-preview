# Changelog

Formato: [Keep a Changelog](https://keepachangelog.com/pt-BR/1.1.0/). Versionamento: [SemVer](https://semver.org/lang/pt-BR/).

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
