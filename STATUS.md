# LF2 Vita — Estado da sessão (2026-07-30)

## Sessão 2026-07-30 — gate de `effect` do itr, fogo no ar, bolas perfurantes

Quatro sintomas reportados no device viraram **seis achados independentes**. Não
compartilham raiz: cada um leva sua própria cadeia de evidências. Sintoma 2
sozinho continha três mecanismos distintos (A2/A3/A4).

### A1 — itr `effect` não tinha gate nenhum
*Sintoma:* Firen toma dano e perde a corrida em chamas ao encostar em alvo
queimando. Os frames 203-206 (`fire`) de TODO personagem carregam
`itr kind:0 injury:30 fall:70 effect:20` — um corpo em chamas irradia fogo.
*Evidência (lf2.exe, `FUN_00417400` @0x417400):* cadeia de early-outs antes de
qualquer teste de caixa; itr = `piVar8` stride 0x50, `effect` em `piVar8[0xb]`
(+0x2c). `effect 4` vs personagem · `effect 20` vs não-personagem/state 18/19 ·
`effect 21` vs state 18/19 · `effect 30` vs frames 200-202 · `effect 2` com
atacante em state 19 e vítima em state 18.
**Divergência de fonte:** OpenLF2 `class_global.c:52-73` rotula as duas primeiras
como 2 e 20; o binário usa **20 e 21**. Vale o binário.
*Correção:* `lf2::itrEffectAllows()`, aplicado nos 4 caminhos de ataque.

### A2 — re-acendimento infinito (consequência de A1)
*Sintoma:* alvo queimando nunca desaba. O itr `effect: 2` da corrida em chamas
(vrest 10) reacendia a vítima a cada 10 ticks, antes dos 36 TU de `BURN_TICKS`.
*Evidência:* a mesma regra de A1 (`effect 2` + atacante state 19 + vítima state
18). Medido: 200 ticks, sempre state 18.

### A3 — `airborneTick()` apagava o state 18/13
*Sintoma:* pegar fogo no ar cancelava o fogo (pose de pulo sobrescrevia 203).
*Evidência (lf2.exe 0x0040e893-0x0040e8c5, em `FUN_0040e490`):* o original faz o
oposto — mantém o state 18 no ar e ainda lhe dá frames próprios:
```
mov  0x70(%esi),%eax            ; frame_id
cmpl $0x12,0x7ac(%edx,%ecx,1)   ; frames[frame_id].state == 18 ?
cmp  $0xcd,%eax                 ; frame_id < 205 ?
fcompl 0x48(%esi)               ; 1.0 vs object->vy
movl $0xcd,0x70(%esi)           ; frame_id = 205
```
*Correção:* state 18/13 preservados em `airborneTick()` e no pouso; **e** a
troca 203/204 → 205/206 quando `vy > 1.0`, que faltava por completo (era a
"pendência" da revisão anterior — não é pendência, é comportamento evidenciado).

### A4 — expiração do fogo com `knockedDown` já ligado — **INFERÊNCIA**
*Sintoma:* vítima trava em pé dentro do frame 180 (`next: 0` = segura).
*Evidência:* NENHUMA no binário. `knockedDown` é bookkeeping nosso, sem
contrapartida no original (o LF2 lê tudo do state do frame). É correção interna
de porte, não de fidelidade. Marcado como tal.

### A5 — knockback zerado em re-acerto no ar
*Sintoma:* tiro concentrado do Henry às vezes não derruba.
*Evidência (lf2.exe, disassembly em 0x0042ee5b / 0x0042ee6d / 0x0042eee5):*
```
42ee58:  fildl  0x14(%edx)   ; itr->dvx (+0x14)
42ee5b:  faddl  0x28(%eax)   ; + injured->x_velocity (+0x28)
42ee5e:  fstpl  0x28(%eax)
42ee6d:  fsubrl 0x28(%eax)   ; espelho para o outro facing
```
Nenhum ramo consulta o estado aéreo/fall da vítima: os testes ao redor são sobre
`file->type` (4/6) e `effect` (0x16/0x17). O nosso `lv = juggle ? 0.f : …` era
invenção sem fonte — o próprio comentário admitia.

### A6 — `onHit()` gastava toda bola no primeiro corpo
*Sintoma:* a flecha concentrada quebra no primeiro alvo. `henry_arrow2.dat` voa
em **state 3006**.
*Evidência (lf2.exe, lf2_decomp.c 81204 e 81882 — dois sítios idênticos):* o
atacante só vai ao frame `hiting` se `frames[frame_id].state == 3000`. Corroborado
por F.LF `specialattack.js:213` **apenas como fonte secundária**.
*Correção:* `Object::spent()` + `victimRest[]` por vítima (o original indexa
arest/vrest por id; um contador único tornava toda bola single-target).

### Validação do PORTE (não é evidência de fidelidade)
216 CHECKs verdes (19 novos), `check-main` contra headers SDL2 reais, harness de
3600-5400 ticks limpo (pico 27/48 do pool, 0 descartes). Demonstra apenas que
nada regrediu e que o comportamento mudou como esperado.

---

# LF2 Vita — Estado da sessão (2026-07-29)

## LIÇÃO DA SESSÃO — ler o binário direto (ver tools/BINARY_NOTES.md)

Ghidra não mostra literal de float: só referencia o endereço. Por isso a gravidade
ficou tempo demais documentada como "F.LF/comunidade". Varrendo o `lf2.exe` por
doubles IEEE-754: **1.7 @0x48348** (única ocorrência) e o pool com suas frações —
**0.425 = 1.7/4 @0x48358**, que é a gravidade de arma em voo e substituiu o 0.45
que eu havia inventado calibrando por captura de tela. O pool `0x479xx-0x483xx`
tem também ±17/±16/±14/±13/±2.4/0.85/1.2/1.4, provavelmente knockback e faixas de
tombo — atacar hitstop/bdefend/tombo daí, não do F.LF.
OpenLF2 é pedra de roseta: `include/*.h` traz structs com offsets anotados e ele
nomeia endereços (`func_4171C0_is_itr_bdy_overlap`) que casam com os `FUN_` do
nosso decomp. O decomp NÃO ficou obsoleto — é o único que mostra lógica.

## Sessão 2026-07-29 — códigos `next` especiais + ferramental

Levantei TODOS os `next` fora do comum nos 67 `.dat`: existem só **três**, não a
família 1000+ que o HANDOFF supunha. `1000` (remove, já feito), **`1280`**
(disappear do Rudolf) e **`next` negativo** (c-throw do Louis). Ambos implementados
e cobertos por teste. Fonte: F.LF `character.js` (state1280_disappear),
`livingobject.js` (switch_dir_after_trans) e `global.js` (GC.effect.disappear).
Armadilha: o facing do personagem vive no controlador e o `syncAnchor()` sobrescreve
o do Fighter todo tick — por isso o Fighter só sinaliza `flipReq`.

Ferramental novo desta sessão: harness headless (`make -f Makefile.host harness`)
que roda o `main.cpp` real sem tela com entrada roteirizada; compile-check contra
os headers REAIS do SDL2 (`tools/host_sdl.sh` + alvo `check-main`), no lugar dos
stubs ABI; modo auditoria no SELECT; `REVIEW_PROMPT.md` para instância revisora.

---

# LF2 Vita — Estado da sessão (2026-07-25c)

## Sessão 2026-07-25c — Bugs de armas (reteste no device)

Três bugs reportados no build de armas, todos corrigidos (host verde + compile-check):
1. **Não dava para arremessar.** O gate era `wpoint kind 3`, mas dennis.dat só usa
   **kind 1** (188×); o arremesso é sinalizado por um wpoint kind 1 **com velocidade**
   (frames 47/51/54: dvx 19/9/16). Fix em main.cpp: `throwIt = wpoint tem dv != 0`.
2. **Arma flutua em ataque de corrida/pulo.** Os frames jump_attack(80-82)/
   run_attack(85-89) **não têm wpoint** → a arma solta e flutua. Fiel ao LF2:
   atacar com arma na mão no ar/corrida/dash = **arremesso** (player.hpp
   `throwHeldIfArmed()` em airborneTick/runTick → frame 45/50).
3. **Portador empurrado pro lado oposto.** Única fonte de push é o bloco "solid"
   (arma pesada no chão). `solid()` agora exclui armas **arremessadas** (`!thrown`)
   além de seguradas; o loop pula `i==heldWeapon`; drop cai 40px à frente (> RAID 34).
Testes novos em test_object.cpp (solid()!thrown; throw por velocidade do wpoint).

---

# LF2 Vita — Estado da sessão (2026-07-25b)

## Sessão 2026-07-25b — Cenários data-driven (bg.dat)

Parser `dat::parseBackground` + `renderBackground` data-driven, substituindo o
Lion Forest hardcoded. Fonte: binário (`FUN_0040bff0` parse, `FUN_0041a250` draw).
Achado-chave: `width:` da camada = **largura de parallax** (não da imagem);
`screen_x = x - (width-794)*camX/(bgWidth-794)`. `loop:` = espaçamento de tiling
(0 = desenha uma vez); `rect:` = fill sólido RGB565. O render antigo não tinha
parallax e tileava o chão pela largura da imagem (causa dos "buracos" — o fix de
color-key preto era paliativo; os PNGs de land já têm alpha, então honrar
`transparency:0` do bg.dat dá o mesmo resultado, correto).
`bg/sys/*/bg.dat` empacotados no VPK (glob no CMake); só `lf` ligado no código.
Novo `tests/test_bg.cpp` (parser + math de parallax) — suíte host toda verde;
compile-check do `main.cpp` OK (stubs SDL/psp2 recriados em outputs, +SDL_QueryTexture).
Pendente device: conferir o visual do cenário (tufos espaçados por `loop`).
TODO multi-stage: `MAP_W`/`Z_MIN`/`Z_MAX` runtime a partir de `bg.width`/`zboundary`;
subdirs de assets por fase.

---

# LF2 Vita — Estado da sessão (2026-07-23)

## Sumário da sessão (contexto comprimido)

Projeto: engine LF2 nativo em C++ para PS Vita que interpreta os `.dat` originais.
Pasta oficial: `C:\Users\rodrigo.chiesa\Documents\LittleFighter2Vita` (OneDrive abandonado).
Build: WSL Ubuntu, VitaSDK (`cd build && cmake .. && make`). Commits feitos pelo usuário
no Windows/WSL — a sandbox do Claude NÃO roda git (locks impossíveis de apagar).
Testes host: `make -f Makefile.host test` (test_dat, test_fighter, test_player, test_enemy, test_object).

Referências de comportamento do LF2 (NÃO estão no repo; consultar quando houver
dúvida de mecânica em vez de inferir só dos dados):
- **OpenLF2** — github.com/xsoameix/openlf2 — decompilação do binário ORIGINAL.
  Ground truth para: contador de queda (fall/thresholds), banda de z da colisão
  (zwidth/default), spawn de opoint (z herdado), códigos de next 1000+, arest/vrest.
- **F.LF** — github.com/volstants/F.LittleFighter — reimplementação em JS (referência
  de comportamento de alto nível; foi a base dos valores hardcoded iniciais).
**DECOMPILAÇÃO PRÓPRIA (melhor fonte, 2026-07-25):**
`reference/decomp/lf2_decomp.c` — 351 funções do `lf2.exe` real via Ghidra headless
(ver `tools/DECOMPILE.md` + `tools/ghidra_export_c.py`). Gitignored. Consultar por
grep direcionado; funções sem nome (`FUN_00401234`), identificar por constantes.
Já validado nele: `FUN_004171c0` = overlap AABB, idêntico ao nosso `boxOverlap`;
`abs(dz) < 0xf` confirma banda z = **15** (F.LF dizia 12 — o binário vence).
Ressalva: 351 funções é subconjunto (auto-análise); se faltar algo, re-rodar com
análise agressiva.

OpenLF2 (secundário): clone em caminho NATIVO da sandbox (git não gerencia locks no
mount): `cd ~ && git clone --depth 1 https://github.com/xsoameix/openlf2`. É
decompilação PARCIAL: detecção de colisão pronta (src/class_global.c ~150-240);
a aplicação de fall/injury referencia endereços crus não decompilados.

Achados do OpenLF2 já aplicados (2026-07-24):
- Banda de z: `abs(dz) < itr->zwidth`, default **15** (não 12). itr especifica zwidth
  largo em golpes de rodopio. CORRIGIDO em main.cpp (HitInfo.zwidth).
- Anti-juggle: itr com `fall <= 40` NÃO acerta vítima já em falling; `fall > 40` acerta.
  CORRIGIDO no guard de colisão do player→inimigo.
- Matemática de âncora/caixa CONFIRMADA idêntica: `left = x - centerx + box.x` (right) /
  `x + centerx - w - box.x` (left), `top = y - centery + box.y`. Meu fix da âncora estava certo.
Contador de fall CORRIGIDO (docs da comunidade, LF2 Fandom "Falling"): modelo era
INVERTIDO. Real = Falling Points: FP começa 0, hit SOMA o `fall` do itr, decai 1/frame.
FP>40 = Dance of Pain (atordoado em pé); FP>60 = knockdown, FP=0. Valores de fall
sempre 1/10/20/25/40/60/70. Consequência: fall-60 sozinho NÃO derruba (60 não é >60);
fall-70 (launcher) derruba de primeira; flurry acumula. Implementado em player.hpp (fp).
Refinamento pendente: DoP usa frame injured curto (220); poderia usar o injured2/DoP
(226, ~28 frames imóvel) pro stun mais longo.

Aberto: transformação do Louis (LouisEX, next 1000+) — resolver por docs da comunidade
quando for relevante (OpenLF2 não decompilou; não precisa de Ghidra por ora).

Arquitetura: `dat.hpp` (parser: decrypt+parse dos .dat, índice data.txt) →
`fighter.hpp` (interpretador do grafo de frames: next/999, dv 550=zero 0=keep,
wait a 30Hz TICK_MS=33, sheetLocal com folhas 0-69/70-139/140-209, drawOrigin/worldBox
com espelho por centerx) → `player.hpp` (controlador: locomoção, corrida duplo-toque,
pulo 210-212 por vy, dash 213-214, socos 60/65 alternados, especiais via hit_Fa/Ua/Da,
defesa 110/111 com recuo, contador de queda FALL_MAX=60, hit() com fall do itr) →
`enemy.hpp` (Enemy = Player + IA de perseguição; alcance real 55/12px) →
`main.cpp` (loop 30Hz, colisão simétrica por itr/bdy de frame, sem body-blocking).

Marcos vencidos: parser completo (66 .dat) → interpretador → player data-driven →
VPK instala e roda na Vita → ícone/LiveArea custom (CAUSA RAIZ: sce_sys exige PNG
paleta 8-bit colortype 3; truecolor = "pacote corrompido" sem bolha; TITLE_ID
agora LF2V00002) → inimigos data-driven (firen.dat + 3 folhas) → fall counter →
movimentos básicos completos → combos + especiais → consolidação da âncora →
6 folhas de personagem reconvertidas dos BMPs originais (fix do bug Davis/Dennis)
→ cenários (Lion Forest) restaurados dos bg/sys/lf/*.bmp originais (RLE8).

ACHADO PENDENTE (usuário decidiu adiar, 2026-07-23): assets/*.bmp e as sprites
.png derivadas (321+ arquivos, personagens copyright Marti/Starsky Wong) estão
commitados no GitHub PÚBLICO (github.com/volstants/lf2-vita) desde o commit
inicial — o .gitignore cobre data/, sprite/, bg/, *.dat mas nunca cobriu assets/.
Contradiz a política definida no projeto (engine open source, assets não).
Usuário optou por "deixar como está por enquanto". Retomar quando ele quiser:
opções eram (a) gitignore+rm --cached daqui pra frente, (b) reescrever histórico
completo (filter-repo/BFG, exige force-push), (c) manter como está.

Armadilhas descobertas (não redescobrir):
- Instalação: sce_sys PNGs = paleta 8-bit; hello_world isola Vita vs pacote.
- Timing: lógica a 30Hz; 60Hz dobra velocidade.
- Folhas: PNGs stride 80px; fronteiras 70/140; dennis magenta, firen/florestas PRETO.
- 0xCDCDCDCD = unset nos .dat; 5 next quebrados no jogo original.
- Âncora: player.x É o objectX (centro); Fighter faz TODA a conta de centerx/centery.
  syncAnchor passa cru. Não reintroduzir pré-transformação (cancela e vira cell-pinning).
- SESSÕES CONCORRENTES do Claude no mesmo repo já corromperam diagnóstico duas vezes.
  Uma sessão por vez, ou definir dono dos assets.

## Estado dos controles (validado em device, exceto onde marcado)

- X = ataque (combo soco 60↔65 por buffering, cada soco = novo swing)
- Círculo = pulo · corrida = duplo-toque · dash = corrida+pulo
- Triângulo = defesa (recuo 111 ao bloquear — OK em device)
- Quadrado = especial (nível, 3 modos: dir+Sq / Sq segurado+dir / Sq arma 0,5s):
  neutro? não — Fa=frente, Ua=↑, Da=↓, Fj=Quadrado+Pulo. Funciona em device
  exceto Quadrado+Pulo (corrigido p/ nível; AGUARDA RETESTE).

## Bugs / pendências (após reteste de 2026-07-24)

| # | Item | Estado |
|---|------|--------|
| 1 | Chão com buracos pretos | CORRIGIDO (land1/land4 chave preta) — aguarda reteste |
| 2 | Quadrado+Pulo não disparava | CORRIGIDO (spc como nível + 3 modos) — aguarda reteste |
| 3 | Multi-hit do many_foot (1 hit só + inimigo expelido) | CORRIGIDO (re-hit por vrest/arest, knockback = dvx do itr: flurry 2px segura, finisher 12 lança) — aguarda reteste |
| 4 | "Linha acima" no chute | Usuário reportou "Sim" no reteste anterior — RECONFIRMAR no build atual; se persistir, instrumentar drawOrigin |
| 5 | ↑+ataque "some no fim" | Folhas completas + física normal — provável build velho; RECONFIRMAR |
| 6 | Projéteis (bola 235/chase 295) só animam | ESPERADO — falta sistema opoint (próximo passo grande) |
| 7 | Sombras | OK em device (elipse 37×9 do bg.dat) |
| 8 | Cenário Lion Forest | Restaurado + FILE entries no VPK — aguarda reteste |

## Próximos passos (ordem)

1. Usuário: rebuild + reteste dos itens 1-5 acima; commit do lote
   ("Especiais no Quadrado, re-hit vrest/arest, knockback dvx, chao/land fix").
2. **Sistema de objetos/projéteis** (opoint → oid → data.txt index → spawn/update/despawn):
   bola de fogo/chase nascem de verdade; base p/ armas type 1/2. dat::Index já
   resolve oid; empacotar dennis_ball.dat/dennis_chase.dat + folhas no VPK.
3. Sistema de MP (frame.mp: custo dos especiais; regen; barra no HUD já existe).
4. Áudio sceAudio (sound: dos frames → data/*.wav).
5. Parser de bg.dat → renderBackground data-driven (bg/ completo já no disco;
   destrava as 9 fases) + stage.dat (waves/spawns/bosses).
6. Roster/seleção de personagem (26 no data.txt; BMPs de todos já em assets/).
7. Backend GXM (por último; ver CHECKLIST.md).
