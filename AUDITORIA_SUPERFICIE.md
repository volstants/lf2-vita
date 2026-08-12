# Auditoria de superfície não verificada · 2026-08-12
> **Segunda passada, mesma data.** Os itens 2, 4 e 5 foram fechados com
> evidência de Nível A (achados A14-A18). A taxa-base subiu de 9/9 para 13/13.

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
| `launch(-8.f)` no tombo | invenção | **errado** → `-7.0` só como padrão, e via acumulador (`0x42f215`) |
| `vy = -6.f` no tombo | invenção | **errado** → não existe; o `itr->dvy` manda (`0x42f1cd`) |
| `mp_start = 200` | F.LF `global.js` | **errado** → `500` (`0x4063e1`) |
| regen de MP `1` a cada 2 ticks | invenção | **errado** → `(500-hp)/100 + 1` **por tick** (`0x41faf2`) |

**Treze de treze.** Isso não é azar: é o que acontece quando uma reimplementação
clean-room (F.LF) e a intuição de quem escreve o porte são tratadas como fonte.
As quatro linhas novas saíram desta segunda passada, e três delas erraram por
**mais** do que um valor: erraram o mecanismo. Não existe constante de
lançamento; existe acumulador com commit dividido. A regeneração de MP não é
uma taxa fixa; é função do dano sofrido.
A conclusão prática é que **qualquer valor abaixo que ainda esteja sem endereço
do binário deve ser presumido errado até prova em contrário**, e não o inverso.

---

## O que ainda não tem evidência de Nível A

Ordenado por risco = (probabilidade de estar errado) × (impacto se estiver).

### 1. `BURN_TICKS = 36` — **CANDIDATO `+0xEA` REFUTADO; questão maior em aberto**

`player.hpp`. Atribuído ao F.LF ("locks frame 203 for 36 TU").

**O candidato que este documento sugeriu estava errado.** Busca exaustiva das
referências a `+0xEA` no `.text` — cinco, todas mapeadas:

| Endereço | O que faz |
|---|---|
| `0x00406357` | `mov %bl,0xea(%esi)` — zero-init de construtor (`xor %ebx,%ebx` em `0x004061e0`) |
| `0x0040da3b` / `0x0040da47` | leitura e decremento no laço de decaimento por tick |
| `0x00413620` | `cmpb $0x0,0xea(%esi)` — teste |
| `0x0043057a` | `movb $0x3,0xea(%edx)` — grava **3**, num ramo com discriminador `== 6` |

Nenhuma escrita usa 36; o maior valor gravado é 3. `+0xEA` **não** é o
cronômetro de queimadura.

**Erro de método, e é o mesmo que este documento descreve.** Apontei `+0xEA`
porque ele decai perto do `fall` e do `bdefend` — proximidade de vizinhança
tratada como confirmação, exatamente o vício listado na seção "Erro de processo"
abaixo. O item nasceu viciado.

**O que a investigação seguinte revelou.** O sítio que acende a queimadura é
`0x0042fd76`, e ele **não grava cronômetro nenhum**:

```
42fd69:  cmpl $0x0,0x6f8(%edx)   ; vítima é personagem?
42fd76:  movl $0xcb,0x70(%eax)   ; frame_id = 203
42fd84:  movl $0x0,0x88(%ecx)    ; frame_wait = 0
42fd98:  push $0x10 ; call 0x417090   ; efeito/som, id 16
42fda9:  fcoml 0x28(%ecx)        ; compara 0.0 com x_velocity
42fdb6:  movb $0x1,0x80(%ecx)    ; …e ajusta o facing pela direção do empurrão
```

Frame, wait, som e facing. Nada de duração.

Isso levanta uma hipótese mais forte que a original: **pode não existir contador
de queimadura no engine.** A saída dos frames 203-206 seria estrutural — cadeia
de frames mais algum outro mecanismo — e não uma contagem regressiva. Se for o
caso, `BURN_TICKS = 36` não está "com valor errado": está modelando algo que o
original não tem.

**Estado:** o candidato está refutado com busca exaustiva. A pergunta de fundo —
como o original sai do state 18 — segue **sem resposta** e não deve ser fechada
por analogia.

### 2. ~~`launch(-8.f)` e `vy = -6.f` — velocidade do tombo~~ · **FECHADO — A14/A16**

Os dois estavam errados, e nenhum dos candidatos do pool era o certo. `±6.0` e
`±3.0` **não** entram no tombo. O `-8.0` de `0x448340` era mesmo coincidência:
confirmado nesta sessão como limiar de seleção dos frames 180-183 em
`0x0040e7c1`.

O que o engine faz (A14, A16):

- o impulso vertical do tombo é `itr->dvy`, e **`-7.0`** (`0x00447a50`) só
  quando o `itr` não traz `dvy`;
- ele não vai para `y_velocity`, vai para um **acumulador** em `+0x30`;
- um passo separado por tick, `FUN_004196f0`, faz
  `y_velocity = (acumulado × 2.0) / (golpes_do_tick + 1)`;
- com um só golpe a conta dá exatamente o acumulado, então **`-7.0` é o número
  do caso simples** — mas o mecanismo não é uma constante de lançamento;
- `shaking` adia esse commit (A17): o empurrão sai **depois** do hitstop.

Sobra em aberto, e com endereço: reverter a semântica de `+0x340` (divisor de
dano, `0x0042e8c6`) e confirmar se `dvy` nos `.dat` é negativo para cima.

### 3. `FRICTION = 1.0` e `MIN_SPEED = 1.0` · **RISCO MÉDIO**

`types.hpp`, ambos do F.LF (`ps.fric = 1`, `GC.min_speed = 1`). Afetam toda a
locomoção. Valores redondos e plausíveis, mas foi exatamente isso que se disse do
`0.45`.

### 4. ~~`mp = 200` no início~~ · **FECHADO — A18**

É **500** (`0x004063ca`-`0x004063e1`, o mesmo `movl $0x1f4` que carrega `hp`,
`hp` máximo e o teto recuperável). O campo também estava mal endereçado no
raciocínio anterior: `mp` é `+0x308`, não `+0x300` — `+0x300` é o teto
recuperável, que cai `dano/3` por golpe (`0x0042e97d`).

### 5. ~~Regeneração de MP — 1 a cada 2 ticks~~ · **FECHADO — A18**

O engine regenera **todo tick**, e a taxa cresce com o dano sofrido:
`mp += (500 − min(hp,500)) / 100 + 1` (`0x0041fa90`-`0x0041faf2`). De `+1`/tick
com HP cheio a `+6`/tick com HP zerado. Os dados de `id` 51 e 52 têm o `hp`
dividido por 2 antes da conta.

O porte estava entre 2× e 12× lento, dependendo do HP. Era o item de menor
custo de verificação da lista e o de maior erro medido.

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

### 10. Fonte de aleatoriedade — **AUSENTE NO PORTE** · resolvido no engine, aberto no porte

Item novo, 2026-08-12. Não é "valor sem evidência": é **mecanismo inteiro sem
contrapartida**. O engine tem 264 pontos de decisão aleatória; o porte tem zero
(varredura por `rand(`, `srand`, `mt19937`, `random_device`, `uniform_int` em
`src/`, `tools/`, `tests/` retorna nada).

O lado do engine está **fechado com evidência de Nível A** — tabela de 3000
bytes em `0x0044FF90`, consumidor `FUN_00417170`, cursores `0x00450C34` (mod
1234) e `0x00450BCC` (mod 3000). Ver `AUDITORIA_2026-08-12.md#a13`.

O que segue aberto é do nosso lado, e são duas perguntas distintas:

1. **Quantos dos 264 sítios caem em código que o porte já executa?** Sabemos que
   5 estão em `FUN_0042e100`, a rotina de aplicação de acerto que já auditamos, e
   que 0 estão no update por tick e 0 em `does_attack_success`. Os outros 259 não
   foram mapeados.
2. **O que cada sítio decide?** Nenhum foi reconstruído.

O risco aqui não é errar um número — é implementar qualquer comportamento
ramificado com `<random>` e nascer divergente por construção, como já aconteceu
nove vezes com parâmetros. Quando a IA de inimigo entrar, `engine_random` tem que
entrar antes.

### 11. Anti-juggle `fall <= 40` · **RISCO MÉDIO** — parcialmente encostado

`main.cpp::itrDeals` e os quatro caminhos. Veio do OpenLF2 (`class_global.c:178`).
Nível C, nunca confirmado no assembly — e o OpenLF2 já errou constantes uma vez
(effect 20/21).

**Encostado por A14 e A16, sem ser resolvido.** O `40` existe no binário, mas
num papel diferente do que o porte usa: em `0x0042f1bc` e `0x0042f205`,
`itr->fall <= 40` cancela o impulso vertical **quando a vítima é `file->type` 2
ou 3** — arma pesada ou objeto de efeito, não personagem. E existe um
anti-juggle real que o porte não tem: a divisão por `golpes_do_tick + 1` no
commit (A16). Nenhum dos dois é o `fall <= 40` do `itrDeals`. O item continua
Nível C.

### 12. Nomes de campo do OpenLF2 para `+0x28`/`+0x30`/`+0x38` · **REFUTADOS**

Item novo. `object.h` do OpenLF2 chama `+0x28` de `pic_x_gain`, `+0x30` de
`y_accl` e `+0x38` de `z_accl`. A16 prova que os três são o **acumulador de
empurrão** x/y/z, pareado com `+0x40`/`+0x48`/`+0x50`.

Isso não derruba o OpenLF2 como pedra de roseta — os nomes que ele deu a
`+0x40`, `+0x48`, `+0x50`, `+0x58`, `+0x60`, `+0x68`, `+0xB0`, `+0xB4`, `+0xB8`
continuam batendo com o assembly. Mas confirma o que a hierarquia já dizia:
**Nível C é bom para nome, não para semântica.** Onde ele escreveu `_unknown`
ou chutou, o chute vale zero.

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
| 1 | ~~`BURN_TICKS` (`+0xEA`)~~ | — | **refutado**; a saída do state 18 vira pergunta aberta, custo agora ALTO |
| 2 | ~~Velocidades de tombo~~ | — | **fechado**, A14/A16 |
| 3 | ~~`mp_start` e regen de MP~~ | — | **fechado**, A18 |
| 4 | Anti-juggle `fall <= 40` | baixo | Nível C; A16 mostra que o anti-juggle real é outro |
| 5 | `FRICTION`/`MIN_SPEED` | médio | afeta locomoção inteira |
| 6 | `Z_MIN`/`Z_MAX` vs `bg.dat` | baixo | pode já estar certo; é só confirmar quem manda |
| — | `engine_random` (item 10) | baixo p/ implementar | mas **antes** de qualquer comportamento ramificado, não depois |

**O que entrou na fila, vindo de A14-A18** — não é superfície não verificada, é
dívida de implementação com evidência pronta:

| Item | Achado | Por que importa |
|---|---|---|
| Acumulador + commit dividido | A16 | é o mecanismo inteiro do empurrão; o porte não tem |
| Hitstop `shaking` | A17 | congela o objeto e adia o empurrão; muda o timing de tudo |
| `mp = 500` e regen por HP | A18 | um `movl` e cinco linhas; o de menor custo |
| Impulso `-7.0` / `itr->dvy` | A14 | depende do acumulador estar em pé antes |
| Frame 180 vs 186 por direção | A15 | barato depois de A9, é o mesmo padrão |
| `+0x340` como divisor de dano | A19 | campo novo, semântica ainda não revertida |

Nada disso é bug conhecido. É **superfície não verificada** — que, pela taxa-base
acima, é onde os próximos bugs estão.
