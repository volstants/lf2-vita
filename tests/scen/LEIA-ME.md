# Suíte de aceitação (roteiros contra o lf2.exe original)

Cada pasta é uma sessão de jogo reproduzível:

- `events.txt`: as entradas (frame, mensagem Win32, wParam, lParam em hex). São só
  teclas e cliques; não contêm nada do jogo.
- `marks.json`: quantos frames a sessão tem e o que cada trecho testa.
- `oracle_hash.log`: CRC da seção `.data` do jogo a cada frame e do heap a cada 10
  frames, gravados rodando o lf2.exe original no emulador Unicorn
  (`tools/emu/oracle.py`). São só números de verificação.

| Pasta | Frames | O que cobre |
|---|---|---|
| s1_davis_firen | 3408 | projéteis, combos, lançamento, fogo |
| s2_freeze_woody | 2648 | gelo, teletransporte |
| s3_rudolf_john | 2615 | transformação, clones, cura, campo de força |
| s4_louis_deep | 2174 | armadura, lançamentos |
| s5_henry_dennis | 5147 | armas, flauta, chute giratório |
| s6_firzen_julian | 2735 | chefes, especiais pesados |
| s7_knockback | 2796 | golpes fortes, distância e velocidade dos arremessos |
| s8_chaos | 4004 | 8 lutadores, armas (F8), muitos objetos |
| s9_stage | 1856 | modo Stage, inimigos, progressão |
| vs3100, demo3000 | 3100 / 3000 | só entradas; os hashes precisam ser regerados no oráculo |

Rodar (PC Linux/WSL, depois de `make -f Makefile.recomp`):

    tools/scen/check_suite.sh <pasta_do_jogo_original> [jobs]

O resultado só é aceito se a última linha for `SUITE: all identical to the original
(9/9 scenarios)` e o comando sair com status 0. Cada sessão precisa:
- terminar com status 0;
- ter no log todos os frames 1..F, com CRC da `.data` em cada frame e do heap a cada
  10, e a linha `end frame F`;
- ter todos os hashes iguais aos do original.

Os roteiros obrigatórios estão em `suite.txt`: se faltar qualquer arquivo de um deles
(`events.txt`, `marks.json`, `oracle_hash.log`), a suíte reprova antes de rodar.
Log vazio, truncado ou programa que falhou reprovam a sessão.

`tools/scen/replay.sh <pasta> <jogo>` refaz uma sessão no oráculo e no binário
recompilado. Ele sai com status diferente de zero se o oráculo ou o binário falharem, ou
se hashes, traço de objetos ou memória final divergirem. Qualquer mudança no
tradutor (`tools/recomp`), na camada Win32 (`src/recomp/win32_*`) ou função trocada
por código nativo tem de passar aqui antes de ir para o Vita.

Limite importante: a suíte prova igualdade só nos caminhos que essas sessões
executam. Para código escrito à mão (funções nativas/descompiladas), complementar
com teste diferencial da função isolada (ver `docs/PROXIMAS_ETAPAS.md`).
