> ## ⚠ PLANO, não evidência
>
> Este arquivo descreve um método a construir. Nenhum número aqui é fonte de
> mecânica — para isso, `AUDITORIA_*.md` (com endereço). O que este documento
> propõe é uma **quarta camada de evidência**, acima do assembly lido à mão:
> comportamento observado no `lf2.exe` **em execução**.
>
> Precedência atual: Nível A (assembly, com endereço) > Nível B (`lf2_decomp.c`,
> só índice) > Nível C (F.LF / OpenLF2 / intuição — presumido errado).
> O que este plano cria é o **Nível A′**: traço de execução do original.

# Oráculo de execução — plano

## 0. A pergunta que originou isto

> "Achei que a decompilação completa do LF2 base fosse mais proveitosa, porque
> às vezes ficamos cegos."

A cegueira é real e está medida. `AUDITORIA_SUPERFICIE.md` registra **13 de 13**
parâmetros de Nível C refutados quando finalmente conferidos, e três deles
erraram o *mecanismo*, não o valor. Pior: o item 10 diz que o engine tem **264
sítios de decisão aleatória** e que 259 nunca foram mapeados, e o item 1 admite
que ninguém sabe **como o original sai do state 18** — `BURN_TICKS = 36` pode
estar modelando algo que não existe. Isso não é dúvida sobre um número. É não
saber o que não se sabe.

**Mas decompilação completa não cura isso, e o próprio `tools/DECOMPILE.md` já
explica por quê:** 187 dos 481 `FUN_` gerados nem são entrada de função; o melhor
decompilador assistido publicado atinge ~65% de re-executabilidade no caso mais
fácil (libc com gcc), e o nosso alvo é MSVC 8.0 com FPO. Uma decomp completa
produz **Nível B em escala industrial** — mais do artefato que o projeto já
declarou não-normativo. Não se sai da cegueira lendo 481 funções de pseudocódigo
que erram uma em três; ganha-se 481 hipóteses plausíveis e nenhum modo de
falsificá-las. É exatamente a máquina que produziu os 13 erros, com throughput
maior.

**A diferença entre Nível B e Nível A nunca foi detalhe. Foi falsificabilidade.**
Pseudocódigo do Ghidra não roda. O assembly, lido à mão, responde só a perguntas
que alguém teve a ideia de fazer — e a lista de perguntas é justamente o ponto
cego.

O que cura cegueira é **ground truth executável**: o `lf2.exe` está instalado
nesta máquina (`C:\Program Files (x86)\LittleFighter`) e pode ser instrumentado.
Se ele emitir, tick a tick, o estado dos objetos, e o porte emitir o mesmo
registro, então a pergunta deixa de ser "o que será que fizemos errado" e passa a
ser "**no tick 417 o campo `fall` divergiu**". Divergência com número de tick e
nome de campo é um bug com endereço. E aparece sem ninguém ter suspeitado dele
— que é a definição de sair do ponto cego.

Decomp completa responde perguntas feitas. Traço diferencial **faz as perguntas**.

---

## 1. O que é o oráculo

Três artefatos, nesta ordem de valor:

1. **Traço do original** — o `lf2.exe` real, instrumentado, cuspindo um registro
   fixo por tick.
2. **Differ** — alinhamento traço-original × traço-porte, primeira divergência
   por campo.
3. **Oráculo propriamente dito** — reimplementação fiel-a-função, em C, das
   cadeias onde o differ apontar mecanismo desconhecido; validada por
   **reproduzir o traço gravado**, não por parecer certa.

Só o artefato 3 é "decompilação", e ele entra por último e por demanda. Os
artefatos 1 e 2 dão a maior parte do ganho e não dependem de reversão nenhuma.

**Regra que define tudo:** um oráculo que não é validado contra traço gravado
não é oráculo — é um quarto F.LF. Seria Nível C com aparência de Nível A, e o
projeto já sabe o que esse artefato custa.

---

## 2. A trava: determinismo — capturar e injetar a tabela

> **Terceira versao desta secao, e vale registrar as duas anteriores.**
> A primeira dizia "fixe os cursores por escrita de memoria" — errado, os
> cursores ja' nascem em zero. A segunda dizia "nao ha' nada a fixar, o
> determinismo e' de graca" — tambem errado, e por argumento por ausencia sobre
> uma varredura estatica. So' a leitura de memoria do processo vivo resolveu.
> A licao e' a tese deste documento se aplicando a ele mesmo: **evidencia de
> execucao decide; varredura estatica levanta hipotese.**

O estado real (A19, A25, A26):

- Os cursores `0x00450c34` e `0x00450bcc` comecam em zero e avancam `+1` por
  chamada. Nao ha' o que fixar neles.
- A tabela de 3000 bytes em `0x0044ff90` e' **gerada a cada boot** por
  `FUN_00422ac0`, com `rand()` semeado por `timeGetTime()`. Ela muda a cada
  execucao.
- Portanto **o original nao reproduz a propria sequencia entre execucoes**.

O procedimento para o traco diferencial, entao:

1. Iniciar o `lf2.exe`, chegar ao ponto de captura.
2. **Ler os 3001 bytes** de `0x0044ff90` e os dois cursores
   (`tools/probe_rng_table.py` ja' faz exatamente isso).
3. Gravar a tabela junto do traco — ela e' parte da condicao inicial, tanto
   quanto a entrada roteirizada.
4. No porte, `engineRandom().loadTable(...)` e `setCursors(...)` antes do
   primeiro tick.

Sem esse passo os dois lados sorteiam valores diferentes desde a primeira
decisao, e o differ vira gerador de ruido.

Continua valendo, e agora com mais peso: **o que precisa bater e' ordem e
contagem de chamadas.** Uma chamada a mais ou a menos dessincroniza tudo depois.
Por isso `rng_cursor_1234` e `rng_cursor_3000` sao a coluna mais importante do
traco — cursor divergente antes de campo de fisica divergente aponta sitio de
decisao faltando, nao formula errada.

Os **3 sitios de `rand()` da libc** (A22) continuam fora do traco: sao jitter
cosmetico de faisca (+-4 px). Um deles, porem, e' o proprio gerador da tabela —
consumido uma vez no boot, antes de qualquer tick.

---

## 3. Fase 0 — traço do original

O ponto de maior risco e maior retorno. Fazer primeiro, sozinho, e não seguir
enquanto não sair.

### 0.1 Fechar o layout do `object` — as fontes discordam

Antes de dumpar qualquer coisa é preciso um layout único, e hoje não existe:

| Campo | `AUDITORIA_*` (assembly) | OpenLF2 `object.h` |
|---|---|---|
| `x_velocity` | `+0x28` | `0x40` |
| `x_position` | `+0x58` | `0x10` |
| `frame_id` | `+0x70` | `0x70` |

O `frame_id` bate; os outros dois não. A hipótese mais provável é **duas bases
diferentes** — nas rotinas auditadas aparecem `0x70(%eax)` e `0x88(%ecx)` no
mesmo trecho, o que sugere que `%eax` e `%ecx` apontam para structs distintas
(objeto vs. estado de frame), e que o `object.h` do OpenLF2 se refere a uma
delas. **Resolver isso é o passo 0.1 e não é opcional**: um dump com base errada
produz traço que diverge por construção e queima a confiança no método logo na
primeira rodada.

Saída: `tools/OBJECT_LAYOUT.md`, um offset por linha, cada um com endereço de
instrução que o comprova, e a base explicitada.

### 0.2 O gravador

Duas rotas; escolher pela que der traço primeiro, não pela mais elegante.

- **DLL injetada** — hook no update por tick, escreve registro em arquivo. O
  `reference/openlf2-master` é literalmente o LF2 com DLL de hook injetada;
  serve de molde de infraestrutura, não de fonte de mecânica.
- **Script de debugger** (x64dbg / Frida / pykd) — breakpoint no update, dump por
  script. Mais lento, muito mais rápido de colocar de pé. **Preferir para a
  primeira prova de vida.**

### 0.3 Entrada roteirizada

O traço precisa ser reproduzível. Injetar a sequência de input no ponto de
leitura do original (hook), não pelo teclado do Windows — teclado introduz
jitter de tempo e mata a reprodutibilidade.

### 0.4 Esquema do registro

Um CSV, uma linha por (tick, objeto). Colunas fixas, versionadas no cabeçalho.
Mínimo para cobrir o que está aberto hoje:

```
tick, obj_id, type, frame_id, frame_wait, state, facing,
x, y, z, vx, vy, vz, accum_y(+0x30),
hp, mp, recover_cap(+0x300),
fall(+0xB0), shaking(+0xB4), bdefend(+0xB8),
rng_cursor_1234, rng_cursor_3000
```

`rng_cursor_*` no traço não é curiosidade: é o canário. Se os cursores divergirem
antes dos campos de física, a divergência é de consumo de aleatoriedade — sítio
de decisão que o porte não tem — e não erro de fórmula. Esse único campo separa
duas classes de bug que hoje seriam confundidas.

**Critério de saída da Fase 0:** duas execuções do original, mesma entrada,
mesmos cursores fixados, produzem traços **byte-idênticos**. Enquanto isso não
acontecer, não existe oráculo — existe ruído.

---

## 4. Fase 1 — traço do porte

Barato, e a infraestrutura já está pronta. O `make -f Makefile.host harness` já
roda o `main.cpp` real, headless, 1 tick por iteração, com `scriptedInput()`.

O que falta:

- `-DLF2_TRACE` emitindo o **mesmo esquema** da Fase 0;
- `scriptedInput()` passa a ler o roteiro de arquivo, o mesmo consumido pelo
  hook do original (hoje ele é fixo e "deliberadamente burro" — está no
  comentário do `main.cpp:319`);
- `engine_random` portado com a tabela e os cursores.

---

## 5. Fase 2 — o differ

`tools/tracediff.py`. Alinha por `tick` e `obj_id`, reporta a **primeira**
divergência por campo, com contexto de N ticks antes.

Saída desejada, e é o produto inteiro do plano:

```
tick 417  obj 1  campo bdefend: original=12  porte=0   (primeira divergência)
  contexto: tick 415 itr conectou, injury=30 → esperado injury/10 = 3
```

Tolerâncias por classe de campo — e aqui mora o risco que mata o plano ingênuo,
tratado na seção 8: inteiros exatos, floats com épsilon.

**Modo de operação:** rodar o differ é a nova pergunta padrão. Em vez de "será
que o `FRICTION` está certo?", roda-se o traço de locomoção e o differ diz. E
diz também sobre os campos que ninguém pensou em perguntar.

---

## 6. Fase 3 — o oráculo, e onde o `auto-re-agent` entra

Só depois que o differ apontar divergência **cujo mecanismo não é óbvio no
assembly**. Exemplo canônico já disponível: a saída do state 18 (item 1 do
`AUDITORIA_SUPERFICIE.md`), onde a busca exaustiva mostrou que não há cronômetro
e a pergunta ficou em aberto.

Nesses casos, reconstruir a cadeia de funções em `reference/oracle/`:

- C fiel-a-função, um arquivo por função, nome `fn_0042e100.c`;
- árvore **separada** do `src/`, compilável no host, nunca ligada ao `.vpk`;
- mapa endereço→função 1:1 **por construção** — que é a peça que falta ao `src/`
  e a razão pela qual o `auto-re-agent` não serve lá.

Este é o único contexto em que o `auto-re-agent` encaixa: profile `generic-cpp`,
`source_root: reference/oracle`, `build_commands` apontando para um alvo host
dedicado. E com uma substituição essencial: **o critério de aceitação não são os
11 sinais heurísticos dele** — que detectam "a LLM escreveu um stub", falha que
não é a nossa. O critério é **reproduzir o traço gravado**: alimenta-se o oráculo
com as entradas registradas, e ele tem que devolver as saídas registradas. Os 11
sinais ficam como filtro barato de primeira passada, não como veredito.

---

## 7. Ordem de ataque

Por dependência primeiro, risco depois:

| # | Alvo | Por quê | Estado |
|---|---|---|---|
| 1 | `engine_random` (`FUN_00417170`, tabela `0x44FF90`) | pré-requisito de determinismo; fecha item 10 | engine em Nível A; porte zerado |
| 2 | Layout do `object` | pré-requisito do dump; fontes discordam | **conflito aberto** |
| 3 | Cadeia de acerto `FUN_0042e100` | concentra `fall`/`bdefend`/`shaking`/acumulador `+0x30`; 5 dos 264 sítios de RNG | muito já em Nível A |
| 4 | Saída do state 18 (`BURN_TICKS`) | pergunta declarada sem resposta | **aberto** |
| 5 | Locomoção (`FRICTION`, `MIN_SPEED`) | Nível C, afeta tudo, valores redondos suspeitos | risco médio |
| 6 | `Z_MIN`/`Z_MAX` vs `zboundary` do `bg.dat` | constante onde deveria haver dado | risco baixo |

Itens 5 e 6 provavelmente caem **sem oráculo**, só com o differ da Fase 2. Esse é
o sinal de que o plano está funcionando: a Fase 3 deve ser rara.

---

## 8. Riscos que matam o plano

**8.1 x87 vs SSE — o mais grave.** O original é MSVC 8.0 x86: aritmética em
x87, com intermediários de **80 bits**. O porte host é g++ x86-64 com SSE2, 64
bits. Os mesmos `fadd`/`fmul` na mesma ordem dão resultados diferentes no último
bit, e o erro acumula ao longo dos ticks. **Comparação bit-a-bit de float vai
falhar por motivo errado** e produzir uma enxurrada de falsos positivos capaz de
enterrar o método na primeira semana.

Mitigação, em camadas:

- differ com épsilon por campo, e o épsilon é **dado do projeto**, não chute:
  calibra-se rodando dois traços do original entre si (deve dar zero) e depois
  original × porte num cenário já validado;
- o **oráculo** (Fase 3) compila `-m32 -mfpmath=387 -ffloat-store` para casar a
  semântica x87 — lá a igualdade estrita é alcançável e deve ser exigida;
- campos inteiros (`fall`, `bdefend`, `frame_id`, `hp`, `mp`, cursores) são
  comparados **exatos, sempre**. E é justamente onde estão quase todos os itens
  abertos. Na prática o differ útil é o de inteiros; os floats são o ruído.

**8.2 O oráculo virar um quarto F.LF.** Endereçado pela regra da seção 1: sem
traço reproduzido, o artefato não é promovido a nada. Vale registrar que este é
o modo de falha mais provável, porque é o mais confortável.

**8.3 Alinhamento temporal.** O original roda no timer dele; o traço tem que ser
indexado por tick lógico do jogo, não por tempo de parede, e o hook precisa
disparar no mesmo ponto do ciclo todas as vezes.

**8.4 Custo de entrada da Fase 0.** É a única fase que exige ferramenta nova
(hook/debugger no Windows) e é a que pode não sair. Por isso o critério de saída
da seção 3 é binário: dois traços idênticos, ou o plano não avança.

**8.5 Escopo.** O LF2 é freeware, não open source. O oráculo é artefato de
verificação: fica em `reference/` (já no `.gitignore`), fora do `.vpk` e fora do
que se distribui.

---

## 9. Critério de sucesso

Não é "o oráculo compila". É:

1. dois traços do original, mesma entrada → idênticos (Fase 0);
2. differ acusa, em cenário conhecido, uma divergência **já documentada** nas
   auditorias — a prova de que ele enxerga o que sabemos que existe;
3. differ acusa uma divergência **não documentada**;
4. essa divergência vira achado com endereço em `AUDITORIA_*.md`, é corrigida, e
   o differ para de acusá-la.

O item 3 é o que responde à pergunta da seção 0. Até ele acontecer, este plano é
uma aposta; depois dele, é a ferramenta principal do projeto.

---

## 10. O que não fazer

- Não rodar `auto-re-agent` contra `src/`. Não há mapa endereço→função ali, e os
  40 endereços presentes são comentário de evidência, não identidade — em
  `player.hpp:87` uma linha cita duas rotinas do original.
- Não gerar decomp completa como entregável. Já existe `lf2_decomp.c` (4,5 MB) e
  o `FUNCOES_FALSAS.txt` ao lado dele registra por quê ele é índice.
- Não promover nada do oráculo a `AUDITORIA_*.md` sem endereço **e** traço.
- Não começar pela Fase 3 porque é a mais interessante. Sem as fases 0-2 ela é
  geração de hipótese plausível — o vício que o `AUDITORIA_SUPERFICIE.md` chama
  de "erro de processo" e que já custou 13 parâmetros.
