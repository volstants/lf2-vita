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
Como consultar: clone em caminho NATIVO da sandbox (git não gerencia locks no
mount): `cd ~ && git clone --depth 1 https://github.com/xsoameix/openlf2`. Ler
com grep/sed via bash (o tool Read não alcança fora das pastas montadas). É
decompilação PARCIAL: detecção de colisão está pronta (src/class_global.c ~150-240);
a aplicação de fall/injury ainda referencia endereços crus não decompilados.

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
