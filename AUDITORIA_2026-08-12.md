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

**Não foi possível comprovar** o efeito. O valor é gravado e decai, mas não
reconstruí o consumidor — o sinal oposto (atacante positivo, vítima negativo)
sugere semânticas diferentes para os dois lados, provavelmente pausa de
animação de um e tremor lateral do outro. Implementar sem isolar o consumidor
seria adivinhar. Fica registrado com os endereços para a próxima sessão.

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

**Não implementado:** A12 (`shaking`).

---

# VALIDAÇÃO DO PORTE (não é evidência de fidelidade)

234 CHECKs, sem falhas (13 novos, cobrindo saturação no piso, 222/224 por
facing, acumulação e quebra de `bdefend`, custo `injury/10` e decaimento de 1).
`check-main` contra headers SDL2 reais. Harness 5400 ticks limpo em dennis
(dano 3720, pico 27/48, 0 descartes) e henry (dano 1930).

Os dois testes que falharam na primeira execução codificavam o modelo antigo —
`combo hit 2 (FP 50): still standing` e `front defend fully blocks a light hit`.
Ambos foram reescritos para o modelo evidenciado, não silenciados.
