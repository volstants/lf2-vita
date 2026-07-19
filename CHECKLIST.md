# LF2 Vita — Checklist de próximas etapas

Estado atual: parser de `.dat` completo e testado (66 arquivos, 5.868 frames),
build VitaSDK gerando `.vpk`. Engine ainda roda com lógica **hardcoded**
(`dennis.hpp`/`firen.hpp`) e renderer **SDL2**.

Ordem geral: **interpretador** (ganho de conteúdo, testável no host) →
**conteúdo** (personagens/projéteis) → **GXM** (ganho de performance).
Parser e renderer são eixos ortogonais — o interpretador não depende do GXM.

---

## 0. Desbloqueio imediato (git)

- [ ] Apagar `.git/config.lock` e `.git/index.lock` presos (locks do OneDrive; a sandbox não tem permissão)
- [ ] `git config core.filemode false` — senão os 27 arquivos aparecem "modificados" só por mudança de permissão
- [ ] Confirmar que `data/`, `build/`, `build-out/` estão ignorados (`git status` limpo)
- [ ] Primeiro commit: parser + tools + testes + fix do VPK icon/livearea

---

## 1. Interpretador data-driven (próximo grande passo)

Substituir `dennis.hpp`/`firen.hpp` (arrays escritos à mão) por uma struct
genérica que **interpreta** um `dat::File` em runtime.

### Fundação
- [ ] `src/engine/fighter.hpp`: struct `Fighter` genérica (x, z, h, vy, vx, facing, hp, frameId, waitCounter) referenciando um `const dat::File*`
- [ ] Loader central: `dat::Index` do `data.txt` + cache de `dat::File` por `oid`, carregado de `app0:/data/`
- [ ] Remover `enum St` fixo — o "estado" passa a ser o `frameId` corrente

### Timing (bug garantido se ignorado)
- [ ] Rodar a lógica a **30 Hz**: os `wait` dos `.dat` contam ticks de ~33 ms, não de 60fps
- [ ] Ajustar `TICK_MS` (17 → 33) **ou** separar loop lógico/render com acumulador
- [ ] Verificar que animações rodam na velocidade correta (comparar contra o LF2 original)

### Máquina de estados dirigida pelos dados
- [ ] Avanço de frame pelo campo `next` (>0 pula, 0 = frame 0, <0 = pula + espelha facing)
- [ ] Tratar `next`/`hit_*` especiais: `999` (remove objeto), `1000+` (ações de engine), `-842150451` (unset)
- [ ] **Tolerar frames quebrados** — `knight` f92→216, `bat`/`justin` f121, `firen_flame` f76 apontam pra frames inexistentes *no jogo original*; não pode crashar
- [ ] Roteamento de input via `hit_a`/`hit_j`/`hit_d`/`hit_Fa`/`hit_Ua`/… (só personagens usam)
- [ ] Semântica do `state` (0=standing, 1=walking, 3=attack, 15=dash, 16=defend, 18=broken-defend, etc.)

### Física com semântica original
- [ ] `dvx`/`dvy`: `550` = **zerar** velocidade, `0` = **manter** atual (inverter isso arruína o feel)
- [ ] Gravidade e `dvz` (movimento no eixo Z) a partir dos valores do header
- [ ] Velocidades do header (`walking_speed`, `running_speed`, `jump_height`…) em vez de constantes

### Colisão a partir dos dados
- [ ] `bdy`/`itr` montados do frame corrente (não de constantes espelhadas à mão)
- [ ] Espelhamento do `itr` quando `facing` é esquerda (fórmula `79 - x - w` já não é mais hardcode)
- [ ] Tolerância no eixo Z (~10 px) para o acerto valer
- [ ] Campos de reação: `injury`, `fall` (contador de queda), `bdefend` (dano na guarda), `arest`/`vrest` (stunlock)
- [ ] Ordem fixa do pipeline por frame: input → transição → física → colisão

### Renderer (consertar antes de ligar os dados reais)
- [ ] Trocar `pic >= 100 → dennis_1.png` por `header.sheetOf(pic)` + `sheet.pixelOf()` — Dennis real tem **3 folhas de 70**, não 100
- [ ] Sprite size vem do header (`w`/`h`), não de `FRAME_W=80` fixo (real é 79×79, e projéteis variam)

---

## 2. Verificação do interpretador

- [ ] Teste host: carregar `dennis.dat`, simular N ticks de IDLE→WALK→ATTACK e conferir sequência de `pic`
- [ ] Teste de regressão: dano/knockback de um golpe conhecido bate com valores do `.dat`
- [ ] Rodar `make -f Makefile.host scan` limpo (só os 5 erros conhecidos do jogo original)
- [ ] Build ARM do interpretador sem warnings (`arm-vita-eabi-g++ -Wall -Wextra`)
- [ ] Teste em hardware/emulador: Dennis vs Firen com os `.dat` reais, sem crash

---

## 3. Conteúdo (destravado pelo interpretador)

- [ ] Spawn de projéteis via `opoint` → `oid` → arquivo (resolvido pelo `dat::Index`)
- [ ] Objetos type 3 (bolas de fogo, chasers), type 1/2 (armas leves/pesadas)
- [ ] Sistema de agarrão (`cpoint`: `vaction`/`aaction`/`throwvx/vy/vz`) — atenção ao lixo `0xCDCDCDCD`
- [ ] Carregar mais personagens jogáveis a partir do `data.txt` (26 disponíveis)
- [ ] Seleção de personagem na tela de menu
- [ ] Stages via `stage.dat` (fases, spawns, bosses)

---

## 4. Áudio e input nativos

- [ ] Efeitos sonoros dos frames (`sound:` → arquivos `data\*.wav`)
- [ ] Mapear controles do Vita para o esquema do LF2 (atk/jump/defend + direções)
- [ ] Música de fundo por stage (`music:` no `stage.dat`)

---

## 5. Backend GXM (destino "sem imposto de abstração")

> Opcional para performance (2D no Vita sobra mesmo via SDL2); vale como
> arquitetura e aprendizado. Fazer **depois** do interpretador estável.

- [ ] HAL fina própria: `init_video`/`draw_sprite`/`present`/`read_input`/`play_audio` (~10-20 funções)
- [ ] Isolar toda chamada de render atrás da HAL (SDL2 vira só um backend)
- [ ] Backend GXM: shaders CG pré-compilados, textura com paleta indexada (economiza VRAM)
- [ ] Conversão de spritesheet BMP → formato de textura nativo (swizzle)
- [ ] Backend sceGu paralelo, se quiser voltar ao alvo PSP

---

## 6. Distribuição e licença

- [ ] README: engine é open source, `.dat`/sprites são do Marti/Starsky Wong — usuário fornece os próprios
- [ ] Confirmar que nenhum asset original entra em commit público (`.gitignore` já cobre)
- [ ] Instruções de build (VitaSDK + pacotes vdpm) e de instalação (VitaShell/FTP)
- [ ] Tag de versão quando o interpretador substituir a lógica hardcoded (v0.6.0?)

---

### Notas de armadilhas já mapeadas
- **Timing 30 vs 60 fps** — quebra silenciosa (animação no dobro da velocidade)
- **`row`/`col` do header** — `row` = imagens na horizontal, `col` = nº de linhas (não o contrário)
- **`pic >= 100`** — pressupõe 2 folhas; Dennis real tem 3 de 70
- **`0xCDCDCDCD` (-842150451)** — lixo de memória do MSVC nos `.dat`; tratar como "unset" (`dat::isUnset`)
- **5 frames quebrados** no jogo original — o interpretador precisa tolerar `next` para frame inexistente
- **Comentários `#`** em `data.txt`/`stage.dat` — já tratados no parser
