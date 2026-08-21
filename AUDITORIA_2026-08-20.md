> Achados de fidelidade com endereco. Precedencia: este arquivo e' **Nivel A**
> (assembly). O que ainda nao tem evidencia vai para `AUDITORIA_SUPERFICIE.md`.
> Ponto de entrada do projeto: `RELATORIO_2026-08-12.md`.

# LF2 Vita — Auditoria 2026-08-20: a fonte de aleatoriedade, fechada

Sessao dedicada ao **item 10 do `AUDITORIA_SUPERFICIE.md`** — os 264 sitios de
decisao aleatoria, dos quais 259 nunca haviam sido mapeados. Estao mapeados.

Ferramenta nova: `ghidra-ai-bridge` sobre o projeto Ghidra do `lf2.exe`
(`tools/setup_ghidra_bridge.ps1`). O que ela deu de decisivo nao foi
pseudocodigo — foi **referencia cruzada indexada** mais o **assembly por
funcao** exportado em JSON. Os achados abaixo saem do assembly; o bridge so'
apontou onde olhar.

---

## A19 — `FUN_00417170` reconstruida por inteiro, e o primeiro argumento e' MORTO

A rotina tem 21 instrucoes. `0x00417170`-`0x004171bc`, integral:

```
00417170  PUSH ESI
00417171  MOV  ESI,dword ptr [ESP + 0xc]   ; ESI = arg2  (o modulo)
00417175  TEST ESI,ESI
00417177  JG   0x0041717d
00417179  XOR  EAX,EAX                     ; arg2 < 1  ->  retorna 0
0041717b  POP  ESI
0041717c  RET
0041717d  MOV  EAX,[0x00450c34]            ; cursor A
00417182  ADD  EAX,0x1
00417186  MOV  ECX,0x4d2                   ; 1234
0041718b  IDIV ECX                         ; EDX = (A+1) % 1234
0041718d  MOV  EAX,[0x00450bcc]            ; cursor B
00417192  ADD  EAX,0x1
00417196  MOV  EDI,0xbb8                   ; 3000
0041719b  MOV  ECX,EDX
0041719e  IDIV EDI                         ; EDX = (B+1) % 3000
004171a1  MOV  dword ptr [0x00450c34],ECX
004171a7  MOVZX EAX,byte ptr [EDX + 0x44ff90]   ; tabela[cursorB]
004171ae  ADD  EAX,ECX                     ; + cursorA
004171b0  MOV  dword ptr [0x00450bcc],EDX
004171b7  IDIV ESI                         ; % arg2
004171ba  MOV  EAX,EDX                     ; retorna o RESTO
004171bc  RET
```

Ou seja:

```c
int engine_random(int tag /* NAO USADO */, int n) {
    if (n < 1) return 0;
    cursorA = (cursorA + 1) % 1234;                    // 0x00450c34
    cursorB = (cursorB + 1) % 3000;                    // 0x00450bcc
    return (table3000[cursorB] + cursorA) % n;         // 0x0044ff90
}
```

**O primeiro argumento nunca e' lido.** Depois do `PUSH ESI`, `arg1` mora em
`[ESP+8]` e `arg2` em `[ESP+0xc]`; a unica leitura de pilha na funcao inteira e'
`[ESP+0xc]`. Nao ha' um so' acesso a `[ESP+8]`.

Nos 264 sitios, esse argumento morto assume 255 valores distintos entre `0x2` e
`0x128`, quase sempre crescendo junto com o endereco. **INFERENCIA** (nao e'
Nivel A): e' etiqueta de rastreio por sitio de chamada, deixada pelo
desenvolvedor e ignorada pelo build final. O que e' Nivel A e' so' isto: **o
porte nao precisa reproduzi-lo.** `engine_random(n)` basta.

---

## A20 — Os 264 sitios, mapeados: 19 funcoes

| funcao | sitios | `n` observados |
|---|---:|---|
| `0x00403a40` | 68 | 0x2..0x50, muitos literais |
| `0x0041bc90` | 67 | 0x1..0x3b, muitos literais |
| `0x004094b0` | 22 | 0x11, 0x14, 0x1e, + registradores |
| `0x00429730` | 20 | 0x3, 0x5, 0x6, 0x1e, + registradores |
| `0x00408cb0` | 14 | 0x5, + registradores |
| `0x00406ba0` | 11 | 0x2, 0x4, 0x15, 0x18, 0x28 |
| `0x00434ab0` | 10 | 0x2, 0x4, + registradores |
| `0x00432ab0` | 9 | 0x2, 0x8, + registradores |
| `0x00437400` | 9 | 0x2, 0x7, 0xa, 0x12c |
| `0x00417f80` | 8 | 0x4, 0x5, 0x6, 0x7, 0x10 |
| **`0x0042e100`** | **7** | **0x6, 0x10** |
| `0x004034f0` | 5 | 0x2, + registradores |
| `0x00438b40` | 4 | so' registradores |
| `0x00413080` | 3 | 0x2 |
| `0x00417400` | 2 | 0x2 |
| `0x00436fc0` | 2 | 0x1e, ECX |
| `0x00402130` | 1 | 0x8 |
| `0x004025d0` | 1 | 0x8 |
| `0x0043a860` | 1 | ECX |

**Correcao do `AUDITORIA_SUPERFICIE.md` item 10:** ele registra "5 estao em
`FUN_0042e100`". Sao **7**, com endereco:

```
0x0042e507  engine_random(_, 6)     0x0042f439  engine_random(_, 16)
0x0042f4d3  engine_random(_, 16)    0x0042f544  engine_random(_, 16)
0x0042f65a  engine_random(_, 6)     0x00430465  engine_random(_, 16)
0x0043145e  engine_random(_, 6)
```

Os sete gravam o resultado em `+0x70` (`frame_id`) da vitima, nos ramos de
agarrao desfeito e de vitima nao-personagem (`+0x6f8` = 1, 2, 4, 6). O que cada
um decide caso a caso ainda **nao** foi reconstruido — segue aberto.

Tambem confirmado: `does_attack_success` (`0x00417400`) tem 2 sitios, e nao 0
como o item 10 supunha. O update por tick continua com 0.

---

## A21 — ~~O determinismo e' de graca~~ · **REFUTADO por A25/A26**

> Esta secao afirmava que a sequencia do original era identica em toda execucao.
> **Esta' errada.** Os cursores realmente comecam em zero e nunca sao semeados —
> isso continua valendo. Mas a TABELA e' gerada a cada boot a partir de um
> `rand()` semeado por tempo (A25/A26), entao a sequencia muda por execucao.
> O que sobreviveu do raciocinio abaixo esta' marcado; o resto, nao.

## A21 (texto original, parcialmente invalido)

`0x00450c34` e `0x00450bcc` vivem no BSS, comecam em zero e so' sao escritos
dentro do proprio `FUN_00417170` (`+1` por chamada, sempre, independentemente
do sitio). Nao ha' semeadura por tempo em lugar nenhum do caminho.

Consequencias, e sao grandes:

1. **A sequencia do original e' identica em toda execucao.** Nao e' preciso
   fixar cursor para comparar tracos — o `ORACULO.md` §2 supunha o contrario e
   esta' corrigido.
2. **O que importa nao e' o valor, e' a ORDEM e a CONTAGEM de chamadas.** Uma
   chamada a mais ou a menos no porte dessincroniza tudo que vem depois, e a
   divergencia aparece longe da causa.
3. Por isso a coluna `rng_cursor` do traco deixa de ser conveniencia e passa a
   ser **o instrumento principal**: cursor divergente antes de campo de fisica
   divergente significa sitio de decisao faltando, nao formula errada.

---

## A22 — Existe uma SEGUNDA fonte, nao-deterministica, e ela e' cosmetica

`rand()` da libc, semeada em `0x0043cf40` com `srand(timeGetTime())`.

Apenas **3 sitios** em todo o binario: `0x004146b0`, `0x00422ac0` e
`0x0042e100`. No `0042e100` os dois usos ficam no bloco `LAB_0043187a` e
espalham a posicao da faisca de impacto (`rand() % 9 - 4`, ou seja +-4 px em
x e y).

Separacao limpa, e e' boa noticia: **a jogabilidade roda na tabela
deterministica; so' o enfeite roda no `rand()` semeado por tempo.** Os 3 sitios
de `rand()` ficam de fora do traco por construcao — incluir qualquer um deles
produziria divergencia falsa a cada execucao.

---

## A23 — ~~A tabela vem pela rede~~ · **REFUTADO pela memoria do processo**

> `tools/probe_rng_table.py` num `lf2.exe` vivo, single-player, cursores ainda
> em zero: **3000 dos 3001 bytes nao-zero**. A tabela esta' preenchida antes de
> qualquer sorteio. A conclusao "permanece zerada offline" caiu.
>
> O que era Nivel A e continua de pe': a tabela mora em BSS e nao existe no
> arquivo; ela trafega por `send`/`recv` no netplay. O que era **inferencia por
> ausencia** — "nao existe gerador, logo fica zerada" — era falso, e o gerador
> aparece em A25.
>
> Licao registrada, porque e' a tese do projeto se aplicando a si mesma:
> argumento por ausencia sobre varredura estatica nao e' evidencia. A leitura
> de memoria custou trinta segundos e derrubou duas secoes.

## A23 (texto original, parcialmente invalido)

O plano original desta sessao era extrair os 3000 bytes de `0x0044ff90` do
arquivo. **Nao da'**, e a razao e' verificavel em trinta segundos no cabecalho PE:

| secao | VA | VSize | RawSize |
|---|---|---:|---:|
| `.data` | `0x0044d000` | 50980 | **8192** |

`0x0044ff90` esta' no delta **12176** da `.data`, e o raw da secao tem 8192
bytes. O endereco cai **alem** do que existe no arquivo: e' BSS, zerado no load.
Os dois cursores (`0x00450bcc`, `0x00450c34`) idem.

Quem preenche, entao? As referencias literais a `0x44ff90` no `.text` inteiro
sao **cinco**, e todas se explicam:

| endereco | instrucao | papel |
|---|---|---|
| `0x004171a7` | `MOVZX EAX,byte ptr [EDX + 0x44ff90]` | a leitura do RNG (A19) |
| `0x0043e3e7` | `MOV byte ptr [ECX + 0x44ff90],DL` | copia **de** um blob de estado |
| `0x0043da30` | `MOV DL,byte ptr [EAX + 0x44ff90]` | copia **para** o blob |
| `0x0040304a` | `PUSH 0x44ff90` | argumento de `send` |
| `0x00428679` | `PUSH 0x44ff90` | argumento de `recv` |

Os dois ultimos fecham a questao. `0x0043f3cc` e `0x0043f3c6` sao thunks
(`JMP dword ptr [0x00447294]` / `[0x00447298]`) para **`send`** e **`recv`** do
`WSOCK32.DLL`, e a pilha bate com a assinatura Winsock:

```
0040303d  MOV  ECX,[0x0044f46c]    ; o socket
00403043  PUSH 0x0                 ; flags
00403045  PUSH 0xbb9               ; len = 3001
0040304a  PUSH 0x44ff90            ; buf = a tabela
0040304f  PUSH ECX
00403050  CALL send
```

`0x00402ec0` guarda o socket em `0x0044f46c` e **envia**; `0x004246b0` guarda o
socket e **recebe**. A tabela e' a semente compartilhada do **netplay**: o host
manda, o cliente recebe, e os dois lados passam a sortear igual. E' lockstep.

**Nao existe gerador.** Nenhuma das cinco referencias escreve conteudo novo — as
duas copias movem bytes de e para um blob, e o blob vem da rede.

### Consequencia, e ela e' grande

Em single-player a tabela **permanece zerada**, e o RNG degenera:

```c
return (0 + cursorA) % n;      // ou seja:  cursorA % n
```

Com `cursorA` andando 1, 2, 3, … 1233, 0, 1, … O "aleatorio" do LF2 offline e'
um **contador modulo n**. O porte nao precisa da tabela para nada enquanto nao
houver rede — precisa apenas dos dois cursores e da ordem das chamadas.

**Nivel de evidencia, explicitado.** O mapa estatico e' Nivel A e esta'
completo: cinco referencias, todas atribuidas. A afirmacao "permanece zerada em
single-player" e' **inferencia forte, nao observacao** — ela decorre de nao
haver gerador, o que e' um argumento por ausencia. Por isso vai junto
`tools/probe_rng_table.py`, que le' a memoria de um `lf2.exe` em execucao e
confirma ou derruba em uma rodada. **Rodar antes de confiar.**

---

## A24 — Detalhe de 3001 vs 3000, que nao e' erro de leitura

O laco de copia em `0x0043e3e0`-`0x0043e3f6`:

```
0043e3f0  CMP ECX,0xbb9      ; 3001
0043e3f6  JL  0x0043e3e0
```

`ECX` corre de 0 a 3000 inclusive: **3001 bytes** copiados e transmitidos. Mas o
RNG indexa `cursorB % 3000` (`0xbb8`), entao o byte de indice 3000 nunca e'
lido. O buffer do porte tem 3001 bytes de proposito, para que o dia do netplay
nao vire um off-by-one silencioso.

Junto, em `0x0043e3cb`: `MOV [0x00450bcc],ECX` restaura o **cursorB** a partir
do mesmo blob, imediatamente antes da tabela. Cursor e tabela viajam juntos —
o que faz sentido, porque so' os dois juntos definem a proxima sorteada.

---

## A25 — O gerador: `FUN_00422ac0`, e por que dois scans o perderam

18 instrucoes, `0x00422ac0`-`0x00422af7`, integral:

```
00422ac2  MOV  EDI,dword ptr [0x00447198]   ; PTR_rand
00422ac8  XOR  ESI,ESI                      ; i = 0
00422aca  LEA  EBX,[EBX]                    ; nop de alinhamento, sem semantica
00422ad0  CALL EDI                          ; rand()
00422ad2  CDQ
00422ad3  MOV  ECX,0xff                     ; 255
00422ad8  IDIV ECX                          ; EDX = rand() % 255
00422ada  ADD  ESI,0x1                      ; ++i  ANTES do store
00422add  ADD  DL,0x1                       ; 1..255
00422ae0  CMP  ESI,0xbb8                    ; 3000
00422ae6  MOV  byte ptr [ESI + 0x44ff8f],DL
00422aec  JL   0x00422ad0
00422aef  MOV  byte ptr [0x00450b48],0x0    ; terminador
```

```c
for (int i = 0; i < 3000; ++i) table[i] = rand() % 255 + 1;
table[3000] = 0;
```

Tres detalhes que importam:

- **Faixa 1..255, nunca 0.** Nao e' estilo: o buffer e' terminado em nulo e
  trafega inteiro num `send` de 3001 bytes. Um zero no meio truncaria o pacote.
- **`ESI` e' pre-incrementado**, entao o literal codificado na instrucao de
  store e' `0x0044ff8f` — **um byte abaixo** da tabela. Na primeira iteracao
  `ESI` vale 1 e o destino da' `0x0044ff90`, correto.
- Chamado de `0x004246b0`, duas vezes.

### Por que a varredura estatica nao achou

Duas buscas, ambas erradas por um byte, na mesma direcao:

1. Busca pelo literal `0x0044ff90` (`90 ff 44 00`) nos bytes do arquivo: achou
   5 ocorrencias, e elas realmente sao as unicas. Mas o gerador codifica
   `0x0044ff8f` (`8f ff 44 00`).
2. Busca por literais dentro da faixa da tabela: usava `lo = 0x0044ff90`, o que
   exclui `0x0044ff8f` por exatamente um byte.

Nao foi a analise nao-agressiva do Ghidra que causou isto — `FUN_00422ac0`
estava exportada e disponivel o tempo todo. Foi o predicado da busca. Registrado
porque a correcao generaliza: **ao caçar quem escreve num buffer, procurar a
faixa `[base - 16, base + tamanho)`**, nunca a base exata. Enderecamento com
indice pre-incrementado e' idioma comum de compilador, e desloca o literal.

---

## A25b — A tabela e' de BOOT, nao de partida · **MEDIDO**

A A25 deixou em aberto se `FUN_00422ac0` roda por boot ou por partida, ja' que
os dois sitios de chamada estao em `0x004246b0`. Resolvido por medicao, nao por
leitura.

`tools/probe_rng_table.py`, **duas partidas no mesmo processo** (pid 28408),
tres leituras em cada:

| leitura | cursorA | cursorB | primeiros bytes da tabela |
|---|---:|---:|---|
| 1 | 183 | 325 | 209, 182, 175, 242, 126, 111, 219, 184 |
| 2 | 379 | 1755 | *identicos* |
| 3 | 45 | 2655 | *identicos* |
| 4 | 300 | 205 | *identicos* |
| 5 | 873 | 778 | *identicos* |
| 6 | 99 | 1238 | *identicos* |

Os cursores andaram milhares de posicoes — `cursorB` chegou a envolver
(2655 → 205) — e **a tabela nao mudou um byte**. Ela e' preenchida uma vez, no
boot.

**Ressalva registrada:** a troca de partida se deu por morte do personagem, nao
pelo menu. Sao partidas distintas — os cursores confirmam, com milhares de
sorteios entre elas — mas nao esta' descartado que um caminho de menu invoque
`FUN_00422ac0` de novo. A decisao de implementacao seria a mesma de todo jeito:
como o `rand()` e' semeado por relogio, nem o original reproduz a propria
sequencia, entao boot ou partida e' indiferente para jogabilidade — so' importa
para o traco, e la' a tabela e' capturada e injetada de qualquer maneira.

O `fillFromRand()` do porte esta' em `main()`, nao em `resetGame()`.

### O dado incomodo que essas leituras trazem de brinde

`cursorB` em 1755, 2655, 1238 significa **milhares de sorteios por partida**. O
porte hoje tem **2 dos 264 sitios** implementados (A20). A consequencia para o
`ORACULO.md` e' concreta e vale dizer antes de alguem se decepcionar com o
differ: enquanto os outros 262 nao existirem, o traco do porte diverge do
original quase imediatamente, e a coluna `rng_cursor` vai acusar isso no lugar
de qualquer erro de formula. **Cobertura de sitios de RNG e' pre-requisito da
Fase 2**, nao um refinamento posterior.

---

## A26 — A consequencia: o original NAO e' reproduzivel entre execucoes

`rand()` e' semeado com `srand(timeGetTime())` em `0x0043cf40` (A22). O gerador
consome esse `rand()`. Logo **a tabela e' diferente a cada boot**, e a sequencia
de sorteios do engine tambem.

Isso derruba o A21 e reabre, numa forma nova, a secao 2 do `ORACULO.md`:

- Nao adianta fixar cursor: eles ja' comecam em zero. O que varia e' a tabela.
- Para traco diferencial e' preciso **capturar os 3000 bytes do processo vivo**
  (o `probe_rng_table.py` ja' faz) e **injetar no porte** via `loadTable()`.
  Sem isso os dois lados sorteiam diferente e o differ so' produz ruido.
- Fora do traco, o porte nao precisa reproduzir a sequencia do original —
  **o proprio original nao reproduz a dele.** Precisa reproduzir o
  *mecanismo*: mesma faixa 1..255, mesmos modulos 1234/3000, mesma ordem e
  contagem de chamadas.

O `loadTable()` de `src/engine/random.hpp` foi escrito para netplay. Acabou
sendo necessario para o single-player tambem, por outra razao.

---

## O que isto muda no porte

O item 10 deixa de ser "mecanismo inteiro sem contrapartida" e vira tarefa com
escopo fechado:

```c
// engine_random.hpp  — os tres enderecos, sem etiqueta
static int  g_cursorA = 0;   // 0x00450c34
static int  g_cursorB = 0;   // 0x00450bcc
static const unsigned char kTable[3000] = { /* extrair de 0x0044ff90 */ };

inline int engineRandom(int n) {
    if (n < 1) return 0;
    g_cursorA = (g_cursorA + 1) % 1234;
    g_cursorB = (g_cursorB + 1) % 3000;
    return (kTable[g_cursorB] + g_cursorA) % n;
}
```

Implementado em `src/engine/random.hpp`, com `tests/test_random.cpp`
(100.070 CHECKs verdes). A tabela fica zerada, que e' o comportamento do
original offline (A23); `loadTable()` existe so' para o dia do netplay.

**Antes de confiar, rode `tools/probe_rng_table.py`** com o LF2 aberto numa
partida. Se ele acusar bytes nao-zero, existe um gerador que a analise estatica
nao achou, e o caminho simplificado esta' errado.

Depois disso, os 7 sitios de `FUN_0042e100` sao os primeiros a entrar, porque
sao os unicos dentro de codigo que o porte ja' executa hoje.
