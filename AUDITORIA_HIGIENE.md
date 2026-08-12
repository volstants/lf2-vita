# Auditoria de higiene — perfil "AI slop" · 2026-07-30

Mapeamento do repositório contra o perfil de código gerado sem verificação.
Objetivo: lista acionável para correção manual. Cada item traz veredito,
`arquivo:linha` e o que mudar.

Ressalva de método: boa parte deste código foi escrita por mim em sessões
anteriores. Isto é auto-auditoria, então tratei cada item procurando a evidência
contrária à minha própria escrita, não a favor.

**Resumo:** 4 dos 7 itens de código se aplicam, sendo **1 deles um bug real
introduzido hoje** (itens 4 e 5 já corrigidos). Os 5 itens de comportamento de projeto **não** se aplicam —
é onde o repositório está mais forte.

---

## CÓDIGO

### 1. Nomenclatura genérica e inconsistente — **NÃO SE APLICA** (com uma ressalva)

Varredura por `data|result|temp|tmp|helper|value|item|obj|aux|ret` como nome de
variável retornou **uma** ocorrência: `fighter.hpp:173` `data = d`, que é o campo
legítimo `Fighter::data`. Os nomes seguem o domínio (`itr`, `bdy`, `wpoint`,
`opoint`, `frameId`, `vrest`) porque vêm do formato original.

**Ressalva real — idioma misturado.** Comentários em inglês e português no mesmo
arquivo, resultado das sessões de hoje:

| Arquivo | Linhas de comentário em PT |
|---|---|
| `src/main.cpp` | 18 |
| `src/engine/fighter.hpp` | 9 |
| `src/engine/object.hpp` | 8 |
| `tests/test_player.cpp` | 8 |
| `src/engine/dat.hpp` | 3 |

**Ação:** escolher um idioma e uniformizar. Sugiro **português** — os docs
(`STATUS`, `HANDOFF`, `TESTPLAN`, `AUDITORIA`) já estão em PT, e o código é a
minoria. Converter o inglês é mais trabalho, mas deixa o repositório coerente.

---

### 2. Comentários excessivos e didáticos — **APLICA-SE** (item de maior risco de percepção)

| Arquivo | Linhas | Comentário | % |
|---|---|---|---|
| `src/engine/fighter.hpp` | 342 | 143 | **41%** |
| `src/engine/object.hpp` | 316 | 132 | **41%** |
| `src/characters/player.hpp` | 754 | 292 | **38%** |
| `src/engine/types.hpp` | 101 | 30 | 29% |
| `src/main.cpp` | 1421 | 372 | 26% |

Dois arquivos com 41% de comentário. O conteúdo **não** é tutorial — são citações
de endereço e trechos de assembly, que é justamente o que prova verificação. Mas
a densidade sozinha produz a assinatura visual errada, e blocos de 20 linhas de
disassembly dentro de um header são o lugar errado para eles.

**Comentários que hoje contradizem o código** (achei dois):

- `fighter.hpp:57-59` — `ST_BURNING` diz *"F.LF locks it for 36 TU"* e *"state 18
  delegates to falling on landing"*. A fonte deixou de ser o F.LF (é o assembly
  em `0x0040e893`) e o pouso **não** delega mais para falling — `airborneTick`
  preserva o state 18.
- `object.hpp:116-123` — a nota "there is deliberately no canHit()/state
  whitelist here" descreve o desenho anterior; hoje existe `spent()`, que é
  exatamente um teste de state.

**Ação sugerida:** mover os blocos longos de assembly para `AUDITORIA_*.md` e
deixar no código apenas a linha de referência, no formato:

```cpp
// lf2.exe 0x0042f2c8 — arest escalar, vrest por par. Ver AUDITORIA_2026-07-30.md#a5
```

Isso preserva a rastreabilidade e derruba os dois arquivos de 41% para algo
próximo de 15%.

---

### 3. Sem coesão arquitetural — **APLICA-SE**

`src/main.cpp` tem **quatro** caminhos de ataque que resolvem o mesmo problema de
quatro jeitos, por cópia divergente:

| Caminho | Linhas |
|---|---|
| jogador (desarmado) → inimigos | ~1150-1185 |
| arma na mão → inimigos | ~1200-1270 |
| inimigo → jogador | ~1285-1310 |
| projétil → atores | ~1320-1365 |

Os quatro repetem, cada um com a sua variação: teste anti-juggle, gate de
`itrEffectAllows`, banda de z, coleta de candidatos, desempate de alvo único,
gravação de rest. Foi por isso que, em sessões anteriores, correções entraram em
um caminho e não nos outros — o `zwidth` por curto-circuito e o anti-juggle
faltando em caminhos são exatamente esse sintoma, e estão no histórico de commits.

**Ação sugerida:** extrair uma única
`bool resolveHit(const HitInfo&, VictimRef, int& arest, int& vrest)` e reduzir os
quatro blocos a chamadas. É a refatoração de maior retorno do repositório e a
mais arriscada — recomendo fazê-la **depois** do teste no device desta entrega,
não junto.

---

### 4. Tratamento de erro ausente ou genérico — **APLICA-SE** → ✅ **CORRIGIDO 2026-07-30**

- `main.cpp:754` — `SDL_Init(...)` sem verificar retorno.
- `main.cpp:764,767` — `SDL_CreateWindow` / `SDL_CreateRenderer` sem verificar
  `nullptr`.
- `render.hpp:14-19` — `loadTex` devolve `nullptr` em falha; `drawSprite` e
  `drawSpriteAt` fazem `if (!t) return;`. Falha de asset vira **sprite invisível
  sem diagnóstico**.
- `dat.hpp:51` — `loadText` devolve `{}` se o arquivo não abrir; `dat::load`
  devolve `File` vazio e o jogo prossegue com um personagem sem frames.
- **`SDL_GetError()` não é chamado em lugar nenhum do repositório.**

No device isto se manifesta como tela preta ou personagem invisível, sem pista.

**Ação sugerida:** um `LF2_CHECK(cond, msg)` que registre em arquivo no
`ux0:data/` e aborte cedo. Não precisa de exceções — precisa de uma mensagem.

#### Correção aplicada

`src/engine/log.hpp` (novo). Sem SDL de propósito, para que `dat.hpp` — que é
puro e roda nos testes de host — possa registrar do mesmo jeito; quem tem SDL
passa `SDL_GetError()` como argumento.

- `LF2_WARN(cond, fmt, ...)` — falha recuperável, registra e segue.
- `LF2_CHECK(cond, ret, fmt, ...)` — registra e devolve `ret`. **Não aborta:**
  abortar na Vita fecha o app sem rastro na tela, e o log já é o rastro.
- Destino: `ux0:data/lf2vita.log` no device, `lf2vita.log` no host, `stderr` se
  o arquivo não abrir. `fflush` a cada linha, senão um crash logo depois leva o
  log junto.

Pontos ligados: `SDL_Init`, `IMG_Init`, `SDL_CreateWindow`, `SDL_CreateRenderer`
(main.cpp), `IMG_Load` e `SDL_CreateTextureFromSurface` (render.hpp),
`dat::loadText` (dat.hpp), e um `LF2_WARN` por personagem do roster que carregue
sem frames.

#### Achado colateral — o harness rodava com renderer nulo

Na primeira execução o log acusou:

```
[AVISO] SDL_CreateRenderer acelerado falhou (Couldn't find matching render driver); tentando software
```

Sob `SDL_VIDEODRIVER=dummy`, `SDL_RENDERER_ACCELERATED` não existe. O código
antigo guardava o `nullptr` sem verificar, e todas as chamadas de render
seguintes viravam no-op silencioso dentro do SDL. **O harness nunca exercitou o
caminho de render.** Agora existe fallback para `SDL_RENDERER_SOFTWARE`, que
também é a diferença entre abrir e não abrir num device onde o driver acelerado
esteja indisponível.

Os números do harness não mudaram (3600 ticks, dano 2150, pool 27/48, 0
descartes), o que era o esperado — o render não alimenta a lógica.

---

### 5. API usada "quase certa" — **UMA OCORRÊNCIA** → ✅ **CORRIGIDO 2026-07-30**

`src/main.cpp:548`, dentro de `damageObjects`:

```cpp
{ int dummy = 0; applyRest(hi, o.rehit, dummy); }
```

Aqui `o` é **a arma que está apanhando** — a vítima. `o.rehit` é o cooldown de
re-dano daquela arma, ou seja, um contador **por par**. Eu o passei no slot
`attackerArest`.

Consequência: para todo itr com `vrest > 0` (a maioria), `applyRest` grava
`o.rehit = itr->arest` — frequentemente **0** — e joga o `vrest` no `dummy`, que
é descartado. Resultado: a arma passa a poder ser danificada **todo tick** em vez
de a cada `vrest`. Pedras e facas quebram muito mais rápido que antes.

O código anterior era `o.rehit = hi.rest > 0 ? hi.rest : 8;`.

**Correção (revisada).** Inverter os argumentos resolve o sintoma mas descarta o
`arest` de novo, e ele **não** é dispensável aqui. Em `FUN_00417400` a lista de
vítimas (`injured_of_attack[]`) é construída sobre todos os objetos, armas
inclusive — uma arma é um objeto com `file->type` 1/2/3 e entra na mesma
varredura que os personagens. A gravação em `0x0042f2f7` (`mov %eax,0xec(%edx)`)
é incondicional quanto ao tipo da vítima. Logo, socar uma pedra consome o `arest`
do atacante no original.

Com um `dummy` isso se perde: se o golpe acerta só uma arma e nenhum inimigo,
`playerArest` fica 0 e no tick seguinte a mesma caixa pode danificar outra arma.

Patch correto — passar o escalar do atacante:

```cpp
// linha 264 (fwd decl) e 530 (definição)
static void damageObjects(const HitInfo& hi, bool fromRight, int skipIdx,
                          bool attackerArmed, int& attackerArest);

// linha 548
applyRest(hi, attackerArest, o.rehit);

// linhas 1189 e 1227 — os dois call sites têm o jogador como atacante
damageObjects(hi,  player.right, player.heldWeapon, player.heldWeapon >= 0, playerArest);
damageObjects(whi, player.right, player.heldWeapon, /*attackerArmed=*/true,  playerArest);
```

O gate não precisa mudar: ambos os call sites já estão dentro de blocos guardados
por `playerArest == 0`, e isso está certo — no original o `arest` só bloqueia o
tick seguinte, porque a coleta de vítimas acontece toda numa passada antes da
aplicação.

Peso relativo: o que muda comportamento visível é o `vrest` indo para o descarte
(arma quebrando rápido demais). A parte do `arest` é fidelidade, não sintoma.

Regressão minha, de hoje, não coberta por teste. É o item mais urgente desta
lista e o único que muda comportamento observável.

---

### 6. Reimplementação de stdlib / dependências desnecessárias — **NÃO SE APLICA**

Dependências: `SDL2` e `SDL2_image`, ambas usadas de fato. Nada de terceiros para
tarefa trivial.

A tokenização própria em `dat.hpp` não é reimplementação de stdlib: o formato
`.dat` é proprietário e criptografado por XOR com chave própria. Não há
`std::regex`, `boost`, nem parser genérico onde caberia um `strtok`.

---

### 7. Otimizações para o hardware-alvo — **APLICA-SE PARCIALMENTE**

**Carregamento de textura dentro do loop de jogo.** `objAssetsCached`
(`main.cpp:109`) é chamado por `spawnOpoints` (`main.cpp:470`), que roda a cada
frame em que um opoint dispara. Em cache miss ele executa `IMG_Load` +
`SDL_CreateTextureFromSurface` (`main.cpp:99`) — I/O de arquivo e alocação de
textura dentro de um orçamento de 33 ms. Existe pré-carga (commit "precarrega
assets de projeteis"), mas o caminho de miss continua alcançável em jogo.
**Ação:** pré-carregar todos os oids do roster na entrada da partida e fazer o
miss em jogo virar log + descarte, nunca carga.

**O que está certo e não precisa mexer:** o pool de objetos é fixo
(`ObjectPool<N>`, sem alocação no loop); a varredura confirmou zero
`new`/`malloc`/`push_back`/`std::string` no loop de jogo; `sheetAsset` só é
chamado no carregamento; `sheetOf` é varredura linear de ~3 elementos por draw,
irrelevante.

**Sobre NEON:** não vejo onde caberia com honestidade. O jogo é 2D e todo o blit
passa pelo `SDL_RenderCopyEx` acelerado. Não vou listar "faltou NEON" como
defeito só porque está na lista genérica — seria inventar problema.

**Fragmentação de heap:** não há alocador custom, mas também não há alocação
sustentada em jogo. Baixa prioridade.

---

## COMPORTAMENTO DO PROJETO

### 8. Mensagens de commit genéricas — **NÃO SE APLICA**

Amostra real do histórico:

```
Parser de .dat: decrypt, header, objetos, data.txt index, validacao
Especiais no Quadrado (nivel+3 modos), re-hit por vrest/arest, knockback por dvx do itr
Fix armas: arremesso por velocidade do wpoint, ataque armado no ar/corrida arremessa
Flechas/dardos pousados expiram (pool de 24 enchia e travava os especiais)
Corrige 7 achados da revisao: knockedDown preso, zwidth por curto-circuito, ...
```

Nenhum "fix bug" ou "improve performance". As mensagens dizem **o quê** e
frequentemente **por quê**. É a defesa mais forte que o projeto já tem, e é
gratuita — basta continuar.

### 9. Incapacidade de explicar decisões — **NÃO SE APLICA**

Este é o teste mais forte da lista, e o repositório responde a ele por escrito:
`AUDITORIA_2026-07-30.md` traz endereço, disassembly e fluxo reconstruído por
achado; `tools/BINARY_NOTES.md` traz as receitas de extração; `STATUS.md`
registra o que foi tentado e descartado (incluindo a admissão de que `0.45` de
gravidade foi inventado e substituído por `0.425` do binário).

**Ação sugerida:** garantir que esses arquivos estejam versionados no repositório
público, não só locais. Registrar erro próprio corrigido é a evidência mais
difícil de falsificar.

### 10. Velocidade de lançamento incompatível — **NÃO SE APLICA**

O histórico mostra progressão real (v0.6 → v0.7 → v0.8) com commits de correção
intercalados, não um salto para "pronto".

### 11. Ausência de builds intermediários — **NÃO SE APLICA**, mas dá para reforçar

Os commits mostram iteração. **Ação sugerida:** criar tags `v0.6`, `v0.7`,
`v0.8.0` retroativamente e anexar o `.vpk` de cada uma. Tag + artefato é o que um
revisor externo consegue verificar sem ler o histórico inteiro.

### 12. Crashes em casos de borda óbvios — **NÃO AUDITADO**

Não fiz auditoria de robustez. Candidatos que eu olharia primeiro, sem afirmar
que são defeitos: índices de `victimRest[]` versus `NUM_ENEMIES`; `f.data->frame(id)`
devolvendo `nullptr` para `next` pendurado; `.dat` ausente ou truncado;
pool de objetos esgotado sob spam de especiais (o harness cobre este último e
está limpo).

---

## Ordem sugerida de correção

| # | Item | Esforço | Impacto |
|---|---|---|---|
| ~~1~~ | ~~Bug do `applyRest` em `main.cpp:548`~~ | — | ✅ feito |
| 2 | Dois comentários que contradizem o código | minutos | percepção |
| ~~3~~ | ~~Checagem de erro + `SDL_GetError`~~ | — | ✅ feito |
| 4 | Pré-carga de assets de projétil | horas | frame time |
| 5 | Uniformizar idioma dos comentários | horas | percepção |
| 6 | Mover blocos de assembly para os docs | horas | percepção |
| 7 | Unificar os 4 caminhos de ataque | dias | manutenibilidade |
| 8 | Tags + `.vpk` por versão | minutos | defesa externa |

Os itens 1 e 3 mudam comportamento. Do 2 ao 6 e o 8 são cosméticos ou de
processo. O 7 é o único que eu faria **depois** do teste no device desta entrega.
