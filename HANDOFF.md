# LF2 Vita — Handoff (2026-07-25)

> **Leia este arquivo primeiro.** Documento autoritativo para continuar o
> desenvolvimento em uma nova instância do Claude.
> Companheiros: `CHECKLIST.md` (roadmap), `STATUS.md` (bugs/achados), `tools/DECOMPILE.md`.

---

## 1. Projeto

Engine nativo de **Little Fighter 2** para **PS Vita**, em C++17, que **interpreta
os `.dat` originais** em runtime. Nada de lógica de personagem hardcoded: um
`Fighter` genérico lê qualquer `.dat`; a diferença entre personagens está 100%
nos dados.

**Escopo deste repositório: port fiel "as is".** O remaster (quality-of-life)
será um projeto SEPARADO, futuro, baseado neste após finalizado. Não criar
branch de remaster aqui.

## 2. Ambiente e comandos

| O quê | Como |
|---|---|
| Pasta | `C:\Users\rodrigo.chiesa\Documents\LittleFighter2Vita` |
| Build Vita | WSL: `cd build && cmake .. && make -j$(nproc)` → `build/lf2-vita.vpk` |
| Testes host (rápido) | `make -f Makefile.host test` |
| Compile-check do main | `g++ -std=c++17 -I <stubs> -I src -c src/main.cpp` (stubs de SDL/psp2 no outputs) |
| Git | **O usuário commita** (a sandbox do Claude não roda git: não apaga lock no mount) |
| Deps VitaSDK | `sdl2_image`, `libwebp`, `libpng`, `libjpeg-turbo` via `vdpm` |

**Divisão de trabalho:** Claude diagnostica, edita e valida no host (testes +
compile-check). O usuário builda no device, testa e commita.

## 3. Arquitetura

```
src/engine/dat.hpp        parser dos .dat (decrypt, header, frames, sub-blocos,
                          data.txt index, weapon_strength_list)
src/engine/fighter.hpp    interpretador do grafo de frames (next/999, dv, wait 30Hz,
                          sheetLocal, drawOrigin/worldBox/pointWorld)
src/characters/player.hpp controlador: input, locomoção, combate, FP, hit()
src/characters/enemy.hpp  Enemy = Player + IA (persegue, ataca, usa especiais)
src/engine/object.hpp     projéteis E armas (voo, arremesso, repouso no chão)
src/engine/types.hpp      constantes (GRAVITY, TICK_MS, Z bounds, botões)
src/engine/render.hpp     loadTex (color key) + helpers
src/main.cpp              loop 30Hz, colisão, spawn opoint, armas, menu, HUD, roster
tests/test_*.cpp          dat, fighter, player, enemy, object
tools/bmp2png.py          converte BMPs originais (RLE8) → PNG
tools/ghidra_*.py         decompilação (ver seção 7)
```

Órfãos (código morto da fase hardcoded, podem ser apagados):
`src/characters/char.hpp`, `dennis.hpp`, `firen.hpp`.

## 4. Parâmetros de fidelidade (VALIDADOS — não mexer sem fonte)

| Parâmetro | Valor | Fonte |
|---|---|---|
| Timestep | 30 Hz (`TICK_MS=33`) | LF2 nativo |
| Gravidade | `1.7` | F.LF global.js |
| Falling Points | começa 0; hit SOMA `fall`; decai **0.45**/tick | F.LF + LF2 Fandom |
| FP > 40 | Dance of Pain (atordoa em pé) | idem |
| FP > 60 | knockdown, FP=0 | idem |
| Valores de `fall` | 1/10/20/25/40/60/70 | idem |
| Banda de z | `abs(dz) < itr.zwidth`, default **15** | **binário** (F.LF dizia 12 — errado) |
| Anti-juggle | `fall<=40` NÃO acerta quem está em falling | OpenLF2 |
| Âncora | `player.x` É objectX; `left = x - centerx + box.x` | binário |
| MP | começa em **200** (não cheio); especial cobra `frame.mp`; `mp<0` DRENA | F.LF |
| `dvx`/`dvz` | SETAM velocidade | F.LF |
| `dvy` | **ACUMULA** (`vy += dvy`) | F.LF |
| `550` | zera a componente | .dat |
| Folhas | stride 80px; fronteiras pic 0-69/70-139/140-209 | header |
| Transparência | Dennis magenta; Firen/cenário/armas **PRETO** | assets |

## 5. Controles

- **X** ataque · **O** pulo · **Triângulo** defesa · **Quadrado** especial · **Start** modo teste
- Corrida = duplo-toque direção · Dash = corrida + pulo
- **Quadrado** = especial **HORIZONTAL** só (`hit_Fa`; +Pulo = `hit_Fj`). ↑/↓ com Quadrado são ignorados de propósito.
- **Comando fiel** (verticais e todos): **Triângulo SEGURADO → direção → A/J** → `hit_[dir][a/j]`
- Armas: **X** ao lado da arma pega · **X** parado golpeia · **direção + X** arremessa

## 6. Estado

**Funciona:** parser completo · interpretador · locomoção/corrida/pulo/dash ·
socos com combo · defesa com recuo · Falling Points · especiais (Quadrado +
comando fiel) · projéteis (opoint → oid → spawn/voo/colisão) · MP · IA que
persegue, ataca e usa especiais · menu 4×2 · roster de 8 (dennis, davis, woody,
firen, freeze, rudolf, louis, henry) · render genérico (N folhas via header) ·
cenário Lion Forest · HUD · **armas** (pegar 115/116, segurar por wpoint↔wpoint,
golpear 20/25 via itr kind 5 + `weapon_strength_list`, arremessar 45/50, pedra sólida).

**PENDENTE DE TESTE NO DEVICE:** todo o sistema de armas (nunca foi testado na Vita).

**Não implementado:** códigos `next` 1000+ (LouisEX, teleporte do Woody) ·
agarrão (cpoint) · Davis Leap Attack (D+^+J+A, 4 inputs) · custo de HP de alguns
especiais · stages (só Lion Forest hardcoded; falta parser de `bg.dat` + `stage.dat`) ·
DoP com frame longo (226) · `vrest` por atacante (hoje timer único) ·
hitstop 3 · defesa por acúmulo de `bdefend` (hoje all-or-nothing).

## 7. Fontes de referência (ordem de consulta)

1. **`reference/F.LF/`** — clone do Project-F/F.LF (JS). **Fonte primária de mecânica.**
   Clean room, legível, nomes semânticos. Ver `LF/global.js` (constantes),
   `LF/character.js` (state machine), `LF/weapon.js`, `LF/livingobject.js`,
   `LF/specialattack.js`, `LF/mechanics.js`.
2. **`reference/decomp/lf2_decomp.c`** — decompilação PRÓPRIA do `lf2.exe` (611
   funções, Ghidra). **Árbitro** quando as fontes divergem. Consultar por grep.
3. Docs da comunidade (LF2 Fandom, lf-empire) — para o que nenhum código cobre.
4. OpenLF2 (github.com/xsoameix/openlf2) — decompilação parcial, secundária.

**Regra:** F.LF para descobrir; binário para confirmar quando houver dúvida/conflito.

### Como ler o binário com fluência (método validado)
As rotinas de I/O dos `.dat` no binário **nomeiam cada offset da struct**. Ex.:
`FUN_0040d0a0` (contém `fprintf("zwidth: %d")`) revelou o layout completo do `itr`:
```
[0]kind [1]x [2]y [3]w [4]h [5]dvx [6]dvy [7]fall [8]arest [9]vrest
[10]respond [11]effect [12-13]catchingact [14-15]caughtact
[16]bdefend [17]injury [18]zwidth
```
(`respond` existe no engine mas nenhum `.dat` usa.) Mesmo método serve para
`frame`, `bdy`, `wpoint`, `cpoint`, `opoint` e a struct de objeto em runtime —
fazer esse mapeamento quando for atacar códigos 1000+, cpoint ou stages.

### Regerar a decompilação
Ver `tools/DECOMPILE.md`. Ghidra headless + `ghidra_pre_aggressive.py` (preScript)
+ `ghidra_export_c.py` (postScript). **Atenção:** o `.text` tem só 283 KB (os 3,4 MB
são `.rsrc` = sprites embutidos); ~611 funções é o total real. O script já filtra
padding `int3` (sem o filtro, inflava para 2180 com 1573 stubs `swi(3)`).

## 8. Armadilhas (NÃO redescobrir)

- **Instalação VPK:** PNGs do `sce_sys` DEVEM ser paleta 8-bit (truecolor = "pacote
  corrompido", sem bolha). Buildar `hello_world` do VitaSDK isola Vita vs pacote.
- **Sandbox do Claude:** não roda git nem builda para Vita. Não tenta.
- **Sessões concorrentes do Claude no mesmo repo já corromperam diagnóstico 3×.**
  Rodar UMA por vez.
- **30 Hz:** rodar a 60 Hz dobra a velocidade de tudo.
- **Âncora:** `syncAnchor` passa `x`/`z+h` CRUS. Não pré-transformar (cancela com o
  Fighter e vira cell-pinning → jitter nos frames com centerx variável).
- **`z` não move o sprite** (só ordenação/colisão) — um projétil pode aparecer no
  lugar certo e não colidir (era z=0; `syncAnchor` seta `f.z=z`).
- **Projétil:** velocidade vem do `dvx` do FRAME (bola=15) OU do `opoint` (flecha,
  frames dvx=0 → semântica KEEP preserva a do opoint).
- **Aterrissar CORTA ataque aéreo** (fiel ao original) — não deixar terminar no ar.
- **`g_objBank` é `std::deque`, não `vector`** — Objects guardam `&entry.data`; uma
  realocação de vector dangling → crash (foi o C2-12828-1).
- **Arma na mão:** o wpoint DA ARMA coincide com o wpoint da MÃO (não é centro).
- **Dano de arma:** itr **kind 5** + `weapon_strength_list[wpoint.attacking]`.
- **5 frames quebrados** no jogo original (next → frame inexistente): tolerar.
- **`0xCDCDCDCD` (-842150451)** = lixo do MSVC nos `.dat`; tratar como unset.
- **`assets/*.png` estão no repo público** (sprites do Marti/Starsky Wong).
  Decisão do usuário: adiado. Ver STATUS.md.

## 9. Próximos passos

1. **Usuário: buildar e testar as armas** (nunca testadas no device) e commitar.
2. **Stages:** parser de `bg.dat` → `renderBackground` data-driven (destrava as 9
   fases; `bg/` completo já está no disco) + `stage.dat` (waves, spawns, bosses).
3. **Agarrão** (cpoint) e catch/throw.
4. **Códigos `next` 1000+** via decomp (LouisEX, teleporte).
5. Polish: DoP frame 226, hitstop 3, `vrest` por atacante, `bdefend` acumulado.
6. Davis Leap Attack (4-input) e combos raros.
7. (Fora deste repo) Remaster como projeto separado.

## 10. Como trabalhar

- Editar headers → `make -f Makefile.host test` + compile-check do `main.cpp` →
  só então pedir build ao usuário. Nunca mandar buildar sem validar no host.
- Dúvida de mecânica: **F.LF primeiro**, binário para confirmar. Cruzar 2 fontes
  antes de portar lógica.
- Usuário pediu respostas **econômicas**: dizer o que ele deve fazer, sem narrar
  o que foi feito.
