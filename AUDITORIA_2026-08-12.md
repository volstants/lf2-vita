# Auditoria de fidelidade — `lf2.exe` · 2026-08-12

> **Identificação do binário de referência.** `reference/decomp/lf2.exe` é o
> **LF2 v2.0a**, build de **2009-07-10 17:15:35 UTC** (`TimeDateStamp` =
> `0x4a577737` no cabeçalho PE), SHA256
> `12dfa00f6b767508612550e9ab27ab74b4201ff4cb9ff31d068925924eab8fc5`.
> PE32 i386, MSVC 8.0, sem packer.
>
> **Confirmação pelo Rich header (2026-08-12).** O Rich header está presente e
> íntegro (chave XOR `0x418e5e9a`) — só isso já é evidência de que o binário não
> foi empacotado nem reescrito, porque packer costuma destruí-lo. Ele traz a
> versão de cada ferramenta que produziu objeto:
>
> | prodID | build | objetos |
> |---|---|---|
> | 110 | 50727 | 30 |
> | 123 | 50727 | 26 |
> | 109 | 50727 | 22 |
> | 28 | 9466 | 13 |
> | 125 | 50727 | 7 |
> | 93 | 4035 | 5 |
> | 28 | 9178 | 1 |
> | 14 | 7299 | 1 |
> | 120/124/126 | 50727/50327 | 1 cada |
> | 1 | 0 | 200 |
>
> **Build 50727 = 14.00.50727 = Visual Studio 2005 SP1.** Isso confirma "MSVC
> 8.0" por uma via independente do manifesto e do campo de versão do linker, e
> com mais precisão: é o SP1, não o RTM.
>
> Dois fatos secundários, ambos com consequência: (a) há objetos de toolchains
> claramente mais antigas — builds `9466`, `9178`, `7299`, `4035` — o que indica
> bibliotecas estáticas ou objetos legados relinkados, e explica por que o estilo
> de código não é uniforme ao longo do `.text`; (b) `prodID 1, build 0, count
> 200` é a contagem de imports.
>
> **O mapeamento de `prodID` para nome de ferramenta não foi verificado** e não
> está sendo afirmado aqui. A tabela pública associa a faixa 109-126 às
> ferramentas do VS2005 (compilador C, C++, e variantes de LTCG), mas não tenho
> essa tabela como fonte de Nível A. O que está provado é o número de build.
>
> **Diretório de debug: ausente** (RVA 0, tamanho 0). Não há entrada CodeView,
> logo não há caminho de PDB nem nomes de arquivo-fonte a extrair. Essa porta
> está fechada, e vale registrar para ninguém tentar de novo.
>
> **Não** é o build de 1999 — v2.0a é o último
> lançamento oficial e é o que a comunidade chama de "LF2 original" hoje, mas a
> distinção importa: onde estes documentos dizem "o original", leia-se
> "LF2 v2.0a". Os `.dat` interpretados vêm da mesma instalação.

Metodologia S. Sessão dedicada ao **núcleo de reação a dano**: `fall`,
`bdefend` e `shaking`. Os três são campos do objeto que o porte nunca lia — o
modelo em uso vinha do F.LF e de invenção própria.

Offsets novos confirmados nesta sessão (nomes via OpenLF2 `object.h`, Nível C;
uso confirmado no assembly):

| Offset | Campo |
|---|---|
| `+0x14` | `y` — negativo = fora do chão |
| `+0x20` | `attacks` |
| `+0xB0` | `fall` — falling points |
| `+0xB4` | `shaking` — hitstop |
| `+0xB8` | `bdefend` — resistência de guarda |
| `+0x2FC` | `hp` |
| `+0x31C` | `drink_hp` |
| `itr+0x1C` | `itr->fall` |
| `itr+0x40` | `itr->bdefend` |
| `itr+0x44` | `itr->injury` |

---

## A8 — `fall` satura no piso da faixa; o default dispara em `itr->fall == 0`

**Severidade:** Fidelidade · Observável

### Comportamento do engine

`FUN_0042e100`, bloco `0x0042ea8c`-`0x0042ec33`.

Acumulação:

```
42ea8c:  mov  0x1c(%ecx),%ecx      ; itr->fall
42ea8f:  test %ecx,%ecx
42ea98:  jne  0x42eaa3
42ea9a:  addl $0x14,0xb0(%eax)     ; itr->fall == 0 → fall += 20
42eaa1:  jmp  0x42eaa9
42eaa3:  add  %ecx,0xb0(%eax)      ; senão fall += itr->fall
```

Tombo forçado (`0x0042eabf`-`0x0042eb02`) quando a vítima está em state 13
(gelo, lido de `frame_id3`), state 12 (caindo, lido de `frame_id4`), ou é
`file->type` 1/2/4/6 (armas e bebida) — nesses casos `fall = 80`.

Seleção da reação, **com reescrita do contador**:

```
42eb1a:  mov  0xb0(%eax),%ecx
42eb20:  cmp  $0x3c,%ecx ; jle    → > 60 : fall = 80 (tomba)
42eb43:  cmp  $0x28,%ecx ; jle    → > 40 : frame 226 (DoP), fall = 60
42eb98:  cmp  $0x14,%ecx ; jle    → > 20 : frame 222/224,   fall = 40
42ec01:  test %ecx,%ecx  ; jle    → >  0 : frame 220,       fall = 20
```

O ponto central: `fall` **não** é um acumulador livre. Escolhida a reação, o
engine reescreve o contador para o piso daquela faixa (`0x0042eb6c`,
`0x0042ebdc`, `0x0042ec29`). É isso que faz um segundo golpe fraco derrubar —
depois do primeiro, o contador já está no piso da faixa, não no valor somado.

`80` (`0x50`) não é teto: é marcador de "vai tombar", testado depois por
igualdade (`0x00430102: cmpl $0x50,0xb0(%edx)`).

Estar no ar força o tombo em qualquer faixa (`0x0042eb7d`, `0x0042ebed`,
`0x0042ec3a`: `cmpl $0x0,0x14(%edx) ; jge`), e `hp <= 0` também
(`0x00430060`-`0x00430069`).

### Divergência encontrada

O porte acumulava `fp` sem saturar, usava limiar 60 para tombo e 40 para Dance
of Pain, e tratava o default de `fall` como "não informado" (`< 0`) em vez de
`== 0`.

### Conclusão

**Divergente do LF2 original.**

---

## A9 — A escolha entre os frames 222 e 224 é pelo FACING

**Severidade:** Fidelidade · Observável

### Comportamento do engine

Dentro da faixa `fall > 20` (`0x0042ebac`-`0x0042ebd2`):

```
42ebac:  mov  0x80(%eax),%dl        ; facing da VÍTIMA
42ebbb:  cmp  0x80(%ecx),%dl        ; == facing do ATACANTE ?
42ebc8:  sete %al
42ebcb:  lea  0xde(%eax,%eax,1),%eax ; 222 + 2*al
42ebd2:  mov  %eax,0x70(%ecx)
```

`0xde` = 222. Facings **iguais** — atacante e vítima olhando para o mesmo lado,
isto é, golpe pelas costas — dá 224. Facings **opostos**, golpe de frente, dá
222. Não há relação com a magnitude do `fall`.

### Divergência encontrada

O porte escolhia 222 para `fp` 21-30 e 224 para 31-40, faixas vindas do F.LF.
O eixo estava errado: é direção, não intensidade.

### Conclusão

**Divergente do LF2 original.**

---

## A10 — `bdefend` acumula na vítima; guarda quebra acima de 30; defender custa `injury/10`

**Severidade:** Fidelidade · Observável

### Comportamento do engine

Acumulação e quebra (`0x0043008b`-`0x004300f4`):

```
43008b:  mov  0x40(%ecx),%edx      ; itr->bdefend
43008e:  add  %edx,0xb8(%eax)      ; vitima->bdefend += itr->bdefend
4300d2:  cmpl $0x1e,0xb8(%eax)     ; > 30 ?
4300d9:  jle  0x4300ee
4300df:  cmpl $0x7,0x8(%edx)       ; e em state 7 (defendendo)
4300e5:  movl $0x70,0x70(%eax)     ; → frame 112 (broken_defend)
4300ee:  cmpl $0x6e,0x70(%eax)     ; senão, se frame == 110
4300f4:  movl $0x6f,0x70(%eax)     ; → frame 111 (recuo de guarda)
```

Custo do golpe defendido (`0x0042ff52`-`0x0042ff6a`, ramo que desemboca no
bloco acima):

```
42ff52:  mov  $0x66666667,%eax
42ff57:  imul %ecx                 ; ecx = injury
42ff60:  sar  $0x2,%edx
42ff65:  shr  $0x1f,%ecx
42ff68:  add  %edx,%ecx            ; == injury / 10
42ff6a:  sub  %ecx,0x2fc(%eax)     ; vitima->hp -= injury/10
```

### Divergência encontrada

O porte decidia quebra de guarda por `itr->fall >= 60` e devolvia **zero** dano
para golpe leve bloqueado e `dmg/2` para pesado. As três coisas eram invenção;
o campo `itr->bdefend` era parseado e nunca lido.

### Conclusão

**Divergente do LF2 original.**

---

## A11 — `fall` e `bdefend` decaem 1 por tick

**Severidade:** Fidelidade · Latente

### Comportamento do engine

No update por tick do objeto (`0x0040da15`-`0x0040da3a`), com `%edi` zerado por
`xor %edi,%edi` em `0x0040d964`:

```
40da15:  mov  0xb0(%esi),%eax
40da1b:  cmp  %edi,%eax
40da1d:  jle  0x40da28
40da1f:  add  $0xffffffff,%eax     ; fall -= 1
40da22:  mov  %eax,0xb0(%esi)
40da28:  (idêntico para 0xb8 — bdefend -= 1)
```

### Divergência encontrada

O porte decaía `fp` em 0.45 por TU, valor do F.LF, acumulado em centésimos. O
binário decrementa 1 inteiro.

### Conclusão

**Divergente do LF2 original.**

---

## A12 — `shaking` (hitstop) — **NÃO IMPLEMENTADO**

**Severidade:** Fidelidade · Latente

### Comportamento do engine

No mesmo bloco de aplicação de acerto (`0x004300a6`-`0x004300be`):

```
4300a6:  movl $0x3,0xb4(%eax)         ; ATACANTE ->shaking = +3
4300b7:  movl $0xfffffffb,0xb4(%edx)  ; VÍTIMA   ->shaking = -5
```

O campo decai por tick como os outros (`0x0040d966` compara `0xb4` com zero).

### Divergência encontrada

O porte não possui hitstop nenhum.

### Conclusão

~~**Não foi possível comprovar** o efeito.~~ **SUPERADO POR A17.** Os três
consumidores foram isolados: `FUN_0040e490` congela a atualização inteira do
objeto, `FUN_004196f0` adia o commit do empurrão, e `0x0040de38` desloca o
desenho só quando o valor é negativo. A leitura original — "pausa de animação
de um e tremor lateral do outro" — estava certa no palpite e continua não sendo
evidência; o que vale é A17, que tem endereço para cada elo.

---

## A13 — A aleatoriedade do engine é uma TABELA de 3000 bytes, não `rand()`

**Severidade:** Fidelidade · Latente

### Comportamento do engine

`srand` é importado de `MSVCR80.dll` e chamado **uma única vez** em todo o
binário, em `0x0043cf5a`, semeado com `timeGetTime()`:

```
43cf45:  mov  0x447250,%edi       ; IAT: timeGetTime
43cf55:  call *%edi
43cf57:  mov  %eax,%esi
43cf59:  push %esi
43cf5a:  call *0x4470ec           ; IAT: srand
```

`rand` é chamado em apenas **quatro** sítios: `0x414774` (`% 25 + 'A'`, geração
de nome), `0x431a42` e `0x431a86` (`% 9 - 4`, dispersão de posição de efeito) e
`0x422ad0`, dentro de `FUN_00422ac0`, que preenche uma tabela e retorna:

```
422ac0:  push %esi ; push %edi
422ac2:  mov  0x447198,%edi        ; IAT: rand
422ac8:  xor  %esi,%esi
422ad0:  call *%edi
422ad2:  cltd
422ad3:  mov  $0xff,%ecx
422ad8:  idiv %ecx                 ; edx = rand() % 255
422ada:  add  $0x1,%esi
422add:  add  $0x1,%dl             ; dl  = (rand() % 255) + 1
422ae0:  cmp  $0xbb8,%esi
422ae6:  mov  %dl,0x44ff8f(%esi)   ; tabela[esi-1]
422aec:  jl   0x422ad0
422aef:  movb $0x0,0x450b48
```

3000 iterações, valores em 1..255, base **`0x0044FF90`**.

Confere por aritmética, e a conferência é o que dá confiança no resto: o
incremento de `%esi` acontece **antes** da escrita, então os índices efetivos vão
de 1 a 3000 sobre a base `0x44ff8f`, isto é, `0x44FF90` até `0x450B47`. E
`0x44ff90 + 0xbb8 = 0x450b48` — exatamente o byte que `0x422aef` zera. A tabela é
uma **string C de 3000 caracteres com terminador**, e é por isso que os valores
começam em 1 e não em 0: um zero no meio a truncaria. Também é por isso que a
serialização usa `0xbb9` = 3001, e não 3000 — ela leva o terminador junto.

O consumidor é `FUN_00417170`, chamado em **264 sítios** do `.text`. É a única
fonte de aleatoriedade da simulação:

```
417170:  push %esi
417171:  mov  0xc(%esp),%esi       ; arg2 = n
417175:  test %esi,%esi
417177:  jg   0x41717d
417179:  xor  %eax,%eax ; ret      ; n <= 0 → 0
41717d:  mov  0x450c34,%eax        ; cursor A
417182:  add  $0x1,%eax
417186:  mov  $0x4d2,%ecx
41718b:  idiv %ecx                 ; A = (A+1) % 1234
41718d:  mov  0x450bcc,%eax        ; cursor B
417192:  add  $0x1,%eax
417196:  mov  $0xbb8,%edi
41719e:  idiv %edi                 ; B = (B+1) % 3000
4171a1:  mov  %ecx,0x450c34
4171a7:  movzbl 0x44ff90(%edx),%eax
4171ae:  add  %ecx,%eax            ; tabela[B] + A
4171b0:  mov  %edx,0x450bcc
4171b7:  idiv %esi
4171ba:  mov  %edx,%eax ; ret      ; (tabela[B] + A) % n
```

Reconstruído:

```c
static unsigned char tabela[3000];  /* 0x0044FF90 */
static int cursorA;                 /* 0x00450C34 */
static int cursorB;                 /* 0x00450BCC */

int engine_random(int /* não lido */, int n) {
    if (n <= 0) return 0;
    cursorA = (cursorA + 1) % 1234;
    cursorB = (cursorB + 1) % 3000;
    return (tabela[cursorB] + cursorA) % n;
}
```

Período combinado dos dois cursores: 1234 × 3000 = 3.702.000 chamadas.

### O primeiro argumento é um IDENTIFICADOR DE SÍTIO, e a função o ignora

Entre `0x417170` e `0x4171bc` o único acesso a parâmetro é `0xc(%esp)`, que em
`__cdecl` — após o `push %esi` do prólogo — é o **segundo** argumento de origem.
O primeiro nunca é lido.

Ele não é lixo. Extraindo o imediato do primeiro argumento nos 264 sítios:

```
258 sítios passam um imediato   →  258 valores DISTINTOS, zero repetição
faixa 2 … 296
```

Um valor único por sítio de chamada, numerado sequencialmente. É instrumentação
— quase certamente o mecanismo com que o autor rastreava dessincronia de replay,
mantido nas chamadas e removido do corpo da função no build lançado.

Para o porte isso é um presente: **existe uma numeração canônica de todo ponto
de decisão aleatória do engine**, dada pelo próprio autor. Um sítio pode ser
citado por ID em vez de por endereço.

Os `n` imediatos observados: 1..8, 10, 11, 13..18, 20, 21, 24, 30, 31, 39, 40,
45, 50, 51, 59, 60, 70, 81, 100, 149, 171, 173, 175, 180..182, 188, 189, 192,
193, 200, 250, 279, 300.

### Cinco dos 264 sítios estão dentro da rotina de aplicação de acerto

Distribuição por faixa: 82 em `0x402000`-`0x408000`, 40 em `0x408000`-`0x410000`,
5 em `0x410000`-`0x418000`, 28 em `0x418000`-`0x420000`, 47 em
`0x420000`-`0x428000`, 25 em `0x428000`-`0x430000`, 37 em `0x430000`-`0x440000`.

Nenhum cai em `FUN_0040e490` (update por tick) nem em `FUN_00417400`
(`does_attack_success`). **Cinco caem em `FUN_0042e100`** — a rotina que os
achados A8-A12 auditaram:

| Sítio | ID | `n` | Destino do retorno |
|---|---|---|---|
| `0x0042e507` | 236 | 6 | — (segue para `fldl 0x448910`) |
| `0x0042f439` | 238 | 16 | `+0x70` (frame_id1) |
| `0x0042f4d3` | 239 | 16 | `+0x70` (frame_id1) |
| `0x0042f544` | 240 | 16 | `+0x70` (frame_id1) |
| `0x0042f65a` | 241 | 6 | `+0x70` (frame_id1) |

O sítio 238 está sob `cmpl $0x3ea,0x7ac(%ecx,%edx,1)` — teste de **state 1002**,
faixa de arma, não de personagem. Os sítios 239 e 240 são precedidos de
`movb $0x1,0xeb(%eax)`. O sítio 241 tem como alternativa no ramo oposto
`movl $0x14,0x70(%eax)` (frame 20).

**Não reconstruí o que cada um decide.** O padrão — frame escolhido em faixa de
16 ou de 6, em ramo de arma — é consistente com variação de frame de reação de
arma atingida, mas isso é leitura de contexto, não prova. Registrado como
pergunta aberta com endereço.

### Corolário — a simulação é determinística, e a prova é o replay

A tabela é **estado serializável**, gravada e restaurada junto com o cursor B:

```
43da30:  mov  0x44ff90(%eax),%dl       ; tabela  → buffer+0x8c8
43da3c:  mov  %dl,0x8c8(%esi,%eax,1)
43da46:  cmp  $0xbb9,%eax              ; 3001 bytes

43e3c5:  mov  0x8c4(%eax),%ecx         ; buffer+0x8c4 → cursor B
43e3cb:  mov  %ecx,0x450bcc
43e3e0:  mov  0x8c8(%eax,%ecx,1),%dl   ; buffer+0x8c8 → tabela
43e3e7:  mov  %dl,0x44ff90(%ecx)
```

e é lida/gravada em disco com tamanho `0xbb9` em `0x0040304a` e `0x00428679`.

Isso explica a mensagem em `0x49b60`: *"Recording file are recorded in a LF2
with some data files (character or stage files) different from yours. Your LF2
has to use the same set of data files in order in replay this recording file!"*
Se o `.lfr` guardasse posições, os `.dat` não precisariam bater. Ele guarda
**entrada + semente**, e o engine **re-simula**. Uma partida do LF2 é uma função
determinística de (tabela, cursores, entradas, `.dat`).

### Divergência encontrada

O porte não possui fonte de aleatoriedade alguma: varredura por `rand(`,
`srand`, `mt19937`, `random_device` e `uniform_int` em `src/`, `tools/` e
`tests/` retorna **zero** ocorrências. Os 264 pontos de decisão que no engine
consultam `FUN_00417170` não têm contrapartida.

### Reprodução

Não observável hoje: nenhum dos comportamentos que dependem dos 264 sítios está
implementado com fonte alternativa. Torna-se observável assim que IA de inimigo
ou qualquer escolha ramificada entrar — e, se entrar com `rand()` ou
`<random>`, entra divergente por construção.

### Evidência

Nível A. `0x0043cf40`-`0x0043cf5a` (semente), `FUN_00422ac0` (geração),
`FUN_00417170` (consumo), `0x0043da30`/`0x0043e3e0` (serialização),
`0x0040304a`/`0x00428679` (disco). Contagem de 264 chamadas por
`grep -c "call   0x417170"` sobre `objdump -d`.

### Conclusão

**Divergente do LF2 original** quanto ao mecanismo — o engine tem uma fonte
identificada e reconstruída, o porte não tem nenhuma.

**O que cada um dos 264 sítios decide não foi levantado.** Enquanto isso não for
feito, o impacto por comportamento segue **sem comprovação**, e a prioridade de
implementar `engine_random` depende de quantos desses sítios caem em código que
o porte já executa.

---

---

## A14 — O tombo NÃO carrega velocidade nenhuma em `y_velocity`; ele acumula em `+0x30`, e o valor padrão é **7.0 para cima**

**Severidade:** Fidelidade · Observável

### Comportamento do engine

`FUN_0042e100`, bloco `0x0042f183`-`0x0042f21b`. O bloco inteiro está sob a
guarda de que a vítima **vai tombar**:

```
42f183:  mov  0x194(%esi,%edi,4),%eax   ; vítima
42f18a:  cmpl $0x50,0xb0(%eax)          ; fall == 80 ?
42f191:  jne  0x42f2a7                  ; não tomba → nada disto acontece
```

`80` é o marcador de tombo estabelecido em A8. Só quando `fall` foi reescrito
para 80 é que existe impulso vertical.

Leitura do `dvy` do `itr` e ramificação:

```
42f197:  mov  0xc(%esp),%edx            ; ponteiro do itr
42f19b:  mov  0x18(%edx),%ecx           ; itr->dvy      (itr+0x18)
42f1a0:  mov  0x368(%eax),%eax          ; vítima->file
42f1a6:  mov  0x6f8(%eax),%eax          ; file->type
42f1ac:  mov  %ecx,0x18(%esp)
42f1b0:  je   0x42f1fb                  ; dvy == 0 → ramo B
```

**Ramo A — o `itr` declara `dvy`:**

```
42f1b2:  cmp  $0x2,%eax ; je 0x42f1bc   ; type 2 (arma pesada)
42f1b7:  cmp  $0x3,%eax ; jne 0x42f1c2  ; type 3 (objeto de efeito)
42f1bc:  cmpl $0x28,0x1c(%edx)          ; itr->fall <= 40 ?
42f1c0:  jle  0x42f1d3                  ;   → não aplica impulso
42f1c2:  fildl 0x18(%esp)               ; (double)itr->dvy
42f1cd:  faddl 0x30(%eax)
42f1d0:  fstpl 0x30(%eax)               ; vítima->+0x30 += itr->dvy
```

Trava contra atravessar o chão, **só no ramo A**:

```
42f1da:  fildl 0x14(%eax)               ; (double) y inteiro da vítima
42f1dd:  faddl 0x30(%eax)
42f1e0:  call 0x4450d0                  ; _ftol2_sse (cvttsd2si)
42f1e7:  jle  0x42f21e                  ; resultado <= 0 → segue
42f1f0:  fldl 0x448338                  ; 12.0
42f1f6:  fstpl 0x30(%ecx)               ; vítima->+0x30 = 12.0
```

**Ramo B — o `itr` não declara `dvy` (`dvy == 0`):**

```
42f1fb:  cmp  $0x2,%eax ; je 0x42f205
42f200:  cmp  $0x3,%eax ; jne 0x42f20b
42f205:  cmpl $0x28,0x1c(%edx)          ; itr->fall <= 40 ?
42f209:  jle  0x42f21e                  ;   → não aplica impulso
42f20b:  mov  0x194(%esi,%edi,4),%eax
42f212:  fldl 0x30(%eax)
42f215:  fsubl 0x447a50                 ; 7.0
42f21b:  fstpl 0x30(%eax)               ; vítima->+0x30 -= 7.0
```

`0x00447a50` contém `7.0` (double, conferido byte a byte).

**Convenção de sinal, provada:** a gravidade é somada em `+0x48`
(`0x0040e788`-`0x0040e791`: `fldl 0x48(%esi); faddl 0x448348 (=1.7); fstpl
0x48(%esi)`) e a posição integra `y_position += y_velocity`
(`0x0040e6c2`-`0x0040e6c8`). Logo **`+y` aponta para BAIXO** e um lançamento
para cima é **negativo**. `-7.0` é impulso para cima.

**Exceção com endereço:** se a vítima é `file->type` 2 (arma pesada) ou 3
(objeto de efeito) **e** `itr->fall <= 40`, não há impulso vertical nenhum —
nem o padrão nem o do `itr`.

### Divergência encontrada

O porte usa `launch(-8.f)` e `vy = -6.f`, ambos inventados. Nenhum dos dois
existe: o padrão é **7.0**, e ele não é uma constante de lançamento fixa — é o
fallback de quando o `itr` não traz `dvy`. Quando traz, o engine soma
`itr->dvy` (campo que o porte já parseia e nunca leu neste caminho).

A coincidência com `-8.0` em `0x448340` continua sendo coincidência: aquele
valor é limiar de seleção dos frames 180-183 em `0x0040e7c1`, confirmado nesta
sessão.

### Evidência

Nível A. `0x0042f183`-`0x0042f21b`; constante `7.0` em `0x00447a50`, `12.0` em
`0x00448338`; sinal de `y` por `0x0040e6c2`/`0x0040e788`; `_ftol2_sse` em
`0x004450d0` (`cvttsd2si`).

### Conclusão

**Divergente do LF2 original.**

---

## A15 — Frame de queda 180 vs 186 é escolhido pelo SINAL do empurrão horizontal contra o facing

**Severidade:** Fidelidade · Observável

### Comportamento do engine

Imediatamente depois de A14, ainda dentro da guarda `fall == 80`
(`0x0042f21e`-`0x0042f258`):

```
42f21e:  mov  0x194(%esi,%edi,4),%ecx
42f225:  mov  0x80(%ecx),%dl            ; facing da vítima
42f22b:  test %dl,%dl ; jne 0x42f239
42f22f:  fcoml 0x28(%ecx)               ; 0.0  vs  +0x28
42f234:  test $0x1,%ah                  ; C0
42f237:  je   0x42f248                  ; → frame 180
42f239:  cmp  $0x1,%dl ; jne 0x42f251    ; facing != 0 e != 1 → frame 186
42f23e:  fcoml 0x28(%ecx)
42f243:  test $0x41,%ah                 ; C0|C3
42f246:  jp   0x42f251                  ; → frame 186
42f248:  movl $0xb4,0x70(%ecx)          ; frame 180
42f251:  movl $0xba,0x70(%ecx)          ; frame 186
```

`ST(0)` é `0.0` — a rotina mantém um zero no topo da pilha x87 e o restaura
depois de cada chamada (`d9 ee fldz` em `0x0042ec9a`, `0x0042ecef`,
`0x0042f054`, `0x0042f4ab`, `0x0042f4d8`, entre outros). A estrutura das duas
comparações — `C0` isolado num ramo, `C0|C3` no outro — é o idioma do MSVC para
`0.0 < v` e `0.0 >= v`.

Resultado: **facing 0 com empurrão `<= 0`, ou facing 1 com empurrão `>= 0`, dá
frame 180 (`0xb4`); o oposto dá frame 186 (`0xba`)**. Isto é, cair para trás dá
180 e cair para a frente dá 186. O eixo é direção, não intensidade — o mesmo
padrão que A9 estabeleceu para 222/224.

### Divergência encontrada

O porte tem um único frame de queda escolhido por `vy` (180-183, achado
anterior) e não distingue 180 de 186 por direção.

### Evidência

Nível A. `0x0042f21e`-`0x0042f258`.

### Conclusão

**Divergente do LF2 original.**

---

## A16 — O empurrão não é aplicado no acerto: existe um PASSO DE COMMIT que divide pelo número de golpes do tick

**Severidade:** Fidelidade · Observável · **este é o achado central da sessão**

### Comportamento do engine

O `object_t` tem **duas** trincas de `double` para velocidade, e elas são
distintas:

| Offsets | Papel |
|---|---|
| `+0x28` / `+0x30` / `+0x38` | **acumulador de empurrão** (x, y, z) |
| `+0x40` / `+0x48` / `+0x50` | **velocidade efetiva** (x, y, z) |
| `+0x58` / `+0x60` / `+0x68` | posição (x, y, z) |

Prova estrutural, no construtor `FUN_004061d0`
(`0x004061da`-`0x0040621c`): as **seis** primeiras são inicializadas com o mesmo
valor (`0x447920` = `0.1`), na ordem `+0x50, +0x48, +0x40, +0x38, +0x30, +0x28`,
e só depois as três de posição recebem `0.0`. Prova funcional, em
`0x00430de6`-`0x00430e18`: um mesmo `ST(0)` é gravado nas seis, aos pares
(`+0x30` e `+0x48`, depois `+0x28` e `+0x40`, depois `+0x38` e `+0x50`) — é o
"zerar todas as velocidades".

Só a velocidade efetiva move o objeto: `0x0040e51d` (`x += +0x40`),
`0x0040e587` (`z += +0x50`), `0x0040e6c2` (`y += +0x48`), gravidade em `+0x48`.

**`FUN_0042e100` escreve exclusivamente no acumulador.** `itr->dvx` vai para
`+0x28` (`0x0042ee5b`, `0x0042ee6d`, e mais nove sítios); `itr->dvy` e o padrão
`-7.0` vão para `+0x30` (A14). Em toda a faixa `0x42e100`-`0x430400` há
exatamente **duas** escritas em `+0x48`, e as duas são de arma ricocheteando
(`0x0042e53d`, `0x0042f474`), nenhuma da vítima.

Quem transforma acumulador em velocidade é `FUN_004196f0`
(`0x004196f0`-`0x00419798`), um passo separado que varre os 400 slots. Chamado
uma vez por tick em `0x0041f540`, **depois** do laço de colisão que chama
`FUN_0042e100`:

```
4196f1:  fldl 0x4479e0            ; K = 2.0            (ST2 no laço)
4196f8:  fldz                     ; 0.0                (ST1 no laço)
419703:  cmpb $0x0,0x4(%ecx,%esi,1)  ; slot ativo?
41970c:  cmpl $0x0,0xb4(%edx)     ; shaking != 0 → PULA o slot inteiro
419715:  cmpl $0x0,0x20(%edx)     ; attacks == 0 → só zera os acumuladores
41971b:  mov  0x20(%edx),%edi     ; attacks
41971e:  fldl 0x28(%edx)
419721:  fmul %st(2),%st          ; * 2.0
41972a:  fildl 0x8(%esp)          ; (double)(attacks + 1)
41972e:  fdivrp %st,%st(1)
419730:  fstpl 0x40(%edx)         ; x_velocity = (+0x28 * 2) / (attacks+1)
419735..41974a: idem para  +0x30 → 0x48
41974f..419764: idem para  +0x38 → 0x50
419769:  movl $0x0,0x20(%edx)     ; attacks = 0
419772:  fstl 0x28(%edx)          ; acumuladores = 0.0
419777:  fstl 0x30(%edx)
41977c:  fstl 0x38(%edx)
```

`0x004479e0` contém `2.0`, conferido byte a byte.

`+0x20` (`attacks` no `object.h` do OpenLF2) é **o número de `itr` que
acertaram este objeto neste tick** — incrementado na vítima em `0x0042ea88` e
`0x0043009b`, zerado aqui.

**A aritmética fecha o caso do golpe único.** Com `attacks == 1`:
`v = acumulado × 2 / 2 = acumulado`. Um único golpe entrega exatamente o que o
`itr` pediu. Com dois `itr` simultâneos: `(a₁+a₂) × 2 / 3` — a soma dos dois é
amortecida, não somada crua. É um anti-juggle **de tick**, embutido na
aritmética, que o porte não tem em forma nenhuma.

### Consequência para A14

Um tombo causado por um único `itr` sem `dvy` entrega
`y_velocity = -7.0 × 2 / 2 = -7.0`. **`-7.0` é o número que o porte deveria
usar para o caso simples** — mas só o caso simples. Com dois golpes no mesmo
tick, não é.

### Divergência encontrada

O porte aplica o empurrão direto na velocidade, no instante do acerto, sem
acumulador, sem contador de golpes por tick e sem passo de commit.

### Evidência

Nível A. Construtor `0x004061d0`; pareamento em `0x00430de6`-`0x00430e18`;
integração em `0x0040e51d`/`0x0040e587`/`0x0040e6c2`; acumulação em
`0x0042ee5b` e `0x0042f1cd`/`0x0042f21b`; commit em `FUN_004196f0`
(`0x004196f0`-`0x00419798`), chamado em `0x0041f540`; `K = 2.0` em `0x004479e0`.
Inventário exaustivo de `+0x30` no `.text`: 5 escritas e 5 leituras, todas
mapeadas — nenhuma outra rota do acumulador para a velocidade existe.

### Conclusão

**Divergente do LF2 original.**

---

## A17 — `shaking` é hitstop de verdade: congela o objeto inteiro e ADIA o empurrão · **fecha A12**

**Severidade:** Fidelidade · Observável

### Comportamento do engine

A12 registrou as escritas (`atacante = +3`, `vítima = -5` em
`0x004300a6`/`0x004300b7`; há um segundo par `+3`/`-3` em
`0x0042f2b7`/`0x0042f2cc`, e `+2`/`-3` em `0x00418986`/`0x00418997`) e concluiu
"não foi possível comprovar o efeito". Os três consumidores estão isolados
agora.

**1. Congela a atualização por tick.** `FUN_0040e490` é a primeira coisa que
roda para o objeto, e é uma cancela:

```
40e490:  push %ecx ; push %esi ; mov %ecx,%esi
40e494:  mov  0xb4(%esi),%eax
40e49a:  test %eax,%eax
40e49c:  je   0x40e4c3            ; shaking == 0 → segue a atualização inteira
40e49e:  jle  0x40e4a9
40e4a0:  add  $0xffffffff,%eax
40e4a3:  mov  %eax,0xb4(%esi)     ; shaking > 0 → -1
40e4a9:  mov  0xb4(%esi),%eax
40e4b1:  jge  0x40ef68
40e4b7:  add  $0x1,%eax
40e4ba:  mov  %eax,0xb4(%esi)     ; shaking < 0 → +1
40e4c0:  pop ; pop ; ret          ; e RETORNA — nada mais acontece neste tick
```

Enquanto `shaking != 0`, o objeto não anda, não cai, não avança frame e não
decai `fall`/`bdefend`. O contador anda 1 por tick **em direção a zero, dos dois
lados** — o sinal não altera a duração.

Há uma segunda cancela equivalente em `0x0040d966`, que deixa passar apenas
`file->type == 3`.

**2. Adia o commit do empurrão.** `FUN_004196f0` (A16) pula o slot inteiro
quando `shaking != 0` (`0x0041970c`) — não aplica **e não zera** os
acumuladores. Eles ficam guardados até o hitstop acabar.

**3. O sinal é de DESENHO.** Em `0x0040de38`:

```
40de38:  cmp  %ebp,0xb4(%esi)     ; ebp = 0
40de43:  jge  0x40de54            ; shaking >= 0 → sem deslocamento
40de45:  mov  0x20(%esp),%eax
40de49:  lea  (%eax,%eax,2),%ebp
40de4c:  lea  -0x3(%ebp,%ebp,1),%ebp   ; deslocamento = 6*n - 3
40de50:  mov  %ebp,0x10(%esp)
```

Só `shaking < 0` recebe deslocamento lateral. É isso que separa atacante de
vítima: **os dois congelam, só a vítima treme.**

### O encadeamento completo, com endereço em cada elo

1. `FUN_0042e100` acumula o empurrão em `+0x28`/`+0x30` e grava
   `vítima->shaking = -5`, `atacante->shaking = +3`.
2. No mesmo tick, `FUN_004196f0` vê `shaking != 0` e **não** aplica.
3. Nos ticks seguintes, `FUN_0040e490` congela os dois e anda o contador.
4. Quando `shaking` chega a zero, `FUN_004196f0` aplica o empurrão acumulado e
   o objeto sai voando.

O impacto é visível: o golpe conecta, ambos param, e só depois a vítima é
arremessada.

### Divergência encontrada

O porte não tem hitstop nenhum. Não é só um efeito visual ausente: sem a
cancela, o empurrão sai no tick do acerto em vez de sair depois, e o `fall`
decai durante o congelamento que não existe.

### Evidência

Nível A. `FUN_0040e490` (`0x0040e490`-`0x0040e4c2`), `0x0040d966`,
`0x0041970c`, `0x0040de38`-`0x0040de50`. Escritas em `0x004300a6`/`0x004300b7`,
`0x0042f2b7`/`0x0042f2cc`, `0x00418986`/`0x00418997`.

### Conclusão

**Divergente do LF2 original.** A12 sai de "não foi possível comprovar" para
comprovado.

---

## A18 — `mp` começa em **500**, e a regeneração depende do HP que falta

**Severidade:** Fidelidade · Observável

### Comportamento do engine

**Campo.** `mp` é `+0x308`, não `+0x300`. A separação sai da inicialização
(`0x004063ca`-`0x004063e1`), onde quatro campos recebem `0x1f4` = **500** de uma
só vez:

```
4063ca:  mov  $0x1f4,%ecx
4063cf:  mov  %ecx,0x2fc(%esi)    ; hp
4063d5:  mov  %ecx,0x304(%esi)    ; hp máximo
4063db:  mov  %ecx,0x300(%esi)    ; teto recuperável ("barra escura")
4063e1:  mov  %ecx,0x308(%esi)    ; mp
```

Que `+0x300` é o teto recuperável e não o `mp` se prova duas vezes: em
`0x0042e97d` ele recebe `-dano/3` a cada golpe (`imul $0x55555555` + `sar`), e
em `0x00418109`-`0x0041813e` a ordem `hp <= +0x300 <= +0x304` é imposta por
duas travas seguidas. Que `+0x308` é o `mp` se prova pelos custos de especial
lidos na rotina de decisão (`0x00403aa2` em diante: 350, 250, 100, 170, 220,
150, 200, 450, 70, 320, 120, 500 — a tabela de `mp:` do LF2).

**Regeneração** (`0x0041fa90`-`0x0041faf2`), uma vez por tick por objeto:

```
41fa90:  cmpl $0x1f4,0x308(%ecx)  ; mp >= 500 → não regenera
41fa9c:  cmpl $0x0,0x450bd4       ; flag global tem de ser 0
41faa5:  cmpl $0x0,0x8(%ecx)      ; +0x8 >= 0
41faab:  mov  0x2fc(%ecx),%eax    ; hp
41fab1:  cmp  $0x1f4,%eax ; jle   ; hp = min(hp, 500)
41fac3:  mov  0x6f4(%edx),%edx    ; file->id
41fac9:  cmp  $0x33,%edx ; je     ; id 51
41face:  cmp  $0x34,%edx ; jne    ; id 52
41fad3:  cltd ; sub %edx,%eax ; sar $1,%eax   ; para 51/52: hp /= 2
41fad8:  mov  $0x1f4,%edx
41fadd:  sub  %eax,%edx           ; t = 500 - hp
41fadf:  mov  $0x51eb851f,%eax
41fae4:  imul %edx ; sar $0x5,%edx            ; t / 100
41faee:  lea  0x1(%edx,%eax,1),%edx           ; t/100 + 1
41faf2:  add  %edx,0x308(%ecx)    ; mp += t/100 + 1
```

Ou seja: **`mp += (500 − min(hp,500)) / 100 + 1` por tick.** Com HP cheio é `+1`
por tick; com HP zerado é `+6` por tick. Os dados de `id` 51 e 52 têm o `hp`
dividido por 2 antes da conta, o que dobra o degrau — regeneram mais rápido em
qualquer HP.

Não há trava depois da soma: a trava é só na entrada (`mp >= 500` não
regenera), então o `mp` pode ultrapassar 500 em até 5 pontos no último tick.

### Divergência encontrada

Duas, ambas de origem conhecida:

- `mp = 200` no início (`fighter.hpp:177`, do F.LF `global.js: mp_start`) —
  **errado**, é 500.
- regeneração de `1 a cada 2 ticks` (`player.hpp::tickInner`,
  `(++mpRegenAcc & 1) == 0`, invenção pura sem fonte) — **errado**. O engine
  regenera todo tick, e a taxa cresce com o dano sofrido. No pior caso o porte
  está 12× lento (0,5/tick contra 6/tick).

### Evidência

Nível A. Inicialização `0x004063ca`-`0x004063e1`; regeneração
`0x0041fa90`-`0x0041faf2`; separação `+0x300`/`+0x304`/`+0x308` por
`0x0042e97d` e `0x00418109`-`0x0041813e`; custos de especial a partir de
`0x00403aa2`.

### Conclusão

**Divergente do LF2 original.** Sai a taxa-base de 9/9 e entra 11/11.

---

## A19 — Campos confirmados no disassembly (fecha parte do item 2 do plano)

**Severidade:** Método · não é achado de comportamento

Os candidatos do `struct_harvest.py` que esta sessão confirmou com base
identificada. Cada linha tem endereço; nenhuma vem da saída do script.

| Offset | O que é | Onde se prova |
|---|---|---|
| `+0x194` | array de **400 ponteiros** de objeto, no gerenciador (`this`), não no `object_t` | `0x0042e110` e 264 outros; laços com `cmp $0x190` em `0x0041f2a4`, `0x00419785` |
| `+0x20` | golpes recebidos **neste tick**; divisor do commit; zerado em `0x00419769` | `0x0042ea88`, `0x0043009b`, `0x0041971b` |
| `+0x28`/`+0x30`/`+0x38` | acumulador de empurrão x/y/z (**não** `pic_x_gain`/`y_accl`/`z_accl`) | A16 |
| `+0x2FC` | `hp` | `0x004063cf` |
| `+0x300` | teto recuperável; cai `dano/3` por golpe | `0x0042e97d`, `0x00418109` |
| `+0x304` | `hp` máximo | `0x004063d5`, `0x0041810f` |
| `+0x308` | `mp` | A18 |
| `+0x340` | divisor de dano: `dano = injury*100 / +0x340` quando `> 0` | `0x0042e8c6`-`0x0042e8e5` |
| `+0x344` | índice 1-2 usado no placar global `0x00451b60` | `0x0042e932`-`0x0042e94e` |
| `+0x348` | dano creditado ao dono | `0x0042e9c3` |
| `+0x34C` | dano sofrido, acumulado | `0x0042e98a` |
| `+0x354` | **slot do dono a creditar**; default `99` no construtor | `0x0042e913`, `0x0042e9b6`, `0x004063c0` |
| `+0x358` | abates creditados ao dono | `0x0042e925` |
| `+0xB4` | `shaking` | A17 |

### Armadilha que precisa ficar registrada

**O `object_t` e o struct do `.dat` de personagem têm offsets que colidem na
mesma faixa, e os dois guardam `double`.** A rotina de dump em
`0x0040d185`-`0x0040d22a` imprime o struct do `.dat` campo a campo com o nome
literal, e ela diz:

| Offset | Campo do `.dat` |
|---|---|
| `+0x20` | `running_speed` |
| `+0x28` | `running_speedz` |
| `+0x30` | `heavy_walking_speed` |
| `+0x38` | `heavy_walking_speedz` |
| `+0x40` | `heavy_running_speed` |
| `+0x48` | `heavy_running_speedz` |

Isto é, `fldl 0x30(%ecx)` pode ser `heavy_walking_speed` **ou** o acumulador de
empurrão, dependendo de quem é `%ecx` — e as duas leituras em `0x00413838` e
`0x004138c6` são do `.dat`, não do objeto. O `struct_harvest.py` não distingue
os dois, e esta é a segunda vez que a ressalva de Nível D dele impede um erro.
Vale acrescentar a tabela acima ao cabeçalho do script.

# IMPLEMENTAÇÃO DO PORTE

*Histórico. Não é evidência de fidelidade.*

- `Player::fp`/`fpAcc`/`FP_DOP`/`FP_FALL` substituídos por `Player::fall`
  (inteiro, semântica do binário) e `Player::bdefend`. Constante `FALL_DOWN = 80`.
- `Player::hit()` reescrito: acumulação com default em `== 0`, tombo forçado por
  gelo/queda/ar/morte, quatro faixas com reescrita para o piso, 222/224 por
  facing, `bdefend` acumulado com quebra acima de 30, dano defendido `injury/10`.
- Assinatura passou a receber `itrBdefend` e `attackerFacingRight`; os quatro
  caminhos de ataque de `main.cpp` alimentam ambos, e `HitInfo` carrega
  `bdefend` (incluindo pelo `weapon_strength_list`).
- Decaimento de `fall` e `bdefend` em `tickInner`: 1 por tick.

**Não implementado:** A12/A17 (`shaking`), A14, A15, A16, A18 — todos abertos
para a próxima sessão. Nenhuma linha de código do porte mudou em 2026-08-12
depois de A13; os achados A14-A19 são de auditoria, não de implementação.

---

# VALIDAÇÃO DO PORTE (não é evidência de fidelidade)

234 CHECKs, sem falhas (13 novos, cobrindo saturação no piso, 222/224 por
facing, acumulação e quebra de `bdefend`, custo `injury/10` e decaimento de 1).
`check-main` contra headers SDL2 reais. Harness 5400 ticks limpo em dennis
(dano 3720, pico 27/48, 0 descartes) e henry (dano 1930).

Os dois testes que falharam na primeira execução codificavam o modelo antigo —
`combo hit 2 (FP 50): still standing` e `front defend fully blocks a light hit`.
Ambos foram reescritos para o modelo evidenciado, não silenciados.
