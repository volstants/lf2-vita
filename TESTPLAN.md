> ## ⚠ Critérios de teste ≠ evidência de fidelidade
>
> Passar num teste daqui prova que o porte faz o que **nós** decidimos que ele
> faria. Não prova fidelidade ao LF2 original — isso só sai de
> `AUDITORIA_*.md`, com endereço. Onde um critério depender de valor Nível D,
> está marcado no próprio item: anote o observado, não compare com o esperado.

# Roteiro de teste no device — LF2 Vita v0.8.0

**Reestruturado em 2026-08-12 como checkpoints com porteiro.** O conteúdo
anterior — os três roteiros acumulados, 55 + 28 + 11 itens — está preservado
inteiro no **§7 Catálogo**, com a numeração original. O que mudou é a ordem de
execução e o critério de parada; nenhum item foi removido.

---

## 0. Como usar

**Checkpoint com porteiro.** Cada bloco começa com um CP que decide se o resto
do bloco vale a pena. Se o porteiro falhar, **pare o bloco**, anote e vá para o
próximo **bloco** — não para o próximo item. Um bloco inteiro rodado em cima de
um porteiro quebrado produz 20 linhas de "ERRO" que dizem a mesma coisa.

Marque `OK` / `ERRO` + uma linha do que viu. **[N]** comportamento novo ·
**[R]** regressão que não pode ter voltado · **[!]** risco assumido.

**Puxe `ux0:data/lf2vita.log` depois de CADA sessão, mesmo quando tudo passar.**
Ele é reescrito a cada abertura do app (`fopen(..., "w")`), então o log da
sessão anterior morre quando você abre de novo. É o único registro que
sobrevive ao device.

---

## 1. Duas coisas que o roteiro antigo não cobria

### 1.1 Ele não tinha nenhum item de plataforma

Os 94 itens do catálogo são todos de mecânica de jogo — e mecânica de jogo é
justamente o que a suíte de host e o harness headless já exercitam. Os modos de
falha que **só existem no hardware** — não bootar, asset faltando, controle sem
mapeamento, memória, suspensão — não tinham um item sequer. O **Bloco A** existe
por isso, e é porteiro de tudo.

### 1.2 O jogo desacelera em silêncio, e nada avisa

`main.cpp` roda passo fixo com acumulador:

```cpp
constexpr int MAX_STEPS = 5;                       // main.cpp:861
while (accumMs >= (Uint32)TICK_MS && steps < MAX_STEPS) { … }
…
if (steps == MAX_STEPS) accumMs = 0;               // main.cpp:1450  ← descarta o atraso
```

Se a Vita não segurar 30 Hz, o laço satura em 5 passos e **joga o atraso fora**.
O jogo não engasga nem pula quadro: ele roda em **câmera lenta**, com a física
correta e o relógio errado. Sem referência externa, isso é indistinguível de
"está normal" — e todo item de tempo do catálogo ("~1,2 s", "~3 s", "~5 s")
vira inútil.

`TICK_MS = 33` ⇒ o alvo é **30,30 Hz**.

---

## 2. O instrumento

### 2.1 O metrônomo grátis — já está no build

O modo auditoria (SELECT) dispara um golpe a cada `AUDIT_PERIOD = 50` ticks
(`main.cpp:967`). A 33 ms por tick isso é **1,650 s por disparo**, cravado no
relógio do jogo, não no relógio real.

**Conte 30 disparos com cronômetro. Tem de dar 49,5 s.**

| Medido | Significa |
|---|---|
| 49,5 s ± 0,5 | 30,3 Hz — o device segura |
| 55 s | ~27 Hz, perdendo 10 % |
| 60 s | ~25 Hz, perdendo 17 % |
| 66 s+ | saturando `MAX_STEPS` de forma crônica |

Zero código, e é a única medida de tempo confiável de todo o roteiro. **Todo
item do catálogo que cita segundos só vale depois que este número fechar.**

### 2.2 Telemetria opcional — ~15 linhas, e paga o custo em uma sessão

Sem isto o log não produz um único número. Se quiser transformar a sessão de
teste em dado em vez de impressão, cole depois de `main.cpp:1450`:

```cpp
// TELEMETRIA — remover ou proteger com #ifdef antes de release
static Uint32 tmT0 = SDL_GetTicks();
static int tmTicks = 0, tmClamp = 0, tmMaxStep = 0;
tmTicks += steps;
if (steps == MAX_STEPS) ++tmClamp;
if (steps > tmMaxStep)  tmMaxStep = steps;
if (tmTicks >= 300) {                                   // ~10 s de jogo
    Uint32 el = SDL_GetTicks() - tmT0;
    lf2::logLine("TELEM", "ticks=%d wall=%ums hz=%.2f clamps=%d maxstep=%d",
                 tmTicks, el, tmTicks * 1000.0 / (el ? el : 1u),
                 tmClamp, tmMaxStep);
    tmT0 = SDL_GetTicks(); tmTicks = 0; tmClamp = 0; tmMaxStep = 0;
}
```

`clamps > 0` é a linha que interessa: é o atraso sendo descartado. Se quiser
também o número de objetos vivos, acrescente o contador do pool — **confira o
nome do acessor real em `object.hpp` antes**, eu não verifiquei.

```bash
./build-vita.sh
curl -T build/lf2-vita.vpk ftp://<ip-da-vita>:1337/ux0:/
```

---

## 3. BLOCO A — Plataforma · **porteiro de tudo**

Nada do Bloco B em diante vale se algum destes falhar.

| CP | Faz | Passa se | Se falhar |
|---|---|---|---|
| **A1** 🔒 | `./build-vita.sh` e instala o `.vpk` pelo VitaShell ou FTP | build sem erro, VPK instala, ícone aparece na LiveArea | pare tudo; é problema de toolchain, não de porte |
| **A2** 🔒 | Abre o app, deixa chegar ao menu, fecha, puxa `ux0:data/lf2vita.log` | chega ao menu de seleção; log **sem nenhuma linha `ERRO`** | leia o log — `LF2_CHECK` já diz o arquivo e a linha |
| **A3** 🔒 | Ainda no log de A2, procura `AVISO` | **zero** `AVISO` de asset | cada `AVISO` é um sprite que vai faltar em silêncio no jogo (`drawSprite` só faz `if (!t) return;`) |
| **A4** | Entra numa partida e olha a tela inteira | 960×544 nativo, sem barra preta nem corte; HUD legível a distância de mão; a faixa amarela do modo auditoria aparece no topo ao apertar SELECT | anote **onde** cortou |
| **A5** 🔒 | Testa cada entrada, uma por vez: ←→↑↓, Ataque, Pulo, Defesa, Especial, START, SELECT | todas respondem | sem isto nenhum teste de mecânica é interpretável |
| **A6** 🔒 | Duplo toque: →→ (correr), ←← , ↓↓ , e as sequências de especial (ex.: ↓→+Ataque) | correr sai; especiais saem | LF2 depende de janela de duplo toque; num D-pad de Vita ela pode estar apertada demais |
| **A7** 🔒 | **Metrônomo (§2.1)**: SELECT, cronometra 30 disparos | **49,5 s ± 0,5** | anote o número. **Se falhar, todo item com "~N s" no catálogo está suspenso** — vá para o Bloco B, que é onde a causa está |
| **A8** | Cronometra do toque no ícone até o menu, e do menu até a partida | anote os dois números; primeira vez e segunda vez (cache) | — |
| **A9** | Partida de **10 minutos** contínuos, jogando de verdade | sem crash, sem travar, sem sprite sumindo com o tempo; log limpo no fim | anote o minuto aproximado |
| **A10** | Botão PS no meio da partida, espera 30 s, volta | volta jogável, sem acelerar para compensar o tempo parado | se acelerar, o acumulador não está sendo zerado na retomada |
| **A11** | Sai pelo menu / fecha o app | fecha limpo, sem tela preta pendurada | — |

---

## 4. BLOCO B — Carga e pool · **onde a lentidão do A7 vai aparecer**

Porteiro: **A1-A3**. Rode mesmo que A7 tenha falhado — é aqui que se descobre
por quê.

| CP | Faz | Passa se | Catálogo |
|---|---|---|---|
| **B1** 🔒 | Henry, 25+ flechas seguidas | continuam saindo; o pool não enche | 44 |
| **B2** | Repete o metrônomo (A7) **com as 25 flechas na tela** | mesmo 49,5 s | — |
| **B3** | Firen: fogo no chão + 3 inimigos queimando ao mesmo tempo | sem engasgo visível | — |
| **B4** | Repete o metrônomo **durante B3** | mesmo 49,5 s | — |
| **B5** | Fumaça, estilhaços, flechas no chão: espera ~3 s | somem sozinhos; faca e pedra do cenário **não** somem | 45, 46, 47 |
| **B6** | Primeiro especial de cada um dos 8 do roster | sem engasgo no primeiro disparo (assets pré-carregados) | 55 |
| **B7** | Sessão de 5 min só com especiais pesados | sem degradação progressiva do metrônomo | 28 |

**Se B2 ou B4 der pior que A7**, a lentidão é de carga e o teto está no
número de objetos ou no desenho. **Se A7 já falhou sozinho, sem carga**, o teto
é do laço base e nada no catálogo vai medir certo até isso ser resolvido.

---

## 5. BLOCO C — Mecânica já auditada e implementada

Porteiro: **A5, A6 e A7**. Sem controle confiável e sem relógio confiável, isto
aqui não é teste, é impressão.

O modo auditoria (SELECT) é o banco de provas: alvos parados a 110/260/430 px,
virados para você, MP no máximo, revivendo sozinhos. Use-o para tudo que não
exija o alvo se mexendo.

| CP | O que verifica | Achado | Itens do catálogo |
|---|---|---|---|
| **C1** 🔒 | Modo auditoria funciona: faixa amarela, 3 alvos posicionados, ciclo de golpes, barras mexendo, revive ao morrer, alvos ficam em pé entre golpes | — | 37-43 |
| **C2** | Reação a dano em faixas e saturação no piso | A8 | 29, 30, 31, 8-12 |
| **C3** | 222 vs 224 **por facing**, não por intensidade | A9 | 32, 33 |
| **C4** | Defesa: custa `injury/10`, quebra por `bdefend > 30`, recupera 1/tick | A10, A11 | 34, 35, 36 |
| **C5** | Tombo forçado: alvo no ar, alvo congelado | A8 | 37, 38 |
| **C6** | Gate de `itr->effect`: fogo/gelo não reacendem | A1 | 1-6 |
| **C7** | Fogo/gelo sobrevivem ao salto e ao pouso; frames 205/206 | A2 | 7-11 |
| **C8** | Knockback horizontal em re-acerto | A3 | 12-14 |
| **C9** | Bola perfurante e o que **não** atravessa | A4 | 15-20 |
| **C10** | `arest` × `vrest`: cadência de re-acerto, alvo único | A5, A7 | 21-24, 13, 14 |
| **C11** | Física de arma com `0.425` | — | 24, 25 |
| **C12** | Arma no chão: imunidade de mão vazia | — | 26-29 |

**Item 2 do catálogo (duração da queimadura): anote o número, não compare.**
`BURN_TICKS = 36` é Nível D, o candidato `+0xEA` foi refutado e como o original
sai do state 18 segue em aberto. Este é o único item do bloco cujo "esperado" é
"anotar".

---

## 6. BLOCO D — Linha de base do que vai mudar · **não é teste**

A16, A17, A14, A15 e A18 **não estão implementados**. Medir estes itens hoje
mede o modelo velho, e o resultado não é aprovação nem reprovação.

Rode assim mesmo, uma vez, **antes** de implementar — para ter o "antes" contra
o qual comparar o "depois". Grave em vídeo se puder; a comparação lado a lado
vale mais que a descrição.

| CP | Grave | O que vai mudar quando A14-A18 entrarem |
|---|---|---|
| **D1** | Um golpe que derruba, do acerto até o pouso | hoje não há hitstop; vai passar a haver ~5 ticks de congelamento antes do arremesso (A17) |
| **D2** | O mesmo golpe, olhando a altura do tombo | hoje `launch(-8.f)`; vai virar `itr->dvy` ou `-7.0` (A14) |
| **D3** | Dois golpes quase simultâneos no mesmo alvo | hoje somam; vão passar a dividir por `golpes+1` (A16) |
| **D4** | Barra de MP no início da partida e o tempo para encher | hoje 200 e 1 a cada 2 ticks; vai virar 500 e `(500-hp)/100+1` por tick (A18) |
| **D5** | Cair de frente e de costas | hoje o mesmo sprite; vai passar a alternar 180/186 (A15) |

**D4 é o único que dá para conferir de olho fechado**: se a barra de MP começar
cheia depois da implementação e hoje começa em ~40 %, a mudança entrou.

---

## 7. BLOCO E — Regressões · rodar sempre, por último

| CP | Faz | Catálogo |
|---|---|---|
| **E1** | Partida completa contra 3 inimigos: ninguém imortal, preso ou invisível | 25, 39 |
| **E2** | Pegar, usar e arremessar arma; nenhum empurrão ao pegar | 26, 48-52 |
| **E3** | Especiais de Firen, Henry, Dennis, Louis, Rudolf | 27 |
| **E4** | Estados por personagem: Louis investida, Woody teleporte, Rudolf transformação | 17-20 |
| **E5** | Códigos `next` especiais: Rudolf invisível, Louis c-throw | 21-23 |
| **E6** | Cenário: parallax, colisão com a pedra correndo/andando/pulando, sair de dentro dela | 30-36 |
| **E7** | Ninguém desliza durante ataques; nenhum sprite congelado ao fim | 53, 54 |

---

## 8. Ficha de sessão — preencher e devolver

```
Data:                  Build:  v0.8.0  commit:
Firmware/enso:         Modelo da Vita:
Telemetria aplicada?   sim / nao

BLOCO A
  A1 build/instala        [  ]
  A2 boot, log sem ERRO   [  ]   linhas de ERRO:
  A3 log sem AVISO        [  ]   assets faltando:
  A4 tela 960x544         [  ]
  A5 entradas basicas     [  ]   quais falharam:
  A6 duplo toque          [  ]
  A7 METRONOMO            [  ]   30 disparos = ______ s   (alvo 49,5)
  A8 tempo de carga       ______ s ate menu / ______ s ate partida
  A9 10 min continuos     [  ]   travou no minuto:
  A10 suspender/retomar   [  ]
  A11 sair limpo          [  ]

BLOCO B
  B2 metronomo c/ flechas ______ s      B4 metronomo c/ fogo ______ s
  outros:

BLOCO C   (so' se A5, A6 e A7 passaram)
  C1 [  ]  C2 [  ]  C3 [  ]  C4 [  ]  C5 [  ]  C6 [  ]
  C7 [  ]  C8 [  ]  C9 [  ]  C10 [  ] C11 [  ] C12 [  ]
  duracao da queimadura observada: ______ s   (anotar, nao comparar)

BLOCO D   (linha de base, sem veredito)
  video gravado? sim / nao      MP inicial observado: ______ %

BLOCO E
  E1 [  ] E2 [  ] E3 [  ] E4 [  ] E5 [  ] E6 [  ] E7 [  ]

Log da sessao anexado?  sim / nao
```

---

## 9. Ordem e custo

| Ordem | Bloco | Custo | Por quê aqui |
|---|---|---|---|
| 1 | A1-A3 | 15 min | porteiro absoluto; o log responde sozinho |
| 2 | A7 metrônomo | 5 min | decide se qualquer medida de tempo vale |
| 3 | A4-A6, A8-A11 | 30 min | plataforma |
| 4 | B | 30 min | explica A7 se ele falhou |
| 5 | D | 20 min | **antes** de implementar A14-A18, senão o "antes" se perde |
| 6 | C | 90 min | o grosso; só com A5/A6/A7 verdes |
| 7 | E | 40 min | regressão, por último |

O Bloco D vem antes do C de propósito: ele é o único que **caduca**. Assim que
A16 entrar, a linha de base do modelo velho deixa de ser gravável para sempre.

---

# 10. Catálogo de itens — preservado do roteiro anterior

> Numeração original mantida. Os blocos do §3-§7 referenciam estes números.

# Roteiro adicional — entrega 2026-08-12 (nucleo de reacao a dano)

Cobre `AUDITORIA_2026-08-12.md`. Esta entrega mexe no que o jogo tem de mais
sensivel ao tato: quantos golpes derrubam, o que a defesa aguenta e qual
animacao de dano toca. Espere que a sensacao mude — a questao e' se mudou PARA
o original.

| # | Teste | Esperado |
|---|---|---|
| 29 | Dois socos fracos seguidos (fall 25) | **derruba no segundo**, nao no terceiro **[N]** |
| 30 | Socos espacados (1 s entre eles) | nao derruba: o contador decai 1/tick **[N]** |
| 31 | Golpe medio isolado (fall 45) | Dance of Pain, atordoamento longo **[N]** |
| 32 | Golpe de frente (fall 25) | animacao de dano **frontal** (222) **[N]** |
| 33 | Mesmo golpe **pelas costas** | animacao **de costas** (224) — antes vinha por intensidade **[N]** |
| 34 | Defender um soco fraco | perde ~1/10 do dano; antes era zero **[!]** |
| 35 | Defender golpes ate a guarda ceder | quebra por `bdefend` acumulado > 30, nao por um golpe pesado unico **[N]** |
| 36 | Defender, esperar, defender de novo | a guarda "recupera": bdefend decai 1/tick **[N]** |
| 37 | Acertar alguem no ar | derruba sempre, qualquer que seja o fall **[R]** |
| 38 | Acertar alguem congelado | estilhaça e tomba **[R]** |
| 39 | Partida completa contra 3 inimigos | dificuldade mudou, mas ninguem fica imortal nem preso **[!]** |

**Risco assumido:** o item 34 muda comportamento visivel — defender deixou de
ser gratuito. E o 29 torna combos mais letais. Os dois vem do assembly
(`0x0042ff6a` e `0x0042eb6c`), mas nunca rodaram no device.

**Nao implementado:** hitstop (`shaking`, achado A12). Se os golpes parecerem
"sem peso" comparados ao original, e' esse o motivo, e nao um bug desta entrega.

---

# Roteiro de teste no device — entrega 2026-07-30 (auditoria de assembly)

Cobre os seis achados da `AUDITORIA_2026-07-30.md`. Nada validado em hardware:
só suíte host (222 CHECKs), harness headless (dennis e henry) e compile-check.

Marque `OK` / `ERRO` + uma linha do que viu. **[N]** = comportamento novo ·
**[R]** = regressão que não pode ter voltado · **[!]** = risco assumido nesta
entrega, olhar com atenção redobrada.

```
cd /mnt/c/Users/rodrigo.chiesa/Documents/LittleFighter2Vita/build && cmake .. && make -j$(nproc)
```

---

## 1. Gate de `itr->effect` — A1 (lf2.exe 0x00417400)

| # | Teste | Esperado |
|---|---|---|
| 1 | Firen em corrida em chamas atravessa alvo **já queimando** | Firen **não** toma dano e **não** é interrompido **[N]** |
| 2 | Alvo parado dentro do fogo do chão do Firen | **desaba** ao fim da queimadura; não reacende em ciclo **[N]** — ⚠ a DURAÇÃO não é critério: `BURN_TICKS = 36` é Nível D, o candidato `+0xEA` foi refutado e como o original sai do state 18 segue em aberto (`AUDITORIA_SUPERFICIE.md` item 1). Anotar a duração observada, não comparar com 36 |
| 3 | Alvo queimando levando fogo de novo | HP **não** cai a cada nova chama **[N]** |
| 4 | Firen acerta fogo em quem **não** está queimando | acende normalmente **[R]** |
| 5 | Soco comum do Dennis (effect 0) | acerta normalmente **[R]** |
| 6 | Freeze: gelo em quem já está congelado (frames 200-202) | não reinicia a cadeia **[R]** |

## 2. Fogo no ar e frames 205/206 — A2 (0x0040e893)

| # | Teste | Esperado |
|---|---|---|
| 7 | Incendiar alvo em **pleno salto** | continua em chamas; a pose de pulo **não** apaga o fogo **[N]** |
| 8 | Observar a **descida** do alvo em chamas | troca para a pose horizontal (frames 205/206), sprite deitado **[N]** |
| 9 | Alvo em chamas **aterrissa** | continua queimando no chão até o fim do cronômetro **[N]** |
| 10 | Congelar alvo em pleno salto | idem: o gelo sobrevive ao voo e ao pouso **[N]** |
| 11 | Pulo comum, sem fogo/gelo | pose de pulo normal (210/211/212) **[R]** |

## 3. Knockback em re-acerto — A3 (0x0042ee5b)

| # | Teste | Esperado |
|---|---|---|
| 12 | Segundo projétil num alvo **já caindo** | o alvo é **empurrado na horizontal**, não só re-erguido no lugar **[N]** |
| 13 | Combo de socos em alvo em pé | tranco por faixa de FP, como antes **[R]** |
| 14 | Golpe pesado de primeira (fall 70) | arremesso longo, como antes **[R]** |

## 4. Bola perfurante — A4 (0x0042f0bf / 0x00430536)

| # | Teste | Esperado |
|---|---|---|
| 15 | Henry, tiro concentrado, 2+ inimigos **alinhados** | a flecha **atravessa** e acerta todos **[N]** |
| 16 | Mesmo tiro, alvo único | derruba de primeira, sempre **[N]** |
| 17 | Bola do Dennis / Davis (state 3000) | ainda **some** no primeiro corpo **[R]** |
| 18 | Flecha comum do Henry (arma leve, oid 201) | ainda krava/cai; não atravessa **[R]** |
| 19 | Vento do Henry (state 3005) | agora **atravessa**; conferir que não fica preso na tela **[!]** |
| 20 | Chamas do Firen (state 18) | agora **atravessam**; conferir que não causam dano contínuo absurdo **[!]** |

## 5. `arest` × `vrest` — A5 (0x0042f2c8)

| # | Teste | Esperado |
|---|---|---|
| 21 | Soco simples repetido no mesmo alvo | cadência de re-acerto **mais rápida** que antes (piso caiu de 8 para 4 TU) **[!]** |
| 22 | Golpe de rodopio / many_foot (tem `vrest`) | continua acertando várias vezes, cadência inalterada **[R]** |
| 23 | Flecha perfurante contra o **mesmo** alvo | não re-acerta antes de 15 TU **[N]** |
| 24 | Dois inimigos, golpe com `vrest` | travar um **não** impede acertar o outro **[N]** |

## 6. Regressões gerais **[R]**

| # | Teste | Esperado |
|---|---|---|
| 25 | Partida completa contra 3 inimigos | ninguém fica imortal, preso ou invisível |
| 26 | Pegar, usar e arremessar arma | sem mudança |
| 27 | Especiais de Firen, Henry, Dennis, Louis, Rudolf | disparam e causam dano |
| 28 | Sessão longa (5+ min) | sem engasgo, sem pool de objetos entupindo |

---

## Riscos assumidos nesta entrega (não são bugs a reportar, são o que vigiar)

**Piso do `arest` caiu de 8 para 4 TU** (itens 21). O 8 era invenção; o 4 vem do
assembly (`0x0042f2e4`). Efeito colateral: todo golpe **sem** `vrest` re-acerta
cerca de duas vezes mais rápido. É a mudança com maior impacto na sensação de
jogo desta entrega. Se os combos ficarem fáceis demais, é aqui.

**Objetos em state 18 / 3005 / 3006 deixaram de se gastar** (itens 19, 20). O
binário só manda ao frame `hiting` quem está em 3000. Chamas e vento agora
persistem — comportamento fiel, mas é a primeira vez que roda no device.

**Teto de `arest` em 12 NÃO foi aplicado** (achado A7, "não foi possível
comprovar"). 143 itrs usam `arest`, 51 deles com valor 15. Se o espaçamento de
algum golpe multi-hit parecer longo demais, é o candidato número um.

---

# Roteiro de teste no device — acumulado desde o último reteste

Cobre **tudo** que mudou desde o seu último retorno (o teste do modo auditoria que
reportou "especiais sem dano / imortal / dentro da pedra"). Nada aqui foi validado
em hardware: só suíte host (6 suítes) + harness headless + compile-check.

Marque `OK` / `ERRO` + uma linha do que viu. Itens **[N]** são comportamento novo;
**[R]** são regressões que não podem ter voltado.

```
cd /mnt/c/Users/rodrigo.chiesa/Documents/LittleFighter2Vita/build && cmake .. && make -j$(nproc)
```

---

## A. Efeitos de golpe — `itr.effect` **[N]**
Eram 450 itrs com efeito, ignorados por completo. Fogo e gelo chegavam como dano genérico.

| # | Teste | Esperado |
|---|---|---|
| 1 | Firen: acertar com fogo | alvo **pega fogo** (animação de chamas), não só perde HP |
| 2 | Deixar o alvo queimando | ele **desaba** ao fim (~1,2 s), não queima para sempre |
| 3 | Alvo com arma na mão leva fogo | **derruba a arma** |
| 4 | Freeze: acertar com gelo | alvo **congela** ~3 s, imóvel, e depois tomba |
| 5 | Alvo com arma leva gelo | **derruba a arma** |
| 6 | Acertar fogo em quem já queima | **não** reinicia a animação |
| 7 | Soco comum do Dennis | **sem** fogo/gelo |

## B. Reação a hit em 4 faixas **[N]**
Antes era sempre o mesmo tranco curto (frame 220).

| # | Teste | Esperado |
|---|---|---|
| 8 | 1 soco (fall 25) | tranco curto |
| 9 | 2 socos seguidos | tranco maior, ainda em pé |
| 10 | 3 socos seguidos | **tombo** |
| 11 | 1 flecha do Henry (fall 60) | **Dance of Pain**: atordoamento longo (~1 s parado) |
| 12 | Flecha concentrada / chute giratório (fall 70) | tombo **de primeira** |

## C. Alvo único **[N]**
Regra do original: golpe com `vrest 0` acerta **só o mais próximo**.

| # | Teste | Esperado |
|---|---|---|
| 13 | 2+ inimigos empilhados na mesma linha, um soco | acerta **só um** (o mais próximo) |
| 14 | Golpe de rodopio (tem `vrest`) na mesma pilha | acerta **vários** |

## D. Tipos de `itr` que passaram a causar dano **[N]**

| # | Teste | Esperado |
|---|---|---|
| 15 | Henry: flauta | causa dano — **inclusive em quem está caindo** |
| 16 | Freeze: coluna de gelo | causa dano |

## E. Estados hardcoded por personagem **[N]**
Estados ≥100 eram atropelados pela pose de pulo (era o seu bug do Louis).

| # | Teste | Esperado |
|---|---|---|
| 17 | **Louis: correr + pular + atacar** | investida longa (~400 px) que **completa**; não trava no meio |
| 18 | Woody: teleporte (`hit_Uj`/`hit_Dj`) | animação roda até o fim (o deslocamento em si ainda não é emulado) |
| 19 | Rudolf: transformação | frames rodam sem travar |
| 20 | Deep (se habilitar no roster) | `dash_sword` completa |

## F. Códigos `next` especiais **[N]**

| # | Teste | Esperado |
|---|---|---|
| 21 | Rudolf `hit_Uj` (custa 350 MP) | **desaparece ~5 s, invulnerável**; sombra pisca antes de voltar |
| 22 | Bater nele enquanto invisível | **nenhum dano** |
| 23 | Louis c-throw | ele **inverte o lado** ao fim do golpe |

## G. Física de arma **[N]**
`WEAPON_FLY_GRAVITY` = **0.425 = 1.7/4**, lido do pool de constantes do `lf2.exe`.

| # | Teste | Esperado |
|---|---|---|
| 24 | Flecha do Henry | ~420 px antes de cravar (era 396 com o valor antigo) |
| 25 | Pedra arremessada | arco mais baixo, sem subir demais |

## H. Arma no chão **[N]**
Regra do original: `on_ground_state_1` (arma leve) é imune a mão vazia.

| # | Teste | Esperado |
|---|---|---|
| 26 | Socar a **faca** no chão de mão vazia | **não** quebra |
| 27 | Socar a **pedra/caixa** de mão vazia | quebra (leva vários golpes, 800 HP) |
| 28 | Bater na faca **com arma na mão** | quebra |
| 29 | Ao quebrar | aparece o estilhaço animado, e ele **some** |

## I. Cenário e colisão **[R]**

| # | Teste | Esperado |
|---|---|---|
| 30 | Andar até as duas pontas da fase | céu e montanhas cobrem a tela toda |
| 31 | Parallax | fundo lento, chão 1:1 |
| 32 | Fogo do Firen no chão | apaga em ~3 s, não fica para sempre |
| 33 | **Correndo** contra a pedra | **não atravessa** (colisão varrida em substeps) |
| 34 | Andando contra a pedra | bloqueia |
| 35 | Pular sobre a pedra | passa por cima |
| 36 | Ser jogado para dentro da pedra | consegue **sair andando** (não fica preso) |

## J. Modo auditoria — SELECT **[N]**

| # | Teste | Esperado |
|---|---|---|
| 37 | Apertar SELECT em partida | faixa amarela no topo do HUD |
| 38 | Alvos | posicionados a ~110/260/430 px, virados para você, parados |
| 39 | Cada ~1,7 s | sai o próximo golpe do ciclo `Fa→Ua→Da→Fj→Uj→Dj` |
| 40 | Barras de vida | **mexem** (o dano é visível; não há mais imortalidade) |
| 41 | Ao morrer | revive com HP cheio, a sessão não acaba |
| 42 | Alvos | ficam **em pé** entre golpes (não acumulam knockdown) |
| 43 | Percorrer os 8 do roster | anotar quais golpes não saem ou não causam dano |

## K. Regressões gerais **[R]**

| # | Teste | Esperado |
|---|---|---|
| 44 | Henry: 25+ flechas seguidas | continuam saindo (pool não enche) |
| 45 | Flechas/shurikens no chão | somem sozinhas em ~3 s |
| 46 | Faca e pedra do cenário | **não** somem |
| 47 | Fumaça (Rudolf/Louis/Henry) | some sozinha; **não** pode ser pega |
| 48 | Ninguém é empurrado ao pegar/arremessar | — |
| 49 | Ataque sem direcional com pedra na mão | arremessa na direção que olha |
| 50 | Faca sem direcional | golpeia; com direcional, arremessa |
| 51 | Carregando pedra | corre mais devagar; **não** pula |
| 52 | Correndo + Ataque perto da pedra | sai **Ataque Correndo**, não pega o item |
| 53 | Nenhum personagem desliza durante ataques | — |
| 54 | Nenhum sprite congelado na tela ao fim da partida | — |
| 55 | Primeiro especial de cada personagem | sem engasgo (assets pré-carregados) |

---

## Pontos que eu já sei que estão incompletos (não reportar como bug)

- Woody/Rudolf: os frames de teleporte e transformação **rodam**, mas o efeito
  (deslocar, trocar de personagem) não é emulado — depende de trocar o `.dat` em runtime.
- Agarrão (cpoint) não existe: `itr kind 1/3` não agarram.
- `itr kind 4` (thrown) inerte — depende do agarrão.
- Sem áudio.
- Só a fase Lion Forest; `stage.dat` (waves/bosses) não existe.
- Quebra de guarda usa `fall >= 60` em vez de acumular `bdefend`.
- Sem hitstop.
- Saída da investida do Louis (state 100) é **inferência** — nenhuma das três
  fontes emula esse estado. Se o alcance parecer errado, é o candidato a ajustar.
