# Auditoria de fidelidade — `lf2.exe` · 2026-07-30

Metodologia S. Alvo: `reference/decomp/lf2.exe` (PE32 i386, `pei-i386`).
Ferramenta primária: `objdump -d --start-address= --stop-address=`.

Convenções de estrutura usadas em todo o relatório (offsets confirmados no
assembly; nomes via OpenLF2 `include/object.h`, `itr.h` — Nível C, apenas
nomeação):

| Offset | Campo |
|---|---|
| `+0x10` | `x` |
| `+0x28` | `x_velocity` (double) |
| `+0x40` | `y_velocity` (double) |
| `+0x48` | `vy` usado no teste de queda (double) |
| `+0x70` | `frame_id1` (frame corrente) |
| `+0x78` | `frame_id3` (frame usado nos testes de **state**) |
| `+0x7c` | `frame_id4` (frame usado na geometria itr/bdy) |
| `+0x88` | `frame_wait` |
| `+0xEC` | `arest` — **escalar** |
| `+0xF0` | `vrest_of_objects[400]` — **array indexado por id da vítima** |
| `+0x368` | `file` |
| `file+0x6f4` | `file->id` |
| `file+0x6f8` | `file->type` (0 = personagem) |
| `frame` | stride `0x178`, base `file+0x7a4`; `state` em `+0x7ac` |
| `itr` | stride `0x50`; `dvx` em `+0x14`; `effect` em `+0x2c` |

---

## A1 — Cadeia de vetos por `itr->effect`

**Severidade:** Fidelidade · Observável

### Comportamento do engine

A rotina `FUN_00417400` (0x00417400) decide se um par atacante/vítima produz
acerto. Assinatura reconstruída pelos deslocamentos de pilha (`sub $0x30,%esp`
+ `push esi` + `push ebx/ebp/edi`): `param_1` = atacante em `0x44(%esp)`,
`param_2` = vítima em `0x48(%esp)`, `param_3` = modo em `0x4c(%esp)`.

Estrutura de laços reconstruída:

- laço externo sobre os **itrs** do atacante — incremento em `0x00417f59`,
  compara com `frame+0x128` (`itrs_size`), volta a `0x00417465`;
- laço interno sobre os **bdys** da vítima — incremento em `0x00417f40`,
  compara com `frame+0x12c` (`bdys_size`), volta a `0x00417492`.

Ponteiro do itr calculado em `0x0041746d`:
`lea (%eax,%eax,4),%eax` + `shl $0x4,%eax` → `×0x50`, somado a `frame+0x130`.

Toda a cadeia de vetos está **dentro de `itr->kind == 0`**:

```
41753d:  test %edx,%edx            ; edx = itr->kind
41753f:  jne  0x4176ac             ; kind != 0 → cadeia não se aplica
417545:  mov  0x10(%esp),%eax      ; eax = itr
417549:  mov  0x2c(%eax),%ecx      ; ecx = itr->effect
```

Vetos, na ordem em que o binário os avalia:

```
; effect 4 (shrafe) contra personagem
41754c:  cmp  $0x4,%ecx
41755e:  cmp  %edx,0x6f8(%eax)     ; edx==0 → file->type == 0 (personagem)
417564:  je   0x417f59

; effect 20 (burn)
41756e:  cmp  $0x14,%ecx
417580:  cmpl $0x0,0x6f8(%eax)
417587:  jne  0x417f59             ; vítima NÃO é personagem
4175a7:  cmpl $0x12,0x7ac(%edi,%eax,1)   ; frames[vítima->frame_id3].state == 18
4175af:  je   0x417f59
4175cf:  cmpl $0x13,0x7ac(%edi,%eax,1)   ; == 19
4175d7:  je   0x417f59

; effect 21 (flame)
4175e1:  cmp  $0x15,%ecx
4175fc:  cmpl $0x12,...  je 0x417f59
417624:  cmpl $0x13,...  je 0x417f59

; effect 30 (freeze column)
417636:  cmp  $0x1e,%ecx
417642:  mov  0x70(%eax),%eax      ; frame_id1 da vítima
417645:  cmp  $0xc8,%eax ; jl  0x41765b
41764c:  cmp  $0xca,%eax ; jle 0x417f59    ; 200 <= frame_id <= 202

; effect 2 (fire)
41765b:  cmp  $0x2,%ecx
417660:  mov  0x44(%esp),%ecx      ; ATACANTE
41767a:  cmpl $0x13,0x7ac(%ecx,%eax,1)    ; atacante state == 19
417682:  jne  0x4176ac             ; não é burn_run → sem veto
417684:  mov  0x194(%esi,%edi,4),%eax     ; VÍTIMA (edi = 0x48(%esp))
41769a:  cmpl $0x12,0x7ac(%ecx,%eax,1)    ; vítima state == 18
4176a2:  je   0x417f59
```

Ponto de destino `0x00417f59` é o **incremento do laço externo**, não do
interno. Consequência semântica: o veto descarta o itr inteiro contra aquela
vítima — não pula apenas um bdy.

Os testes de state leem `frame_id3` (`+0x78`), enquanto a geometria itr/bdy
mais adiante na mesma rotina usa `frame_id4` (`+0x7c`).

Em resumo: um corpo em state 18 (queimando) ou 19 (corrida em chamas) é imune
a `effect` 20 e 21; `effect 2` não reincide quando o atacante está em 19 e a
vítima em 18; `effect 30` não atinge quem está nos frames 200-202.

### Divergência encontrada

A implementação não avaliava `itr->effect` como condição de acerto em nenhum
dos caminhos de ataque. Os frames 203-206 de todo `.dat` de personagem portam
`itr kind:0 injury:30 fall:70 effect:20`; sem o veto, esse itr atinge um
atacante em state 19. Simetricamente, o itr `effect:2` de `firen.dat` frames
257-261 (state 19) reincide sobre uma vítima em state 18.

### Reprodução

Firen em corrida em chamas atravessando alvo já em chamas: o atacante recebe
`injury:30` com `fall:70`. Alvo em chamas sob fogo persistente: reacendimento
a cada `vrest`.

### Evidências

- **Fonte principal:** Assembly · **Nível A**
- **Endereço:** `0x00417400`, cadeia em `0x0041753d`-`0x004176a2`
- **Fonte secundária:** OpenLF2 `class_global.c:52-73` — mesma cadeia, porém
  **rotula duas regras como effect 2 e 20**, quando o binário usa 20 e 21.
  Contradição registrada; prevalece o assembly.

### Conclusão

**Divergente do LF2 original.**

---

## A2 — State 18 durante o voo e troca para o par de frames 205/206

**Severidade:** Fidelidade · Observável

### Comportamento do engine

Em `FUN_0040e490`, no update por tick do objeto, após o bloco que seleciona os
frames de queda por faixa de velocidade (`0xb4`-`0xb7`, `0xba`-`0xbd`, sob
`state == 0xc`), há um teste independente:

```
40e893:  mov  0x70(%esi),%eax             ; frame_id1
40e898:  imul $0x178,%edx,%edx
40e89e:  cmpl $0x12,0x7ac(%edx,%ecx,1)    ; state == 18 ?
40e8a6:  jne  0x40ef20
40e8ac:  cmp  $0xcd,%eax                  ; frame_id < 205 ?
40e8b1:  jge  0x40ef20
40e8b7:  fcompl 0x48(%esi)                ; compara 1.0 com vy
40e8ba:  fnstsw %ax
40e8bc:  test $0x5,%ah
40e8bf:  jp   0x40ef22
40e8c5:  movl $0xcd,0x70(%esi)            ; frame_id = 205
```

Fluxo reconstruído: enquanto o state for 18, o engine **não** substitui o frame
por pose aérea genérica. Ele mantém o state e, quando a velocidade vertical
ultrapassa 1.0 (corpo descendo), promove 203/204 para **205**, o par de frames
de queimadura na horizontal. O limite `frame_id < 205` impede reentrada.

### Divergência encontrada

Dois pontos. A implementação sobrescrevia o frame de state 18 por pose de pulo
enquanto o corpo estava no ar, e sobrescrevia de novo no pouso. E os frames
205/206 nunca eram alcançados: a cadeia permanecia em 203↔204 em qualquer
condição de velocidade.

### Reprodução

Vítima incendiada em pleno salto: a queimadura desaparece na descida.

### Evidências

- **Fonte principal:** Assembly · **Nível A**
- **Endereço:** `0x0040e893`-`0x0040e8c5`, em `FUN_0040e490`
- **Fluxo reconstruído:** teste posterior e independente da seleção de frames
  de queda; não há caminho que zere o state 18 por estar no ar.

### Conclusão

**Divergente do LF2 original.**

---

## A3 — Knockback aplica `itr->dvx` em todo acerto

**Severidade:** Fidelidade · Observável

### Comportamento do engine

No bloco de aplicação de acerto (`FUN_0042e100`), o deslocamento horizontal da
vítima é uma soma da FPU, com espelho por facing:

```
42ee54:  mov    0xc(%esp),%edx       ; edx = itr
42ee58:  fildl  0x14(%edx)           ; carrega itr->dvx (inteiro)
42ee5b:  faddl  0x28(%eax)           ; + vítima->x_velocity
42ee5e:  fstpl  0x28(%eax)           ; grava de volta

42ee6a:  fildl  0x14(%ecx)
42ee6d:  fsubrl 0x28(%eax)           ; espelho para o facing oposto
42ee70:  fstpl  0x28(%eax)
```

Mesma operação replicada em `0x42eeb3`/`0x42eebd` e `0x42eedb`/`0x42eee5`,
selecionada por `cmpb $0x0,0x80(%ecx)` / `cmpb $0x1,...` (facing do atacante).

Fluxo reconstruído dos desvios que guardam esse bloco:

```
42ee85:  mov  0x6f8(%eax),%eax   ; file->type da vítima
42ee8b:  cmp  $0x4,%eax ; je     ; throw_weapon → outro tratamento
42ee94:  cmp  $0x6,%eax ; je     ; drink        → outro tratamento
42ee9d:  mov  0x2c(%edx),%eax    ; itr->effect
42eea0:  cmp  $0x16,%eax ; je    ; effect 22
42eea5:  cmp  $0x17,%eax ; je    ; effect 23
```

Os únicos discriminantes são `file->type` e `effect`. **Nenhum desvio consulta
o estado aéreo, o frame de queda ou o `fall` acumulado da vítima.** A ausência
desses testes é parte da evidência: o engine não distingue primeiro acerto de
re-acerto para efeito de knockback horizontal.

### Divergência encontrada

A implementação zerava o componente horizontal quando a vítima já estava em
queda, aplicando apenas re-elevação vertical.

### Reprodução

Segundo projétil atingindo alvo já derrubado: deslocamento horizontal nulo.

### Evidências

- **Fonte principal:** Assembly + fluxo reconstruído · **Nível A**
- **Endereço:** `0x0042ee54`-`0x0042eeeb`, em `FUN_0042e100`
- **Offsets:** `itr+0x14` (`dvx`), objeto`+0x28` (`x_velocity`),
  objeto`+0x80` (facing)

### Conclusão

**Divergente do LF2 original.**

---

## A4 — Objeto só entra no frame `hiting` quando `state == 3000`

**Severidade:** Fidelidade · Observável

### Comportamento do engine

Dois sítios distintos gravam `frame_id = 10`, e **ambos** são guardados pelo
mesmo teste de state:

Sítio 1 — `0x0042f0bf`:
```
42f0b0:  mov  0x70(%edx),%eax
42f0bf:  cmpl $0xbb8,0x7ac(%eax,%ecx,1)   ; state == 3000 ?
42f0ca:  jne  0x42f183                    ; não → sai sem tocar no frame
...      (exceções para freeze_ball, file->id 0xd1)
42f141:  movl $0xa,0x70(%edx)             ; frame_id = 10
```

Sítio 2 — `0x00430536`:
```
430520:  mov  0x194(%esi,%ebx,4),%eax     ; atacante
430527:  mov  0x70(%eax),%ecx
430536:  cmpl $0xbb8,0x7ac(%edx,%ecx,1)   ; state == 3000 ?
430541:  jne  0x43187a                    ; não → sai
430547:  movl $0xa,0x70(%eax)             ; frame_id = 10
43054e:  movl $0x0,0x88(%eax)             ; frame_wait = 0
430566:  fstl 0x40(%ecx)                  ; y_velocity = st0
```

`0xbb8` = 3000. Fluxo reconstruído: um objeto cujo frame corrente esteja em
qualquer outro state de voo — 3005 (`0xbbd`), 3006 (`0xbbe`) ou 18 — não é
redirecionado ao encostar numa vítima. Ele preserva `frame_id`, e portanto o
itr daquele frame, e prossegue.

### Divergência encontrada

A implementação redirecionava para o frame 10 incondicionalmente, para qualquer
objeto que conectasse.

### Reprodução

`henry_arrow2.dat` (frames 0-5, state 3006) encerra no primeiro corpo atingido.

### Evidências

- **Fonte principal:** Assembly · **Nível A**
- **Endereços:** `0x0042f0bf` e `0x00430536` (guarda), `0x0042f141` e
  `0x00430547` (gravação)
- **Fonte secundária:** F.LF `specialattack.js:213-247` — o handler de 3006 só
  transiciona contra 3005/3006. Corrobora; não foi usado para concluir.

### Conclusão

**Divergente do LF2 original.**

---

## A5 — `vrest` é por vítima; `arest` é escalar do atacante

**Severidade:** Fidelidade · Observável (parcial)

### Comportamento do engine

O objeto mantém dois temporizadores de re-acerto **estruturalmente distintos**:

```
; arest — escalar, sem índice
42f2eb:  movl $0x4,0xec(%eax)
4303c0:  cmp  %edx,0xec(%eax)

; vrest — array indexado por id da vítima
42e18b:  cmpb $0x0,0xf0(%eax,%ebx,1)      ; leitura/gate
42f314:  mov  %cl,0xf0(%eax,%ebx,1)       ; gravação do vrest do itr
42f28a:  movb $0x2d,0xf0(%ecx,%edx,1)     ; valores fixos em casos especiais
42f5c4:  movb $0x3,0xf0(%edx,%edi,1)
```

O modo de endereçamento `base+índice` em `+0xF0` é a evidência direta: há um
contador por vítima. O `arest` em `+0xEC` é acesso direto, sem índice.

Fluxo reconstruído: o gate de re-acerto (`0x0042e18b`) consulta a posição da
vítima corrente. Um atacante bloqueado contra uma vítima permanece livre para
atingir outra no mesmo passe.

### Divergência encontrada

A implementação mantinha um único contador por objeto, o que a tornava
efetivamente monoalvo. Além disso, `vrest` e `arest` são colapsados num único
valor na extração do itr (`vrest > 0 ? vrest : arest`), enquanto o engine os
mantém em campos de naturezas diferentes — escalar do atacante versus array por
vítima. **Esta segunda parte permanece divergente.**

### Reprodução

Qualquer projétil de `vrest` alto contra dois corpos alinhados atinge apenas o
primeiro.

### Evidências

- **Fonte principal:** Assembly · **Nível A**
- **Endereços:** `0x0042e18b`, `0x0042f314`, `0x0042f2eb`, `0x004303c0`
- **Fonte secundária:** OpenLF2 `include/object.h:88-89` — `arest` em `0xEC`,
  `vrest_of_objects[400]` em `0xF0`. Nomeação apenas.

### Complemento — semântica dos dois campos (desassemblada depois)

`itr->arest` está em `+0x20`, `itr->vrest` em `+0x24` (OpenLF2 `itr.h`,
Nível C; confirmados pelos acessos abaixo). A gravação, em `0x0042f2c8`:

```
42f2d6:  mov  0x20(%ecx),%eax      ; itr->arest
42f2d9:  cmp  $0x4,%eax
42f2dc:  jge  0x42f2f7             ; arest >= 4 → usa o valor do itr
42f2de:  cmpl $0x0,0x24(%ecx)      ; itr->vrest
42f2e2:  jne  0x42f2f7
42f2e4:  movl $0x4,0xec(%eax)      ; senão → atacante->arest = 4   (PISO)
42f2f7:  mov  %eax,0xec(%edx)      ; atacante->arest = itr->arest
42f304:  cmpl $0x0,0x24(%ecx)
42f308:  jle  0x42f31b             ; vrest <= 0 → não grava por par
42f30a:  mov  0x194(%esi,%edi,4),%eax  ; edi = VÍTIMA
42f311:  mov  0x24(%ecx),%cl
42f314:  mov  %cl,0xf0(%eax,%ebx,1)    ; vitima->vrest_of_objects[atacante]
```

Fluxo reconstruído, em três fatos:

1. `arest` é gravado em **todo** acerto, inclusive quando o itr também traz
   `vrest`. Não são alternativas.
2. O piso de 4 aplica-se **apenas** quando `arest < 4` **e** `vrest == 0`.
3. `vrest` é gravado como **byte**, só quando `> 0`, no array da **vítima**
   indexado pelo **atacante** — não no atacante.

Note o sentido do array: `0x42f30a` carrega a vítima em `%eax` e `%ebx` é o id
do atacante. O contador pertence a quem apanha.

### Conclusão

**Divergente do LF2 original.**

---

## A7 — Saturação do `arest` em 12

**Severidade:** Fidelidade · Latente

### Comportamento do engine

Existem dois sítios irmãos que gravam o `arest`. Ambos partilham o mesmo piso
de 4 (`0x0042f2d6` e `0x0043030f` são instrução a instrução idênticos). Eles
divergem no que vem depois da gravação.

Sítio 1 (`0x0042f2fe`) segue direto para o `vrest`, sem saturar:
```
42f2fe:  mov  %eax,0xec(%edx)
42f304:  cmpl $0x0,0x24(%ecx)      ; próximo passo é o vrest
```

Sítio 2 (`0x004303ae`) satura em 12 antes de seguir:
```
4303ae:  mov  %eax,0xec(%edx)
4303bb:  mov  $0xc,%edx
4303c0:  cmp  %edx,0xec(%eax)
4303c6:  jle  0x4303ce
4303c8:  mov  %edx,0xec(%eax)      ; arest = 12
```

### Divergência encontrada

Não determinada. A condição que encaminha um acerto ao sítio 1 em vez do sítio 2
não foi reconstruída, portanto não é possível afirmar em que circunstâncias o
teto se aplica.

### Reprodução

Não estabelecida. Impacto potencial: 143 itrs do conjunto de dados usam `arest`,
dos quais 51 com valor 15 e 21 com valor 16 — todos acima do teto.

### Evidências

- **Fonte principal:** Assembly · **Nível A** para a existência do teto
- **Endereços:** `0x004303bb`-`0x004303c8` (teto), `0x0042f2fe` (sem teto)
- Falta: reconstrução do desvio que separa os dois ramos.

### Conclusão

**Não foi possível comprovar.** O teto existe; a sua condição de aplicação, não.
Enquanto não estiver isolada, aplicá-lo alteraria metade dos golpes com `arest`
com base em metade da evidência.

---

## A6 — Flag `knockedDown`

**Severidade:** Higiene · **Não depende de comparação com o engine original**

### Descrição

`Player::knockedDown` é bookkeeping exclusivo da implementação. O engine
original não possui campo equivalente: toda decisão de queda/levantada é lida
do `state` do frame corrente (`frames[frame_id].state`), como visto em
`FUN_0040e490` e `FUN_0042e100`. A flag introduz um estado paralelo que pode
divergir do frame — por exemplo, permanecer ligada quando a cadeia de frames já
saiu da sequência de queda, suprimindo transições subsequentes.

### Evidências

- **Fonte principal:** ausência de contrapartida no binário
- **Nível:** não aplicável — achado interno

### Conclusão

**Inferência.** Hipótese: estado paralelo redundante, passível de dessincronizar
do frame. Motivação: o engine deriva tudo do state. Limitação: não há endereço
a citar, porque o campo não existe no original. Não constitui evidência de
fidelidade em nenhuma direção.

---

# IMPLEMENTAÇÃO DO PORTE

*Histórico. Não é evidência de fidelidade.*

- `lf2::itrEffectAllows()` em `src/engine/fighter.hpp`, aplicado nos quatro
  caminhos de ataque de `src/main.cpp` (A1).
- `ST_BURNING`/`ST_ICE` preservados em `Player::airborneTick()` e no pouso;
  troca 203/204 → 205 sob `vy > 1.0`; constante `fid::BURNING_AIR` (A2).
- Removido o zeramento de knockback horizontal em re-acerto (A3).
- `Object::spent()` gateia `Object::onHit()` (A4).
- `Object::victimRest[NUM_ENEMIES+1]` + escalar `Object::arest` e `playerArest`;
  `lf2::applyRest()` grava os dois conforme o fluxo desassemblado (A5).
- `tools/datdump.cpp` passa a imprimir `arest`/`vrest` — 143 itrs usam `arest` e
  o campo era invisível na ferramenta de inspeção.
- `knockedDown` limpo antes da promoção na expiração do fogo (A6).

Divergência conhecida **não corrigida:** o teto de `arest` em 12 (A7), retido
deliberadamente por falta de evidência sobre a sua condição.

---

# VALIDAÇÃO DO PORTE (não é evidência de fidelidade)

222 CHECKs na suíte host, sem falhas. `make check-main` contra headers SDL2
reais. Harness headless 3600 e 5400 ticks: pico de pool 27/48, 0 descartes,
0 ticks com pool cheio.

Demonstram ausência de regressão. Não sustentam nenhuma conclusão acima.
