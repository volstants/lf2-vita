# LF2 Vita — Handoff (2026-07-25)

> **Leia este arquivo primeiro.** Documento autoritativo para continuar o
> desenvolvimento em uma nova instância do Claude.
> Companheiros: `CHECKLIST.md` (roadmap), `STATUS.md` (bugs/achados),
> `tools/DECOMPILE.md` (regerar o decomp), **`tools/BINARY_NOTES.md` (ler o
> `lf2.exe` direto — constantes de física, OpenLF2 como pedra de roseta)**,
> `REVIEW_PROMPT.md` (prompt de instância revisora).

---

## 1. Projeto

> **Identificação do binário de referência.** `reference/decomp/lf2.exe` é o
> **LF2 v2.0a**, build de **2009-07-10 17:15:35 UTC** (`TimeDateStamp` =
> `0x4a577737` no cabeçalho PE), SHA256
> `12dfa00f6b767508612550e9ab27ab74b4201ff4cb9ff31d068925924eab8fc5`.
> PE32 i386, MSVC 8.0, sem packer. **Não** é o build de 1999 — v2.0a é o último
> lançamento oficial e é o que a comunidade chama de "LF2 original" hoje, mas a
> distinção importa: onde estes documentos dizem "o original", leia-se
> "LF2 v2.0a". Os `.dat` interpretados vêm da mesma instalação.


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

### Protocolo de entrega (obrigatório)

**Toda vez que qualquer arquivo do projeto for alterado** — correção de bug,
feature nova, refactor, doc — a resposta termina com o bloco de build, pronto
para colar no WSL:

```bash
cd /mnt/c/Users/rodrigo.chiesa/Documents/LittleFighter2Vita/build && cmake .. && make -j$(nproc)
```

Sem exceção e sem esperar o usuário pedir. O bloco vai mesmo quando as validações
de host passaram: o host valida a lógica, não a toolchain do VitaSDK.

**Quando uma versão major for confirmada**, acompanha também o link do commit:

- Repositório: `https://github.com/volstants/lf2-vita`
- Formato do link: `https://github.com/volstants/lf2-vita/commit/<sha>`
- Comparação entre versões: `https://github.com/volstants/lf2-vita/compare/<sha_antigo>...<sha_novo>`

Ressalva: **quem commita é o usuário**, então o SHA de um trabalho recém-feito só
existe depois que ele commitar. Claude não deve inventar hash nem apresentar o
HEAD atual como se fosse a entrega — o HEAD, durante uma sessão de edição, é
sempre o commit *anterior* às alterações em curso.

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
| Gravidade | `1.7` | **binário** `lf2.exe` @0x48348 (double, única ocorrência) — antes só F.LF |
| Falling Points | começa 0; hit SOMA `fall`; decai **0.45**/tick | F.LF + LF2 Fandom |
| FP > 40 | Dance of Pain (atordoa em pé) | idem |
| FP > 60 | knockdown, FP=0 | idem |
| Valores de `fall` | 1/10/20/25/40/60/70 | idem |
| Banda de z | `abs(dz) < itr.zwidth`, default **15** | **binário** (F.LF dizia 12 — errado) |
| Anti-juggle | `fall<=40` NÃO acerta quem está em falling | OpenLF2 |
| Âncora | `player.x` É objectX; `left = x - centerx + box.x` | binário |
| MP | começa em **200** (não cheio); especial cobra `frame.mp`; `mp<0` DRENA | F.LF |
| `dvx`/`dvz` | SETAM velocidade | F.LF |
| `dvy` | **ACUMULA** (`vy += dvy`) e chega ao personagem via `Player::drainFrameDvy()` | F.LF |
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
**cenário data-driven** (parser de `bg.dat` + `renderBackground` com parallax
por camada, `loop`/tiling, `rect` fill; Lion Forest ligado) · HUD · **armas**
(pegar 115/116, segurar por wpoint↔wpoint, golpear 20/25 via itr kind 5 +
`weapon_strength_list`, arremessar 45/50, pedra sólida).

**Armas — reteste 2026-07-25c:** corrigidos 3 bugs (arremesso não disparava;
arma flutuava em ataque de corrida/pulo; portador empurrado). **Chave:** o
arremesso NÃO é `wpoint kind 3` (não existe nos dados — dennis só usa kind 1); é
um wpoint kind 1 **com velocidade** (frames 47/51/54). Ataque armado no ar/corrida
= arremesso (frames sem wpoint soltavam a arma). `solid()` só p/ arma pesada
**parada no chão** (não segurada, não arremessada). AGUARDA RETESTE no device.

**Códigos `next` especiais — FEITO.** Levantamento nos 67 `.dat`: só existem
**três** fora do comum, não a família inteira que eu supunha.
`1000` = remove o objeto (já havia). `1280` = *disappear* (Rudolf frame 257, via
`hit_Uj` 250 por 350 MP): volta ao standing e o lutador fica **invisível e
intocável** por 150 ticks (5 s), sombra piscando nos últimos 30 antes de voltar —
é o `effect.super` do F.LF (`character.js` state1280_disappear + `GC.effect.disappear`
shadow_blink 120 / body_blink 150). `next` **negativo** = vai para `|next|` e
**inverte o facing** (F.LF `switch_dir_after_trans`), usado no c-throw do Louis
(frame 270, `next: -999`). Atenção: o facing de personagem mora no controlador e
é empurrado para o Fighter pelo `syncAnchor()` todo tick, então o Fighter só
levanta a flag (`flipReq`) e o Player é quem inverte.
**LouisEX e a transformação seguem fora** — dependem de trocar o `.dat` em
runtime (`transform_character`), não de código `next`.

**`itr.effect` — FEITO (2026-07-29).** Eram **450 itrs** com efeito não-zero,
completamente ignorados: fogo do Firen e gelo do Freeze chegavam como dano
genérico. Tabela do OpenLF2 (`const.c:135-146`): 0 punch · 1 bleed · 2 fire ·
3 freeze · 4 shrafe · 20 burn · 21 flame · 22 firen_explosion ·
23 julian_explosion · 30 column. Ação por família (F.LF `character.js:1721-1732`):
**2/21/22/23** queimam **e derrubam a arma da mão**; **20** queima sem derrubar;
**3/30** congelam e derrubam a arma. Frames de vítima são canônicos: gelo
**200 → 201 (state 13, wait 90 = 3 s) → 202 → 182** (auto-suficiente, o dado
governa); fogo **203↔204 (state 18)** faz laço infinito, então a saída é externa —
trava de **36 ticks** (F.LF `trans.frame(203, 36)`) e o alvo desaba. Vítima já em
chamas é **imune** aos efeitos fracos 20/21. O `dropWeaponReq` é flag no Player
porque o slot de arma vive no `main.cpp`.

**Não implementado:** transformação/LouisEX ·
agarrão (cpoint) · Davis Leap Attack (D+^+J+A, 4 inputs) · custo de HP de alguns
especiais · `stage.dat` (waves/spawns/bosses — o `bg.dat` já é lido, falta a fase) ·
DoP com frame longo (226) · `vrest` por atacante (hoje timer único) ·
hitstop 3 · defesa por acúmulo de `bdefend` (hoje all-or-nothing).

**Cenários (bg.dat) — feito nesta sessão:** `dat::parseBackground` (dat.hpp) lê
`name/width/zboundary/perspective/shadow+shadowsize` e as camadas
(`transparency/width/x/y/height/loop/rect/cc/c1/c2`), fiel ao binário
`FUN_0040bff0` + draw `FUN_0041a250`. **Achado-chave:** o `width:` da camada é a
**largura de parallax**, não a da imagem — `screen_x = x - (width-794)*camX/(bgWidth-794)`;
camada com `width==bgWidth` acompanha a câmera 1:1 (chão), `width==794` fica fixa
(fundo distante). O render antigo hardcoded não tinha parallax e tileava o chão
pela largura da imagem em vez do `loop` (daí os "buracos"). `renderBackground`
agora consome `Scene{Background + texturas por camada}`; `rect:` = fill RGB565.
`bg/sys/*/bg.dat` empacotados no VPK; só `lf` ligado no código.
**TODO multi-stage:** `MAP_W`/`Z_MIN`/`Z_MAX` ainda são `constexpr` (== Lion
Forest); para trocar de fase em runtime, torná-los runtime a partir de
`bg.width`/`zboundary`. Assets são flat em `assets/` (nomes colidem entre fases;
subdirs por fase quando habilitar as outras). Teste: `tests/test_bg.cpp`.

## 6b. Auditoria contra as 3 fontes (2026-07-29) — OpenLF2 é a melhor fonte de colisão

`git clone --depth 1 https://github.com/xsoameix/openlf2` no HOME da sandbox
(não no mount). `src/class_global.c` é a rotina de colisão do binário e
**confirmou literalmente** nossa âncora (`left = x - centerx + box.x`, mirror por
facing, `top = y - centery + box.y`), a banda de z (`zwidth==0 → 15`,
`abs(dz) < zwidth`) e o anti-juggle. `src/const.c` dá as **tabelas de constantes**
que faltavam.

**Tabela de `itr kind` (OpenLF2 const.c:119-133)** — 0 normal_attack ·
1 catch_injured · 2 pick_up_weapon · 3 catch · 4 thrown · 5 strength_list ·
6 super_punch · 7 rowing_pick · 8 heal · 9 forcefield · 10 flute · 11 float ·
14 **stop** · 15 fly · 16 freeze.

Achados aplicados nesta auditoria:
- **`vrest == 0` = ataque de ALVO ÚNICO** (class_global.c:204-230): o original
  compara `abs(attacker->x - injured->x)` e guarda só o mais próximo. Nós
  acertávamos todos os inimigos sobrepostos com um soco. CORRIGIDO no melee e
  (a partir de 2026-07-30) também no caminho da ARMA NA MÃO, que montava
  `whi.rest` a partir de `vrest` e mesmo assim varria os três inimigos.
- **`itr` que causa dano**: aceita `kind != 1,2,7` (class_global.c:268). Censo dos
  67 `.dat` por `injury`: kind 6 tem **0 de 162 com injury** (é marcador de frame
  vulnerável em `broken_defend` 112-114 e `injured` 226-229 — ignorar está certo);
  kind 4 (thrown, 286 com injury) depende de `thrown_injury` do arremessador, que
  só um cpoint seta → inerte por ora. Passamos a aceitar **10 (flute) e 11 (float)**
  — Henry — e **15/16** (freeze_column). 8 (heal) e 9 (forcefield) ficam fora de
  propósito: o efeito deles não é dano.
- **flute/float ignoram o anti-juggle** (class_global.c:178-181). Aplicado.
- **Reação a hit é escalonada em 4 faixas de FP** (F.LF character.js:1676-1686):
  `<=20` → 220 · `21-30` → 222 · `31-40` → 224 · `41-60` → **226** (Dance of Pain,
  **state 16**, wait 6). Usávamos sempre 220. CORRIGIDO — e o 226 é o único estado
  em que um `itr kind 1` pode agarrar (OpenLF2 const.c:102), o que destrava o cpoint.
- **Arma leve no chão não pode ser destruída de mão vazia** (class_global.c:235-242):
  falha se a vítima está em `on_ground_state_1` (1004) e o atacante não é objeto
  nem está armado. Pedra/caixa (2004) pode. CORRIGIDO.
- `on_ground_state_1/2 = 1004/2004` confirma nossos frames de repouso 64/20.

**Desvios conscientes (sem fonte, ou contra a fonte) — revisar quando houver dado:**
| item | nosso | fonte diz |
|---|---|---|
| ~~`WEAPON_FLY_GRAVITY`~~ | **0.425 = 1.7/4** | RESOLVIDO pelo binário: o pool de constantes do `lf2.exe` traz 1.7 e suas frações — 1.133333 (@0x48368), 0.566667 (@0x48350), **0.425 (@0x48358)**, 0.17 (@0x48360). Achado varrendo doubles IEEE-754 no `.exe`, sem Ghidra. |
| bloqueio `itr:14` | cancela o movimento | F.LF escala a 10% (a 10% ainda se atravessa a pedra) |
| `LOOP_TTL` de efeito em laço | 3 s | original apaga por HP do objeto type 3 (`hit_a` decrementa 500 hp) — mecânica ainda não implementada |
| flecha pousada | expira em 3 s | original mantém até ser destruída/coletada |
| `arest` vs `vrest` | um timer só na vítima | são coisas distintas (atacante vs vítima) |
| pickup de arma | raio de 60 px | original usa `itr kind 2` + estado 1004/2004 da arma |
| quebra de guarda | `fall >= 60` | acúmulo de `bdefend` (recover -0.5/tick) |

## 7. Fontes de referência (ORDEM OBRIGATÓRIA — decisão do usuário)

**LEIA `tools/BINARY_NOTES.md` ANTES de dizer "não consegui achar no binário".**
O decomp tem um ponto cego: a saída C do Ghidra nunca mostra literal de float, só
referencia o endereço. Constante numérica se acha varrendo o `.exe` por padrões
IEEE-754 (foi assim que gravidade 1.7 @0x48348 e a gravidade de arma 0.425 = 1.7/4
@0x48358 saíram do binário). E o OpenLF2 tem `include/*.h` com **offsets de struct
anotados**, que decodificam a aritmética de offset crua do decomp.

1. **`reference/decomp/lf2_decomp.c`** — decompilação PRÓPRIA do `lf2.exe` (611
   funções, Ghidra). **FONTE PRIMÁRIA. Sempre tentar aqui primeiro.**
   É o binário original = a verdade. Consultar por grep/sed via bash.
2. **`reference/F.LF/`** — Project-F/F.LF (JS, clean room). **Só quando não for
   possível extrair do binário.** É reinterpretação: já divergiu do original
   (dizia zwidth 12; o binário mostrou 15). Se usar, marcar no comentário do
   código que a fonte foi o F.LF, não o binário.
3. Docs da comunidade (LF2 Fandom, lf-empire) — último recurso.
4. OpenLF2 — decompilação parcial de terceiro, secundária.

**Regra:** binário primeiro, sempre. F.LF é fallback, não atalho. Quando as duas
divergirem, o binário vence sem discussão.

Custo assumido: extrair do binário é mais lento por achado (offsets crus). Isso é
aceitável — o objetivo do projeto é fidelidade, não velocidade. O método da seção
abaixo reduz bastante esse custo.

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

## 8b. Rodada de correções 2026-07-30 (2ª revisão externa)

Os 10 achados foram corrigidos. Dois mudam número do harness, e a explicação
importa mais do que o número:

| roster | antes | depois | |
|---|---|---|---|
| dennis | 14205 | 8990 | ↓ |
| davis | 7470 | 7470 | = |
| woody | 13230 | 2835 | ↓ |
| firen | 19998 | 3337 | ↓ |
| freeze | 0 | 0 | = (pré-existente) |
| rudolf | 10610 | 10651 | = |
| louis | 10210 | 12240 | ↑ |
| henry | 1420 | **5410** | ↑ (a anomalia da 1ª revisão) |

Bissecção (variantes compiladas, uma mudança por vez):
- **Todo o delta vem de dois hunks**: o `vz_array` do multi-spawn (#6) e o dvy
  religado (#2). `#3/#5/#7/#8/#9/#10` são neutros em tudo que o harness alcança.
- O dvy religado é o que tira henry de 1420 para 5410.
- `dano que estaciona` do Rudolf continua sadio: 18000→10651, 54000→32098.
- Pool sadio em todos: pico 25/48, 0 ticks cheio, 0 spawns descartados.

**Achado NOVO, não corrigido (fora do escopo da revisão):** o pouso cancela
ataque para o frame 0. F.LF (`character.js:106-121`) prevê o pouso e vai para
**215 (crouch)** ou **219 (crouch2)** via `state_update('fall_onto_ground')`,
nunca para `standing`. Enquanto o dvy era dado morto isso quase não aparecia;
agora que `rudolf 274 jump_sword` (dvy -7) e `woody 253 fly_crash` (dvy -7)
realmente decolam, a cadeia do golpe é truncada no pouso — é a causa direta das
quedas de dennis/woody/firen na tabela acima. Corrigir isso é a próxima tarefa
de fidelidade, e deve devolver boa parte desse dano.

## 9. Próximos passos

1. **Usuário: buildar e testar as armas** (nunca testadas no device) + o **novo
   cenário data-driven** (parallax, tufos de grama espaçados por `loop` sobre o
   fill verde — deve bater com o LF2 original) e commitar.
2. ✅ **bg.dat data-driven** (feito). Falta `stage.dat` (waves, spawns, bosses) e
   tornar `MAP_W`/`Z_MIN`/`Z_MAX` runtime para habilitar as outras 8 fases.
3. **Agarrão** (cpoint) e catch/throw.
4. ✅ **Códigos `next` especiais** (feito: 1280 disappear + next negativo).
   Resta a **transformação** (Louis→LouisEX, Rudolf copiando o oponente), que é
   trocar o `.dat` do lutador em runtime.
5. Polish: DoP frame 226, hitstop 3, `vrest` por atacante, `bdefend` acumulado.
6. Davis Leap Attack (4-input) e combos raros.
7. (Fora deste repo) Remaster como projeto separado.

## 10. Como trabalhar

- Editar headers → `make -f Makefile.host test` + compile-check do `main.cpp` →
  só então pedir build ao usuário. Nunca mandar buildar sem validar no host.
- Dúvida de mecânica: **binário decompilado primeiro** (seção 7). F.LF só se não
  der para extrair do binário — e então marcar a origem no comentário do código.
- Usuário pediu respostas **econômicas**: dizer o que ele deve fazer, sem narrar
  o que foi feito.
