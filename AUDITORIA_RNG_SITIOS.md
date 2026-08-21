> Anexo do `AUDITORIA_2026-08-20.md` (A20). Cada linha sai do campo
> `assembly` do export do Ghidra — **Nivel A**, nao pseudocodigo.
> As pistas de identidade sao strings e imports referenciados pela funcao;
> **pista nao e' prova**. Confirmar antes de usar como fonte.

# Os 264 sitios de `engine_random` — lista de trabalho

`FUN_00417170(tag_morto, n)` devolve `[0, n)`. **264 sitios em 19 funcoes.**
O porte implementa **2**, ambos em `FUN_0042e100`.

`n` em registrador = calculado em runtime; precisa ler o contexto para saber a faixa.

| funcao | sitios | n literais | n em reg. | pista de identidade |
|---|---:|---|---:|---|
| `0x00403a40` | 68 | 0x2, 0x3, 0x4, 0x5, 0x7, 0x8, 0xa, 0xd, 0xe, 0xf, 0x14, 0x1e, 0x2d, 0x32, 0x3c, 0x46, 0x64, 0xfa | 4 | — |
| `0x0041bc90` | 67 | 0x1, 0x2, 0x3, 0x4, 0x7, 0x8, 0xb, 0x12, 0x14, 0x1e, 0x27, 0x33, 0x3b, 0x51, 0x95, 0xc8 | 18 | Error; data\data.txt |
| `0x004094b0` | 22 | 0x11, 0x14, 0x1e | 19 | — |
| `0x00429730` | 20 | 0x3, 0x5, 0x6, 0x1e | 15 | %s.lfr; _Survival |
| `0x00408cb0` | 14 | 0x5 | 13 | — |
| `0x00406ba0` | 11 | 0x2, 0x4, 0x15, 0x18, 0x28 | 4 | — |
| `0x00434ab0` | 10 | 0x2, 0x4 | 6 | %s.lfr; %4d%02d%02d_%02d%02d%02d |
| `0x00432ab0` | 9 | 0x2, 0x8 | 5 | %s.lfr; %4d%02d%02d_%02d%02d%02d |
| `0x00437400` | 9 | 0x2, 0x7, 0xa, 0x12c | 2 | — |
| `0x00417f80` | 8 | 0x4, 0x5, 0x6, 0x7, 0x10 | 0 | — |
| `0x0042e100` **(no porte)** | 7 | 0x6, 0x10 | 0 | — |
| `0x004034f0` | 5 | 0x2 | 4 | — |
| `0x00438b40` | 4 | — | 4 | %s.lfr; Difficult |
| `0x00413080` | 3 | 0x2 | 0 | — |
| `0x00417400` | 2 | 0x2 | 0 | — |
| `0x00436fc0` | 2 | 0x1e | 1 | — |
| `0x00402130` | 1 | 0x8 | 0 | (Press Left/Right to change); Music: %s |
| `0x004025d0` | 1 | 0x8 | 0 | bgm\boss2.wma; bgm\stage2.wma |
| `0x0043a860` | 1 | — | 1 | Defense: %d.%d; Giant |

---

## O que as pistas mudam na prioridade

As strings desfazem a impressao de "262 sitios pendentes". A maior parte deles
**nao esta em mecanica de combate**, e varios vivem em caminhos que o porte nem
tem:

| grupo | funcoes | sitios | quando dispara |
|---|---|---:|---|
| modo de jogo / replay | `0x00429730`, `0x00432ab0`, `0x00434ab0`, `0x00438b40` | 43 | uma vez, na montagem da partida |
| musica | `0x00402130`, `0x004025d0` | 2 | uma vez, ao escolher faixa |
| info de personagem | `0x0043a860` | 1 | tela de atributos |
| **combate** | `0x0042e100`, `0x00417400` | **9** | a cada acerto |
| nao identificados | `0x00403a40`, `0x0041bc90`, `0x004094b0`, `0x00408cb0`, `0x00406ba0`, `0x00437400`, `0x00417f80`, `0x004034f0`, `0x00413080`, `0x00436fc0` | 209 | **desconhecido** |

As quatro funcoes de modo referenciam `%s.lfr` — o arquivo de replay — junto de
`_Survival`, `Difficult` e um formato de timestamp. Sao os modos de jogo, e os
sorteios delas sao de montagem: palco, ordem, personagem sorteado.

Os dois sitios de musica referenciam nomes de faixa em `bgm\`. Um sorteio cada,
no comeco.

**O alvo real e' `0x00403a40`, com 68 sitios.** Nenhuma string, `n` indo de 2 a
250, e o maior consumidor isolado do binario. Um consumidor desse porte que
dispara durante a partida so' pode ser decisao de IA. O porte **tem** IA
(`characters/enemy.hpp`), entao esta e' a funcao onde divergencia de contagem
vai doer primeiro no traco. Logo atras vem `0x0041bc90`, com 67.

### Correcao ao que ficou registrado no A25b

Eu escrevi que "cobertura de sitios de RNG e' pre-requisito da Fase 2". Esta'
forte demais. O correto: **cobertura dos sitios que disparam DENTRO da janela
tracada**. Sorteio de musica e montagem de modo acontecem antes do primeiro
tick — bastam ser contabilizados uma vez na condicao inicial, junto da tabela e
dos cursores capturados. O que precisa de paridade tick a tick e' IA e combate.

Ordem de ataque sugerida:

1. Identificar `0x00403a40` e `0x0041bc90` — 135 dos 264 sitios, e a hipotese
   de IA precisa de confirmacao, nao de fe.
2. Fechar os 5 sitios restantes de `0x0042e100` (dependem de `cpoint`, state
   1002 e `file->type` 4/6).
3. Os 2 de `does_attack_success` (`0x00417400`), que o porte ja' exerce.
4. Modo/musica: contabilizar na condicao inicial do traco, sem implementar.

## Sitios, um a um

### `0x00402130` — 1 sitios

- `00402328` n=0x8

### `0x004025d0` — 1 sitios

- `004025e9` n=0x8

### `0x004034f0` — 5 sitios

- `0040356e` n=ECX · `004035e5` n=EDX · `004036c7` n=EAX · `00403841` n=0x2
- `0040390d` n=EAX

### `0x00403a40` — 68 sitios

- `00403a51` n=EAX · `00403a8f` n=0xa · `00403b17` n=0x1e · `00403b87` n=0xf
- `00403c32` n=0xf · `00403efa` n=0x3 · `00403f2c` n=0x7 · `00403fe8` n=0x5
- `00403ff8` n=0x1e · `0040405a` n=0x7 · `004040b2` n=0xa · `004040cb` n=0x3
- `004040f6` n=0x7 · `0040415f` n=0xa · `00404174` n=0x3 · `00404254` n=ECX
- `00404281` n=0x2d · `004042e4` n=0x1e · `004043be` n=0x3 · `004043ce` n=0x2
- `0040445e` n=0xa · `00404472` n=0x1e · `0040454e` n=0xa · `004045a0` n=0x3
- `004045d4` n=0x8 · `0040472e` n=0x5 · `00404794` n=0x14 · `004047a8` n=0x64
- `0040484f` n=0xf · `004049f9` n=0xfa · `00404a69` n=0xf · `00404b5d` n=0x3
- `00404ba9` n=0x32 · `00404c64` n=0xa · `00404d10` n=0x5 · `00404e0e` n=0xa
- `00404eac` n=0xa · `00404f8b` n=0xf · `00404f9f` n=0x4 · `0040500f` n=0x14
- `00405114` n=0x46 · `00405158` n=0xa · `00405263` n=0xd · `0040528f` n=EAX
- `004052d8` n=0x1e · `0040537f` n=0x3c · `004053a7` n=EAX · `0040544a` n=0xf
- `004054b6` n=0x5 · `0040556e` n=0xa · `00405778` n=0x7 · `0040580b` n=0x7
- `00405931` n=0x7 · `004059bb` n=0x5 · `00405a72` n=0xa · `00405ae6` n=0x5
- `00405b6f` n=0xa · `00405bd2` n=0xa · `00405c44` n=0x3 · `00405cbf` n=0x7
- `00405d3b` n=0xa · `00405da7` n=0x5 · `00405e23` n=0xe · `00405eb5` n=0x5
- `00405f1b` n=0x5 · `00406054` n=0xa · `004060b1` n=0xa · `004060fe` n=0x5

### `0x00406ba0` — 11 sitios

- `004075b8` n=? · `004075d6` n=0x18 · `004075fd` n=0x18 · `0040765a` n=ECX
- `0040797b` n=? · `00407a28` n=0x18 · `00407aa0` n=ECX · `00408a02` n=0x2
- `00408bb0` n=0x4 · `00408bde` n=0x15 · `00408bfc` n=0x28

### `0x00408cb0` — 14 sitios

- `00408ccb` n=ECX · `00408e00` n=EAX · `00408ea5` n=ECX · `00408ed8` n=EAX
- `00408efe` n=EDX · `00409059` n=ECX · `004091fb` n=EAX · `00409220` n=EDX
- `0040928a` n=ECX · `004092ae` n=EAX · `00409332` n=ECX · `0040937c` n=EDX
- `004093e8` n=0x5 · `00409437` n=EDX

### `0x004094b0` — 22 sitios

- `00409775` n=EDX · `0040980a` n=EDX · `0040a2d1` n=0x1e · `0040a97f` n=ECX
- `0040a9ee` n=ECX · `0040aec4` n=EDX · `0040af2b` n=EAX · `0040afa4` n=EAX
- `0040b013` n=EDX · `0040b110` n=ECX · `0040b1d3` n=EDX · `0040b1e4` n=0x14
- `0040b27e` n=EAX · `0040b345` n=EDX · `0040b3d1` n=ECX · `0040b567` n=EAX
- `0040b659` n=ECX · `0040b775` n=0x11 · `0040b8f8` n=ECX · `0040b925` n=EAX
- `0040ba31` n=EDX · `0040bace` n=EAX

### `0x00413080` — 3 sitios

- `0041363c` n=0x2 · `004136cd` n=0x2 · `004136e7` n=0x2

### `0x00417400` — 2 sitios

- `00417c5f` n=0x2 · `00417d4b` n=0x2

### `0x00417f80` — 8 sitios

- `004181cd` n=0x7 · `004182e1` n=0x7 · `00418512` n=0x10 · `00418663` n=0x6
- `00418731` n=0x6 · `00418742` n=0x7 · `0041875e` n=0x4 · `00418779` n=0x5

### `0x0041bc90` — 67 sitios

- `0041eb84` n=0x33 · `0041ebbb` n=? · `0041ef6c` n=0xc8 · `0041efec` n=0x2
- `0041f045` n=0x1e · `0041f072` n=0x95 · `0041f09f` n=? · `0041f0c8` n=0x1e
- `0041f0ff` n=0x1e · `0041f123` n=EAX · `0041f792` n=0x7 · `0041f7b6` n=0x7
- `0041f818` n=? · `0041f87f` n=0x2 · `0041f8a9` n=0x2 · `0041f8dd` n=0x3
- `0041f908` n=0x3 · `0041f92e` n=0x7 · `0041f955` n=0x4 · `0041f96b` n=0x2
- `00420581` n=? · `004205a5` n=0x7 · `0042062f` n=0x14 · `004206a8` n=0x8
- `004206d6` n=0xb · `0042071d` n=0x4 · `00420736` n=? · `0042076f` n=0x4
- `0042078b` n=? · `004207c7` n=0x4 · `004207e5` n=? · `00420820` n=0x2
- `00420837` n=0x4 · `00420856` n=0x4 · `00420898` n=0x4 · `004208b9` n=?
- `004208d0` n=? · `004208ec` n=? · `00420927` n=0x4 · `0042094a` n=0x4
- `0042097c` n=0x4 · `004209b0` n=0x4 · `004209ef` n=0x4 · `00420a10` n=?
- `00420a2c` n=? · `00420a45` n=0x12 · `00420a96` n=0x4 · `00420ab9` n=?
- `00420ad7` n=? · `00420af0` n=0x12 · `00420b3f` n=0x4 · `00420e0e` n=0x51
- `00420e34` n=0x51 · `0042102d` n=? · `0042104d` n=0x27 · `00421078` n=0x14
- `004210a3` n=0xb · `00421198` n=0x4 · `004212de` n=? · `004212fe` n=0x3b
- `00421339` n=? · `00421367` n=0x1 · `00421527` n=0x2 · `00421591` n=0x1e
- `004215c5` n=0x1e · `004215e8` n=0x1e · `00421629` n=0x1e

### `0x00429730` — 20 sitios

- `0042b6ff` n=ESI · `0042c281` n=ESI · `0042caf1` n=ESI · `0042d233` n=EAX
- `0042d31d` n=EAX · `0042d35f` n=EDX · `0042d466` n=EAX · `0042d4a7` n=EDX
- `0042d68a` n=0x1e · `0042d7d6` n=ECX · `0042d8dd` n=EDI · `0042d9a8` n=EAX
- `0042d9e9` n=EDX · `0042da42` n=0x1e · `0042da77` n=0x5 · `0042db25` n=EDI
- `0042db3e` n=0x6 · `0042dbf0` n=EAX · `0042dc3f` n=0x3 · `0042e06d` n=EDI

### `0x0042e100` — 7 sitios

- `0042e507` n=0x6 · `0042f439` n=0x10 · `0042f4d3` n=0x10 · `0042f544` n=0x10
- `0042f65a` n=0x6 · `00430465` n=0x10 · `0043145e` n=0x6

### `0x00432ab0` — 9 sitios

- `004334b7` n=0x8 · `004334c5` n=0x8 · `00433621` n=ESI · `00434069` n=0x2
- `004340ce` n=ECX · `0043453a` n=ECX · `00434658` n=EAX · `00434699` n=EDX
- `004348e6` n=0x2

### `0x00434ab0` — 10 sitios

- `004355e7` n=0x4 · `004355f5` n=0x4 · `004357b1` n=ESI · `004362c9` n=0x2
- `00436368` n=ECX · `004363a7` n=ESI · `00436881` n=EDX · `0043699c` n=EAX
- `004369dd` n=EDX · `00436d8a` n=0x2

### `0x00436fc0` — 2 sitios

- `00437085` n=0x1e · `00437138` n=ECX

### `0x00437400` — 9 sitios

- `004374b8` n=0xa · `004374c6` n=0xa · `00437524` n=0x7 · `0043754b` n=0x2
- `0043761f` n=ECX · `00437660` n=0x12c · `00437681` n=0x2 · `00437697` n=0x12c
- `004376bd` n=?

### `0x00438b40` — 4 sitios

- `0043996c` n=ECX · `0043a2d4` n=EDX · `0043a526` n=ECX · `0043a66a` n=ECX

### `0x0043a860` — 1 sitios

- `0043ab1f` n=ECX
