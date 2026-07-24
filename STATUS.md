# LF2 Vita — Estado da sessão (2026-07-23)

## Sumário da sessão (contexto comprimido)

Projeto: engine LF2 nativo em C++ para PS Vita que interpreta os `.dat` originais.
Pasta oficial: `C:\Users\rodrigo.chiesa\Documents\LittleFighter2Vita` (OneDrive abandonado).
Build: WSL Ubuntu, VitaSDK (`cd build && cmake .. && make`). Commits feitos pelo usuário
no Windows/WSL — a sandbox do Claude NÃO roda git (locks impossíveis de apagar).
Testes host: `make -f Makefile.host test` (test_dat, test_fighter, test_player, test_enemy).

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

## Bugs conhecidos / pendências

| # | Bug | Estado | Como verificar |
|---|-----|--------|----------------|
| 1 | "Linha acima" no chute (sprite deslocado) | NÃO REPRODUZIDO no disco atual; suspeita de artefato da folha stub antiga | Na Vita: ↓+ataque (many_foot 265) e observar se aparece tira deslocada. Se sim: bug vivo, instrumentar drawOrigin |
| 2 | dennis_2.png era o DAVIS renomeado (sessão concorrente) | **RESOLVIDO** — usuário apagou assets/ e recopiou os BMPs originais do LF2; 6 folhas (dennis_0/1/2, firen_0/1/2) reconvertidas do zero e validadas: ocupação célula-a-célula OK, inspeção visual confirma Dennis (cabelo grisalho, casaco verde) com pics 178-187 (kicks, chase_ball) preenchidos | Testar na Vita: chute (many_foot 265) e chase_ball (295) devem desenhar o Dennis certo, sem sumir |
| 3 | Jitter horizontal em frames de centerx variável | CORRIGIDO (âncora única) — aguarda teste em device | Na Vita: socos/chutes repetidos — corpo não deve tremer. Host: teste "cell slides by centerx delta" |
| 4 | Facing-left: alinhamento sprite/hitbox após mudança de âncora | ALGEBRICAMENTE correto, não testado em device | Na Vita: virar à esquerda, socar inimigo — golpe conecta na distância visual certa |
| 5 | Especiais de projétil (bola 235, chase 295) animam mas não cospem objeto | ESPERADO — falta sistema opoint | Só se resolve com o próximo passo |
| 6 | Sombra/câmera/spawn após âncora | Ajustados (-28 sombra, clamp SW/2) — validar visualmente | Na Vita: sombra sob os pés, bordas do mapa ok |
| 7 | BTN_DEFEND=0 (Triângulo) | Confirmado pelo usuário como Triângulo | — |

## Testes de verificação (na Vita, nesta ordem)

1. Soco repetido (combo 60↔65): sem tremida horizontal (#3), sprites corretos.
2. Virado à esquerda: soco conecta e sprite alinha (#4).
3. ↓+ataque (chute many_foot): sprite ok? "linha acima" ainda existe? (#1)
4. ↑+ataque e F+pulo: personagem some nos frames finais (#2 — esperado até a arte chegar).
5. Defesa (Triângulo): guarda + recuo (111) ao bloquear.
6. Sombras, câmera, bordas (#6).

## Próximos passos (ordem)

1. Usuário: rebuild + bateria de testes acima; commit
   ("Ancora unica no Fighter (objectX): fim do jitter em frames com centerx variavel").
2. Usuário: fornecer dennis_2.bmp original → completar pics 178-187 (fecha #2).
3. **Sistema de objetos/projéteis** (opoint → oid → data.txt index → spawn):
   bola de fogo/chase nascem de verdade; base p/ armas type 1/2. dat::Index já resolve oid.
4. Sistema de MP (frame.mp: custo dos especiais; regen).
5. Áudio sceAudio (sound: dos frames → data/*.wav).
6. Roster/seleção de personagem (26 no data.txt) e stages (stage.dat).
7. Backend GXM (por último; ver CHECKLIST.md).
