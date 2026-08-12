# Auditoria de fidelidade — `lf2.exe` · 2026-08-12

> **Identificação do binário de referência.** `reference/decomp/lf2.exe` é o
> **LF2 v2.0a**, build de **2009-07-10 17:15:35 UTC** (`TimeDateStamp` =
> `0x4a577737` no cabeçalho PE), SHA256
> `12dfa00f6b767508612550e9ab27ab74b4201ff4cb9ff31d068925924eab8fc5`.
> PE32 i386, MSVC 8.0, sem packer. **Não** é o build de 1999 — v2.0a é o último
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
