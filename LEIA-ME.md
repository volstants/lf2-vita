# Little Fighter 2 — Vita

Port do Little Fighter 2 v2.0a para o PS Vita. O motor atual é o próprio
`lf2.exe` recompilado estaticamente para C, no build, a partir da cópia do
usuário. Isso roda sobre uma camada Win32/DirectX emulada e SDL2.

## Onde está cada coisa

| Pasta / arquivo | Conteúdo |
|---|---|
| `BUILD/` | Pacotes locais da v2.06: `lf2-vita-v2.06-programa.vpk` e `lf2-vita-v2.06-completo.vpk`. Ambos contêm o EXE embutido no programa; o completo também inclui os dados do jogo. Pasta fora do Git. |
| `src/recomp/` | motor atual: runtime, Win32/DirectX emulados e hosts (`host_sdl.cpp` Vita/PC, `host_test.cpp` testes) |
| `tools/recomp/` | tradutor lf2.exe → C (`analyze.py`, `emit.py`) |
| `tools/emu/` | oráculo: o lf2.exe original rodando no Unicorn, para comparar |
| `tools/scen/` | roteiros de teste (especiais, objetos, status, arremessos, transformações) |
| `src/` (resto), `tests/`, `Makefile.host` | port antigo escrito à mão (motor `legacy`) e seus testes |
| `docs/` | protocolo, projeto, arquitetura, plano, oráculo, checklist, plano de testes |
| `docs/auditorias/` | auditorias e relatórios de teste, por data |
| `docs/sessoes/` | registros de sessão, status e handoff |
| `docs/prompts/` | prompts de trabalho e de revisão |
| `reference/` | evidências por rodada (`round-*`, com backups `before/`), jogo extraído, instalador; fora do Git |
| `sce_sys/` | ícone e LiveArea do VPK |
| `assets/`, `bg/`, `data/` | dados do jogo original usados pelo port antigo; fora do Git |

Pastas ocultas: `.build/` (intermediários de compilação, pode apagar),
`.venv/`, `.ghidra-exports/` e `.sdlhost/` (ferramentas do Ghidra e headers
SDL do PC, regeneráveis).

## Gerar o VPK

```
export VITASDK=/caminho/vitasdk
./build-vita.sh
# → BUILD/lf2-vita-v<versão>-completo.vpk e BUILD/lf2-vita-v<versão>-programa.vpk
```

Precisa do VitaSDK com SDL2, SDL2_mixer e dependências, de Python com
`capstone` e `pefile` (`LF2_PYTHON=...`), do executável original
(`LF2_EXE=...`, padrão `reference/decomp/lf2.exe`) e da pasta do jogo
(`LF2_GAME=...`, padrão `reference/lf2-game`, com data, bg, sprite e bgm em OGG).

O EXE é traduzido para C durante o build e também embutido no programa.
Portanto, chega ao Vita dentro do VPK; não precisa ser copiado separadamente.
O pacote `-programa` exige os dados em `ux0:data/lf2/`; o `-completo` os inclui.

Os pacotes atuais ficam restritos ao uso pessoal previsto pelo projeto.
Mesmo sem o EXE embutido, o programa contém código traduzido do lf2.exe;
um lançamento público depende de autorização do autor ou de um build feito
pelo próprio usuário com a cópia dele.

## Testes do motor recompilado

```
make -f Makefile.recomp bin/lf2-recomp-test
bash tools/scen/check_suite.sh reference/lf2-game 2
```

A suíte exige os nove roteiros completos, hashes iguais aos do original
e término bem-sucedido das execuções. Isso verifica os cenários cobertos;
desempenho e funcionamento no Vita precisam de validação no console.

Histórico e pendências da v2.06:
`docs/sessoes/RESUMO_2026-09-23_a_25.md`.

A reimplementação foi preservada na branch `reimplementacao`,
commit `726d32f`, tag `v1.09-reimplementacao`.
A branch local do port atual é `recompilacao-estatica`.
A publicação dessa organização no GitHub ainda está pendente.
