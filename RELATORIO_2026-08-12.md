# LF2 Vita — Relatório de sessão e ponto de retomada · 2026-08-12

> **Este arquivo é o ponto de entrada para a próxima sessão.** Ele resume o que
> foi feito, por que o método é o que é, e o que fazer a seguir. Os detalhes
> ficam nos documentos citados — este aqui é o índice narrativo, não a fonte.

---

## 1. O que é o projeto

Port nativo do Little Fighter 2 para PS Vita, em C++17, **interpretando os `.dat`
originais em runtime**. Nenhuma lógica de personagem hardcoded: um `Fighter`
genérico anda o grafo de frames de qualquer arquivo, e a diferença entre Dennis e
Firen está 100% nos dados.

Binário de referência: `reference/decomp/lf2.exe` — **LF2 v2.0a**, build de
2009-07-10 17:15:35 UTC, PE32 i386, **Visual Studio 2005 SP1** (build 50727,
confirmado pelo Rich header nesta sessão), SHA256
`12dfa00f6b767508612550e9ab27ab74b4201ff4cb9ff31d068925924eab8fc5`.

---

## 2. Como chegamos no método atual — e por que ele é assim

Esta é a parte que não pode se perder na troca de sessão, porque ela é o motivo
de todo o resto.

### O ponto de partida

A hierarquia de fontes definida no início do projeto:

```
lf2.exe (assembly)  >  lf2_decomp.c (Ghidra)  >  OpenLF2  >  F.LF
```

No começo isso era uma preferência declarada. Na prática, o porte era escrito com
valores do F.LF, de documentação de comunidade e de intuição — porque era mais
rápido, e porque "parecia certo".

### O dado que mudou tudo

Conforme os parâmetros foram sendo conferidos contra o assembly, apareceu isto
(tabela completa em `AUDITORIA_SUPERFICIE.md`):

| Parâmetro | Fonte | Veredito no assembly |
|---|---|---|
| gravidade de arma `0.45` | calibrado por captura de tela | errado → `0.425` |
| decaimento de fall `0.45`/TU | F.LF | errado → `1`/tick |
| limiar de tombo `fp > 60` | F.LF | errado → satura no piso da faixa |
| frames 222/224 por faixa | F.LF | errado → por **facing** |
| quebra de guarda `fall >= 60` | invenção | errado → `bdefend > 30` |
| golpe defendido = 0 dano | invenção | errado → `injury/10` |
| `arest`/`vrest` fundidos | invenção | errado → campos distintos |
| knockback zerado em re-acerto | invenção | errado → `dvx` sempre |
| frame de queda fixo 180 | invenção | errado → 180-183 por `vy` |

**Nove de nove.** Nenhum acerto.

Isso deixou de ser "preferência de fonte" e virou regra operacional:

> **Qualquer valor sem endereço do binário deve ser presumido errado até prova em
> contrário.** O ônus da prova é de quem afirma, não de quem duvida.

### A "Metodologia S"

Protocolo de auditoria que você definiu e que está em vigor:

1. Localizar a rotina no `lf2.exe` → desassemblar → reconstruir o fluxo → **só
   então** consultar decompilação → OpenLF2 se necessário → F.LF por último.
2. Níveis de evidência: **A** (disassembly), **B** (decompilação), **C**
   (OpenLF2), **D** (F.LF / inferência).
3. Cada achado: Identificador, Severidade, Comportamento do engine (sem
   mencionar o porte), Divergência, Reprodução, Evidência, Conclusão
   (Compatível / Divergente / Não foi possível comprovar / Inferência).
4. Mudanças de código vão numa seção separada, **"IMPLEMENTAÇÃO DO PORTE"**.
5. Testes e builds vão em **"VALIDAÇÃO DO PORTE (não é evidência de
   fidelidade)"** — passar em 238 testes não prova fidelidade a nada.

### Dois vícios de processo que já custaram caro

Estão em `AUDITORIA_SUPERFICIE.md` e valem releitura no início de cada sessão:

- **Corroboração falsa** — apresentar como confirmação algo que não sustenta a
  conclusão. Aconteceu com duas análises externas e com o meu próprio candidato
  `+0xEA`, que apontei porque decaía *perto* de `fall` e `bdefend`. Proximidade
  de vizinhança não é evidência.
- **Verificação que não verifica** — passar um comando de conferência baseado
  numa premissa não checada. Custou a pasta `assets/` (recuperada do backup).

---

## 3. O que esta sessão produziu

### 3.1 Achado A13 — a aleatoriedade do engine

O achado principal, em `AUDITORIA_2026-08-12.md#a13`.

**LF2 não usa `rand()` durante a partida.** Usa uma tabela de 3000 bytes:

```c
static unsigned char tabela[3000];  /* 0x0044FF90, string C com terminador */
static int cursorA;                 /* 0x00450C34 */
static int cursorB;                 /* 0x00450BCC */

int engine_random(int /* não lido */, int n) {   /* FUN_00417170 */
    if (n <= 0) return 0;
    cursorA = (cursorA + 1) % 1234;
    cursorB = (cursorB + 1) % 3000;
    return (tabela[cursorB] + cursorA) % n;
}
```

- `srand(timeGetTime())` é chamado **uma vez** em todo o binário (`0x0043cf5a`).
- `rand()` aparece em 4 sítios; um deles preenche a tabela (`FUN_00422ac0`).
- `FUN_00417170` é chamado em **264 sítios** — é a única fonte de aleatoriedade
  da simulação. **5 deles estão dentro de `FUN_0042e100`**, a rotina de aplicação
  de acerto que já auditamos.
- O primeiro argumento é um **ID de sítio**: 258 imediatos, todos distintos,
  faixa 2-296. Instrumentação de dessincronia deixada nas chamadas.

**Corolário.** A tabela e o cursor são serializados no `.lfr` (`0x0043da30`
grava, `0x0043e3e0` restaura). Por isso o replay exige `.dat` idênticos — o
arquivo guarda **entrada + semente**, e o engine **re-simula**. Uma partida do
LF2 é função determinística de (tabela, cursores, entradas, `.dat`).

**O porte tem zero fonte de aleatoriedade.** Não é bug hoje; vira bug no instante
em que IA de inimigo ou qualquer escolha ramificada for implementada com
`<random>`.

### 3.2 Identificação do binário reforçada

- **Rich header** presente e íntegro (chave `0x418e5e9a`). Build dominante
  **50727 = VS2005 SP1** — confirmação independente do manifesto e do campo de
  linker. Há objetos de toolchains bem mais antigas (builds 9466, 9178, 7299,
  4035), o que indica bibliotecas estáticas relinkadas.
- **Debug directory ausente** (RVA 0). Sem CodeView, sem caminho de PDB, sem
  nomes de fonte. Porta fechada — registrado para ninguém tentar de novo.

### 3.3 Avaliação de seis ferramentas de decompilação

Todas em `tools/BINARY_NOTES.md`. **Zero adoções**, dois scripts nossos:

| Ferramenta | Veredito | Rendeu |
|---|---|---|
| `AI-Decompiler` | não | fronteira de função por prólogo → `fn_boundary_check.py` |
| `decompai` | não | nada |
| `python-decompile3` | não | corroboração independente do Nível B |
| `LLM4Decompile` | não | 64,9% de re-executabilidade como teto medido |
| `Pepper` | não | Rich header e debug dir (§3.2) |
| `rdecomp` | não | inventário de deslocamento → `struct_harvest.py` |

A regra que saiu disso: **não instalamos decompilador. Lemos o que ele se propõe
a fazer e implementamos em cima do `objdump` a parte que ataca o gargalo.**

O número do `LLM4Decompile` merece destaque: o estado da arte, em funções C de
biblioteca padrão compiladas com gcc, produz código re-executável **64,9%** das
vezes. Um terço sai errado no caso fácil. É a justificativa quantitativa para o
Ghidra ser Nível B e não fonte.

### 3.4 Ferramenta nova — `tools/struct_harvest.py`

Inverte a descoberta de campos, que até agora era **reativa** (aparece um bug →
vamos ao assembly → achamos `+0xB8`). Varre um intervalo do `.text`, colhe todo
`disp(%reg)` com base != `esp`/`ebp`, e monta inventário com leituras, escritas,
largura provável e exemplo.

Primeira execução sobre as três rotinas já auditadas:

```
deslocamentos distintos: 113   ja provados: 29   candidatos: 84
```

Candidatos de maior volume:

| off | R | W | leitura provável |
|---|---|---|---|
| `0x194` | 289 | 0 | tabela global de objetos, `[esi + slot*4]` |
| `0x364` | 9 | 7 | vizinho de `+0x368` (`file`) |
| `0x354` | 8 | 4 | **índice de slot** — usado como índice em `+0x194` |
| `0x300`/`0x348`/`0x34C` | 0 | 2-3 | só escrita via `add` — acumuladores |
| `0xBE`-`0xC5` | — | — | oito bytes consecutivos, comparados com 5 |

`+0x354` já rendeu leitura: em `0x0042e9af`-`0x0042e9c3`, quando a vítima é
personagem e `+0x2f4 == -1`, o dano também é somado em `+0x348` do objeto
referenciado pelo `+0x354` do atacante — **atribuição de dano ao criador**.

**Ressalva obrigatória:** o script não sabe para qual struct o base aponta. No
bloco de acerto, `%edx` ora é `object_t`, ora é `itr`, ora é `frame`. Cada linha
da saída é **Nível D**. Ele economiza a busca, não a prova.

### 3.5 Limpeza feita

- `src/characters/dennis.hpp` e `src/characters/firen.hpp` — **removidos**.
  Eram as tabelas de stats/hitbox hardcoded que o `fighter.hpp` substituiu.
  Nada os incluía. Pior: continham constantes inventadas (`DENNIS_HP = 500`,
  `DENNIS_INJURY = 30`) que alguém poderia consultar como se fossem fonte.
  238 CHECKs continuam passando depois da remoção — confirmação de que eram
  código morto.
- `lf2vita.log` — artefato de runtime, removido do diretório de trabalho.
- `.gitignore` — o bloco de `assets/` estava **triplicado**, resíduo do incidente
  de recuperação. Deduplicado.

---

## 4. Estado atual

**Build:** 238 CHECKs, zero falhas. `check-main` contra headers SDL2 reais, OK.
Toolchain da Vita não existe na sandbox — o build do device é sempre seu.

**Não commitado:**

```
 M AUDITORIA_2026-08-12.md
 M AUDITORIA_SUPERFICIE.md
 M tools/BINARY_NOTES.md
 M .gitignore
 D src/characters/dennis.hpp
 D src/characters/firen.hpp
?? tools/fn_boundary_check.py
?? tools/struct_harvest.py
?? RELATORIO_2026-08-12.md
```

**Aberto, por prioridade** (detalhe em `AUDITORIA_SUPERFICIE.md`):

1. **Velocidades de tombo** — `launch(-8.f)` e `vy = -6.f`, ambos inventados.
   Risco ALTO: governam a sensação de todo knockdown. Candidatos no pool: `±6.0`
   (`0x447940`/`0x449af0`) e `±3.0` (`0x447a40`/`0x449050`).
2. **Como o original sai do state 18** (queimadura). O candidato `+0xEA` foi
   refutado por busca exaustiva; o sítio de ignição `0x0042fd76` não grava
   cronômetro nenhum. Hipótese em aberto: pode não existir contador.
3. `mp_start = 200` (F.LF) e regeneração de MP a cada 2 ticks (invenção pura).
4. Anti-juggle `fall <= 40` — Nível C, do OpenLF2, que já errou uma vez.
5. `FRICTION`/`MIN_SPEED`, `Z_MIN`/`Z_MAX`, `TICK_MS`.
6. A7 (teto de `arest` em 12) e A12 (`shaking`/hitstop, consumidor não isolado).
7. Os 84 candidatos do `struct_harvest.py`.

**Dívida de higiene** (`AUDITORIA_HIGIENE.md`, fazer **depois** do teste no
device): unificar os quatro caminhos de ataque de `main.cpp` num `resolveHit()`;
densidade de comentário de 41% em dois headers; idioma misturado PT/EN.

**Teste no device:** `TESTPLAN.md`, itens 1-39, nenhum executado nesta sessão.

---

## 5. Como abrir a próxima sessão

### Passo 1 — commitar o que está pendente

```bash
cd /mnt/c/Users/rodrigo.chiesa/Documents/LittleFighter2Vita
git add -A
git commit -m "A13: aleatoriedade do engine e' tabela de 3000 bytes (0x44FF90) consumida por FUN_00417170 com dois cursores, serializada no .lfr; Rich header confirma VS2005 SP1 e debug dir ausente; struct_harvest.py inverte a descoberta de campos (84 candidatos); avaliacao de Pepper e rdecomp; remove dennis.hpp/firen.hpp orfaos"
git push
```

### Passo 2 — o prompt de abertura

Um prompt só, já com o plano da sessão embutido. Copiar inteiro:

```
Projeto LF2 Vita, pasta LittleFighter2Vita. Port nativo do Little Fighter 2 para
PS Vita em C++17, interpretando os .dat originais em runtime.

═══ LEITURA OBRIGATÓRIA, NESTA ORDEM ═══
  1. RELATORIO_2026-08-12.md    índice da sessão anterior e o método
  2. AUDITORIA_SUPERFICIE.md    taxa-base de 9/9 e os dois vícios de processo
  3. AUDITORIA_2026-08-12.md    achados A8-A13 e identificação do binário
  4. tools/BINARY_NOTES.md      receitas de leitura do binário
(AUDITORIA_2026-07-30.md tem A1-A7; leia se o assunto encostar neles.)

═══ PRECEDÊNCIA ═══
Os 13 .md do repo NÃO concordam entre si, e isso é conhecido. Quando dois
discordarem:
  1. AUDITORIA_*.md            achado com endereço no binário — SEMPRE vence
  2. AUDITORIA_SUPERFICIE.md   diz o que ainda NÃO tem evidência
  3. RELATORIO_2026-08-12.md   índice e ponto de retomada
  4. HANDOFF / STATUS /        contexto, histórico e roadmap.
     CHECKLIST / TESTPLAN      NUNCA fonte de parâmetro de mecânica.
Os de nível 4 têm banner no topo dizendo isso. Número neles sem endereço do
binário é suspeito por padrão — cinco parâmetros refutados já estavam em
HANDOFF.md sob o título "VALIDADOS".

═══ MÉTODO (Metodologia S) ═══
Você é auditor de fidelidade, não implementador com pressa.

Ordem obrigatória das fontes:
  A  lf2.exe, disassembly (objdump)          única fonte primária
  B  reference/decomp/lf2_decomp.c           índice, NÃO fonte
  C  reference/OpenLF2/                      bom para NOMES de campo
  D  reference/F.LF/, docs de comunidade     último recurso

Localizar a rotina no lf2.exe → desassemblar → reconstruir o fluxo → só então
consultar decompilação. Declare o nível de evidência em cada afirmação.

Formato de achado: Identificador · Severidade · Comportamento do engine (sem
mencionar o porte) · Divergência encontrada · Reprodução · Evidência ·
Conclusão (Compatível / Divergente / Não foi possível comprovar / Inferência).

Mudanças de código vão em seção separada "IMPLEMENTAÇÃO DO PORTE".
Testes e build vão em "VALIDAÇÃO DO PORTE (não é evidência de fidelidade)".

Nada entra no porte sem endereço do binário. Se não der para provar, o veredito
é "não foi possível comprovar" e passa para o próximo item — não é para
preencher com inferência plausível. Proximidade de vizinhança não é evidência
(já custou o candidato +0xEA). Coincidência numérica não é evidência (o -8.0 em
0x448340 coincide com o nosso launch(-8.f) e é outra coisa).

═══ FERRAMENTAS NOSSAS — usar antes de desassemblar à mão ═══
  tools/struct_harvest.py     inventário de campos por deslocamento num
                              intervalo do .text. Saída é Nível D: o script não
                              sabe para qual struct o registrador-base aponta.
  tools/fn_boundary_check.py  187 dos 481 FUN_ do lf2_decomp.c não são entrada
                              de função. Rode antes de citar pseudocódigo.
  tools/datdump               parser dos .dat, imprime itr com arest/vrest.
  make -f Makefile.host test  238 CHECKs, host, segundos.
  make -f Makefile.host check-main   type-check contra headers SDL2 reais.

═══ PLANO DA SESSÃO — nesta ordem, avançando o máximo que der ═══

1. VELOCIDADES DE TOMBO  (item 2 de AUDITORIA_SUPERFICIE.md, RISCO ALTO)
   launch(-8.f) e vy = -6.f são invenção. Governam a sensação de todo knockdown.
   Alvo: FUN_0042e100 — a MESMA rotina de A8-A12. Leia os achados antes: a
   acumulação de fall, a saturação no piso da faixa e a seleção 222/224 já estão
   provadas com endereço; não redescubra.
   Comece por:
     python3 tools/struct_harvest.py reference/decomp/lf2.exe 0x42e100 0x430200
   Isolar que constante do pool o ramo de tombo carrega para y_velocity (+0x40)
   ou vy (+0x48). Candidatos, não evidência: ±6.0 em 0x447940/0x449af0 e ±3.0 em
   0x447a40/0x449050.

2. CAMPOS CANDIDATOS  (84 deslocamentos que o struct_harvest achou)
   Na ordem de volume: +0x194 (tabela global de objetos, 289 leituras), +0x354
   (índice de slot, usado como índice em +0x194), +0x300/+0x348/+0x34C
   (acumuladores, só escrita via add), e os oito bytes consecutivos em
   +0xBE-0xC5 (comparados com 5 em 0x40e17a).
   O que já se sabe de cada um está em RELATORIO §3.4, incluindo a leitura
   parcial de +0x354 em 0x0042e9af-0x0042e9c3.
   Cada campo só vira afirmação depois de confirmado no disassembly — o base
   pode ser object_t, itr, bdy ou frame.

3. mp_start E REGENERAÇÃO DE MP  (itens 4 e 5 da superfície)
   mp = 200 é F.LF; a regen de 1 a cada 2 ticks é invenção pura, sem fonte
   nenhuma. Ambos são movl na inicialização — baratos de verificar.

4. FECHAMENTO
   Atualizar AUDITORIA_2026-08-12.md (ou abrir AUDITORIA da data nova) com os
   achados, AUDITORIA_SUPERFICIE.md com o que saiu e o que entrou, e
   RELATORIO com o novo ponto de retomada.

Se travar num item, diga por que travou e vá para o próximo. Sessão que fecha um
item com prova vale mais que sessão que fecha três com inferência.

═══ FORA DO PLANO, MAS PRIORITÁRIO SE ACONTECER ═══
Se eu voltar com resultado de teste no device (TESTPLAN.md, 39 itens, nenhum
executado ainda), isso passa na frente de tudo.

═══ PROTOCOLO DE ENTREGA ═══
Toda vez que um arquivo mudar, me mande o comando de build. Quando fechar uma
versão major, me mande também o comando de commit.
```

---

## 6. Precedência entre documentos — e o conflito que já existia

São 13 arquivos `.md`, escritos ao longo de três semanas, e **eles não
concordam entre si**. Auditoria feita em 2026-08-12; três conflitos reais foram
encontrados e corrigidos:

**`HANDOFF.md` §4 era o pior.** A tabela se chamava *"Parâmetros de fidelidade
(VALIDADOS — não mexer sem fonte)"* e misturava, sob o mesmo carimbo de
validado, valor lido do binário (gravidade `1.7` @`0x48348`) com valor copiado
do F.LF (`fall` decai `0.45`/tick) e com invenção. **Cinco linhas erradas
estavam ali como assentadas** — as mesmas que depois compuseram a taxa-base de
9/9. Reescrita em três blocos: provados no binário com endereço, refutados, e
não verificados com nível de evidência declarado.

**`REVIEW_PROMPT.md` chamava o `lf2_decomp.c` de "fonte primária".** Contradizia
frontalmente a hierarquia atual, onde o decomp é Nível B — índice, não fonte.
Corrigido, com os dois motivos concretos: 187 dos 481 `FUN_` dele não são
entrada de função, e o melhor decompilador assistido publicado acerta 64,9% no
caso fácil.

**`TESTPLAN.md` item 2** usava "queima 36 TU" como critério de aprovação, quando
`BURN_TICKS = 36` é Nível D e o candidato `+0xEA` foi refutado. O critério virou
"anotar a duração observada", sem comparação.

### A ordem, quando dois documentos discordarem

```
1. AUDITORIA_*.md            achado com endereço no binário — SEMPRE vence
2. AUDITORIA_SUPERFICIE.md   diz o que ainda NÃO tem evidência
3. RELATORIO_2026-08-12.md   índice e ponto de retomada
4. HANDOFF / STATUS /        contexto, histórico e roadmap.
   CHECKLIST / TESTPLAN      Nunca fonte de parâmetro de mecânica.
```

`HANDOFF.md`, `STATUS.md` e `TESTPLAN.md` receberam banner de precedência no
topo. Se um documento novo for criado, ele nasce no nível 4 até ter endereço.

---

## 7. Mapa dos documentos

| Arquivo | O que é |
|---|---|
| `RELATORIO_2026-08-12.md` | **este** — índice narrativo e ponto de retomada |
| `AUDITORIA_SUPERFICIE.md` | taxa-base 9/9, superfície não verificada, vícios de processo |
| `AUDITORIA_2026-08-12.md` | achados A8-A13, identificação do binário |
| `AUDITORIA_2026-07-30.md` | achados A1-A7 |
| `AUDITORIA_HIGIENE.md` | perfil "AI slop" mapeado contra o repo |
| `ARQUITETURA.md` | avaliação arquitetural; a dívida é de modelagem, não de estrutura |
| `tools/BINARY_NOTES.md` | receitas de leitura do binário + lições das 6 ferramentas |
| `TESTPLAN.md` | roteiro de teste no device, 39 itens |
| `CHECKLIST.md` | roadmap de features |
| `STATUS.md` | histórico por sessão |
| `HANDOFF.md` | contexto de projeto, ambiente, armadilhas |

**Ferramentas:** `struct_harvest.py` (inventário de campos), `fn_boundary_check.py`
(fronteiras de função — 187 dos 481 `FUN_` do decomp não são entrada de função),
`rsrc_extract.py`, `bmp2png.py`, `datdump.cpp`, `ghidra_export_c.py`,
`host_sdl.sh`.
