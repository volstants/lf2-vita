# Auditoria de superfície não verificada · 2026-08-12

Motivação: os achados A8-A11 fecharam quatro parâmetros que vinham do F.LF ou de
invenção própria. **Os quatro estavam errados.** Vale olhar o que ainda está na
mesma condição, antes que apareça como bug no device.

## A taxa-base, que é o dado mais importante deste documento

Todo parâmetro do porte que veio do F.LF, de documentação de comunidade ou de
invenção, e que foi **depois** conferido contra o assembly, mostrou-se errado:

| Parâmetro | Fonte original | Veredito no assembly |
|---|---|---|
| gravidade de arma em voo `0.45` | calibrado por captura de tela | **errado** → `0.425` (`0x48358`) |
| decaimento de fall `0.45`/TU | F.LF `recover.fall` | **errado** → `1`/tick (`0x40da15`) |
| limiar de tombo `fp > 60` | F.LF | **errado** → satura no piso da faixa (`0x42eb6c`) |
| frames 222/224 por faixa de fp | F.LF `character.js:1676` | **errado** → por facing (`0x42ebcb`) |
| quebra de guarda por `fall >= 60` | invenção | **errado** → `bdefend > 30` (`0x4300d2`) |
| golpe defendido = 0 de dano | invenção | **errado** → `injury/10` (`0x42ff6a`) |
| `arest`/`vrest` fundidos, default 8 | invenção | **errado** → campos distintos, piso 4 (`0x42f2e4`) |
| knockback zerado em re-acerto | invenção | **errado** → `dvx` sempre (`0x42ee5b`) |
| frame de queda fixo em 180 | invenção | **errado** → 180-183 por vy (`0x40e242`) |

**Nove de nove.** Isso não é azar: é o que acontece quando uma reimplementação
clean-room (F.LF) e a intuição de quem escreve o porte são tratadas como fonte.
A conclusão prática é que **qualquer valor abaixo que ainda esteja sem endereço
do binário deve ser presumido errado até prova em contrário**, e não o inverso.

---

## O que ainda não tem evidência de Nível A

Ordenado por risco = (probabilidade de estar errado) × (impacto se estiver).

### 1. `BURN_TICKS = 36` — duração da queimadura · **RISCO ALTO**

`player.hpp`. Atribuído ao F.LF ("locks frame 203 for 36 TU"). É o mesmo tipo de
constante de tempo que o `0.45` do decaimento — e aquele estava errado.

O binário tem o cronômetro em algum lugar: o `fall`/`bdefend` decaem em
`0x0040da15`-`0x0040da3a`, e logo abaixo há `0x0040da3b: mov 0xea(%esi),%al` com
decremento de byte. `+0xEA` é candidato a contador de queimadura. **Não
verificado.**

### 2. `launch(-8.f)` e `vy = -6.f` — velocidade do tombo · **RISCO ALTO**

`player.hpp::hit()`. Ambos inventados. O `-8.0` coincide com a constante
`0x448340`, mas ali ela é **limiar de seleção de frame**, não velocidade de
lançamento — a coincidência é enganosa e não serve de evidência.

O pool de constantes tem `±6.0` (`0x447940`/`0x449af0`) e `±3.0`
(`0x447a40`/`0x449050`), ambos referenciados no bloco de acerto. São candidatos.
**Não verificado.**

### 3. `FRICTION = 1.0` e `MIN_SPEED = 1.0` · **RISCO MÉDIO**

`types.hpp`, ambos do F.LF (`ps.fric = 1`, `GC.min_speed = 1`). Afetam toda a
locomoção. Valores redondos e plausíveis, mas foi exatamente isso que se disse do
`0.45`.

### 4. `mp = 200` no início · **RISCO MÉDIO**

`fighter.hpp:177`, F.LF `global.js: mp_start = 200`. Afeta quantos especiais o
jogador tem no primeiro minuto. Fácil de verificar (é um `movl` na inicialização
do objeto) e ninguém verificou.

### 5. Regeneração de MP — 1 a cada 2 ticks · **RISCO MÉDIO**

`player.hpp::tickInner`, `(++mpRegenAcc & 1) == 0`. Sem fonte declarada nenhuma —
nem F.LF. É invenção pura, e governa o ritmo de especiais do jogo inteiro.

### 6. `launch(-6.f)` na promoção de queda · **RISCO BAIXO, mas é INFERÊNCIA declarada**

`player.hpp:371`, com o comentário admitindo "tuning constant with no source".
Honesto, mas continua sendo um número inventado no caminho de tombo.

### 7. `Z_MIN = 365`, `Z_MAX = 505` · **RISCO BAIXO** — investigado, segue aberto

`types.hpp`. O `bg.dat` traz `zboundary`; se o porte usa a constante em vez do
dado, cada fase com limite diferente fica errada.

Investigado em 2026-08-12: o pool tem `580.0`/`-200.0`/`300.0` em
`0x47938`/`0x47930`/`0x47928`, que pareciam candidatos a limite. **Não são** —
`0x0040689e`-`0x004068d0` os grava em `+0x58`/`+0x60`/`+0x68`
(`x_position`/`y_position`/`z_position`), ou seja, é a posição inicial de spawn.
O limite de `z` continua sem endereço.

### 8. `TICK_MS = 33` (30 Hz) · **RISCO BAIXO**

Bem estabelecido e coerente com todos os `wait` dos `.dat`. Mas nunca foi lido do
binário, e o timer do original é o candidato natural a um `SetTimer`/`timeGetTime`
verificável.

### 9. IDs canônicos de frame (0, 5, 9, 60, 110, 180, 220…) · **RISCO MUITO BAIXO**

`player.hpp::fid`. Marcados como "convenção da comunidade de 20 anos", mas na
prática já foram validados de forma cruzada: o assembly grava literalmente `0x6e`
(110), `0x6f` (111), `0x70` (112), `0xdc` (220), `0xde` (222), `0xe2` (226),
`0xb4`-`0xb7` (180-183), `0xcd` (205). Estes deixaram de ser convenção e passaram
a ter endereço.

### 10. Anti-juggle `fall <= 40` · **RISCO MÉDIO**

`main.cpp::itrDeals` e os quatro caminhos. Veio do OpenLF2 (`class_global.c:178`).
Nível C, nunca confirmado no assembly — e o OpenLF2 já errou constantes uma vez
(effect 20/21).

---

## Erro de processo, não de valor

Além dos parâmetros, dois padrões que produziram erro nesta sessão e vão produzir
de novo:

**Corroboração falsa.** Uma evidência que não sustenta a conclusão, apresentada
como se sustentasse, é pior que nenhuma evidência — faz a conclusão parecer
validada de forma independente. Aconteceu duas vezes hoje, ambas com fontes
externas afirmando "confirma o que vocês acharam" sobre coisas diferentes das que
achamos.

**Verificação que não verifica.** Passei comando de conferência (`ls assets |
wc -l`) baseado numa premissa que eu não havia checado, e o comando confirmaria a
premissa errada. Custou o `assets/`. Quando dá para verificar direto, verificar
direto — não delegar a checagem de uma suposição própria.

---

## Ordem sugerida

| # | Item | Custo | Por quê nesta ordem |
|---|---|---|---|
| 1 | `BURN_TICKS` (`+0xEA`) | baixo | candidato já localizado, mesma família dos que erraram |
| 2 | Velocidades de tombo | médio | governam a sensação de todo knockdown |
| 3 | `mp_start` e regen de MP | baixo | um é F.LF, o outro é invenção pura sem fonte |
| 4 | Anti-juggle `fall <= 40` | baixo | Nível C de uma fonte que já errou |
| 5 | `FRICTION`/`MIN_SPEED` | médio | afeta locomoção inteira |
| 6 | `Z_MIN`/`Z_MAX` vs `bg.dat` | baixo | pode já estar certo; é só confirmar quem manda |

Nada disso é bug conhecido. É **superfície não verificada** — que, pela taxa-base
acima, é onde os próximos bugs estão.
