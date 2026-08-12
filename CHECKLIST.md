> Roadmap de features. **Não é fonte de parâmetro de mecânica** — para isso,
> `AUDITORIA_*.md` (com endereço) e `AUDITORIA_SUPERFICIE.md` (o que ainda não
> tem). Ponto de entrada do projeto: `RELATORIO_2026-08-12.md`.

# LF2 Vita — Roadmap

**Estado atual (v0.8+, 2026-08-12):** build data-driven rodando na Vita. Player
E inimigos interpretam `.dat` reais (`Enemy` = `Player` + IA). Combate completo:
especiais, projéteis, armas, fogo/gelo, defesa por `bdefend`, falling points.
Renderer ainda **SDL2**.

Eixo geral: interpretador (feito) → inimigos data-driven (feito) → conteúdo
(feito) → **fidelidade contra o assembly** (em curso) → áudio → GXM.

> **Este documento estava descrito na v0.6 até 2026-08-12** — dizia que os
> inimigos eram hardcoded quando já eram data-driven havia versões. As seções 1,
> 3 e 4 estavam integralmente feitas e marcadas como pendentes. Corrigido abaixo.
> Deriva de checklist é risco real: uma sessão nova leria isto e "migraria" algo
> já migrado.

---

## ✅ Feito

- [x] Parser de `.dat` completo e testado (decrypt, header, frames, sub-blocos, `data.txt` index)
- [x] `fighter.hpp` — interpretador genérico do grafo de frames (host-testado)
- [x] `player.hpp` — controlador data-driven (standing/walk/run/jump/dash/punch + dano/defesa/morte)
- [x] Timing a **30 Hz** (`TICK_MS=33`) — waits dos `.dat` na velocidade certa
- [x] Avanço por `next` (999=standing, tolerância a `next` quebrado), semântica `dvx/dvy` (550=zerar, 0=manter)
- [x] Colisão `itr`/`bdy` montada do frame corrente, em coordenadas de mundo com espelhamento por facing
- [x] Renderer: `sheetLocal()` corrige as folhas reais (0-69/70-139/140-209), não o `pic>=100`
- [x] Stats do header (walking/running/jump/dash speeds) em vez de constantes
- [x] Suíte de testes host (`test_dat`, `test_fighter`, `test_player`) verde
- [x] Build VitaSDK gerando `.vpk` que **instala e roda** (SDL2_image + libwebp/png/jpeg via vdpm)
- [x] Passagem livre entre corpos (LF2 não bloqueia movimento) e chaves de transparência por textura

---

## 1. ✅ Inimigos data-driven

- [x] `Enemy` é `Player` + IA, lendo o `.dat` real do personagem
- [x] IA de perseguição operando sobre states/frames reais
- [x] Colisão unificada: os dois lados por `forEachItr`/`forEachBdy`
- [x] `char.hpp` removido
- [ ] Apagar `dennis.hpp` e `firen.hpp` — código morto, incluídos por ninguém
      (confirmado 2026-08-12: nenhum `#include` em `src/`)

## 2. ✅ Ícone e LiveArea custom

- [x] Causa raiz: `sce_sys` PNGs precisam ser **8-bit palette (colortype 3)** — RGB/RGBA truecolor faz o promote recusar (sem bolha)
- [x] `icon0` (128×128), `bg` (840×500), `startup` (280×158) regerados em paleta, com branding LF2
- [x] `FILE sce_sys/...` reativados no `CMakeLists`
- [ ] (futuro) Arte definitiva — manter sempre em 8-bit palette

## 3. ✅ Profundidade de combate

- [x] Combos (`hit_a` encadeado: punch → punch2)
- [x] Golpes especiais via `hit_Fa`/`Ua`/`Da`/`Fj` (Quadrado + comando fiel)
- [x] Defesa por input; `bdefend` acumulado com quebra acima de 30 e dano `injury/10`
      — **do assembly** (`0x43008e`, `0x42ff6a`), não do F.LF
- [x] Contador `fall` com saturação no piso da faixa (`0x42eb6c`) e frames de
      reação 220/222/224/226 escolhidos por faixa e por facing
- [x] `arest` escalar + `vrest` por par (`0x42f2c8`)
- [ ] Agarrão (`cpoint`) — único item de combate ainda ausente
- [ ] Hitstop (`shaking`, `+0xB4`) — localizado, consumidor não isolado (A12)

## 4. ✅ Objetos e projéteis

- [x] Spawn via `opoint` → `oid` → arquivo, com `facing = qtd*10 + dir`
- [x] Objetos type 3 (bolas/chasers), type 1/2 (armas leves/pesadas)
- [x] `.dat` de objeto e folhas no VPK; pool fixo sem alocação no loop
- [x] Estados de voo: 3000 se gasta ao acertar, 3005/3006/18 atravessam (`0x430536`)

## 5. Áudio e polish

- [ ] Backend `sceAudio`: efeitos dos frames (`sound:` → `data\*.wav`)
- [ ] Música por stage (`stage.dat`)
- [ ] HUD/menu: seleção de personagem (26 no `data.txt`), barras de vida/MP corretas

## 6. Stages e roster

- [x] `bg.dat` data-driven: parser (`dat::parseBackground`) + `renderBackground`
      com parallax por camada, `loop`/tiling e `rect` fill (fiel ao binário).
      `bg/sys/*/bg.dat` no VPK; Lion Forest ligado. Falta tornar `MAP_W`/z-bounds
      runtime p/ habilitar as outras 8 fases.
- [x] Códigos `next` especiais: `1280` (disappear: invisível+intocável 150 ticks)
      e `next` negativo (vai para `|next|` invertendo o facing). São os únicos
      dois fora de `1000` em todo o data set.
- [ ] `stage.dat`: fases, spawns, waves, bosses
- [ ] Carregar qualquer personagem do `data.txt` como player e como inimigo

## 7. Backend GXM (destino "sem imposto de abstração")

> Opcional pra performance (2D sobra via SDL2), vale como arquitetura/aprendizado.
> Só **depois** do conteúdo estável.

- [ ] HAL fina própria (~10-20 funções: `init_video`/`draw_sprite`/`present`/`input`/`audio`)
- [ ] SDL2 vira só um backend atrás da HAL
- [ ] Backend GXM: shaders CG, textura com paleta indexada, swizzle das folhas

## 8. Distribuição

- [ ] README: engine open source, `.dat`/sprites do Marti/Starsky Wong — usuário fornece
- [ ] Instruções de build (VitaSDK + vdpm) e instalação; tag de versão

---

## Integridade de assets (auditoria de sprites)

Os spritesheets originais são BMPs **RLE8-comprimidos**. Vários no roster estão
**truncados** (stream RLE termina antes do fim) — não é cosmético: os pics além
do corte ficam **ausentes e silenciosos** (animação roda, nada aparece).

Distinção importante: folha **curta** (<560 px) com decode **ok** é normal (o
personagem só tem menos pics — ex.: `firen_2`, `henry_2`, `woody_2`). Só decode
**CORRUPTO** é dado danificado.

Folhas CORROMPIDAS detectadas (precisam de BMP íntegro do usuário):
`bat_1`, `davis_2` (Dennis), `deep_1/2`, `freeze_2`, `jack_0`, `jan_0/1`,
`john_2`, `julian_2`, `justin_0/1`, `knight_0/1`, `louisEX_0/2`, `mark_1`,
`rudolf_0`, `sorcerer_0`.

- [ ] **known missing: `chase_ball` do Dennis (pic 181)** — `davis_2.bmp` trunca em 800×340; sprite ausente (padding transparente evita lixo, mas NÃO é fix de dado). Aguarda `davis_2.bmp` íntegro.
- [ ] Substituir os BMPs corrompidos acima antes de habilitar esses personagens no roster
- [ ] (feito) `dennis_2`/`firen_1/2` reconvertidos via ImageMagick (o PIL falha em alguns RLE8) e padronizados a 800×560

---

### Armadilhas mapeadas (para não redescobrir)
- **Sprite ausente/deslocado ≠ bug de código:** quando um sprite some ou vira "linha", depois de conferir `pic`/`row`/`centerx`, **amostrar os pixels da célula** ANTES de tocar em código — a textura pode carregar e `sheetLocal` acertar, mas a célula estar vazia por trás da chave de cor (asset corrompido). Foi o caso do `davis_2`: inferir o mecanismo pelo sintoma levou a um `return`-antes-de-desenhar imaginário; o dado mostrou outro caminho pro mesmo sintoma.
- **BMPs RLE8:** o PIL decodifica alguns errado (silenciosamente) ou falha; usar ImageMagick (`convert`) pra RLE8.
- **Instalação/promote:** `sce_sys` custom malformado → VPK "corrompido" sem bolha. Buildar `hello_world` isola Vita vs pacote.
- **Timing 30 vs 60 fps:** roda a 30 Hz (`TICK_MS=33`), senão anima em dobro.
- **Folhas:** stride real dos PNGs é 80 px; fronteiras 0-69/70-139/140-209 (não `pic>=100`).
- **Transparência inconsistente:** Dennis = magenta, Firen e florestas = preto — chave por textura.
- **Inimigo folha única:** `firen_0.png` só tem pics 0-69; pics de dano do Dennis (120+) não existem nele.
- **`0xCDCDCDCD` (-842150451):** lixo do MSVC nos `.dat`; tratar como unset (`dat::isUnset`).
- **5 frames quebrados** no jogo original — tolerar `next` para frame inexistente.
- **Sessão git:** a sandbox não apaga locks; commits são feitos por você no WSL/PowerShell.
