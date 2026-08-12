> Avaliação de estrutura e modelagem. **Não é fonte de parâmetro de mecânica** —
> para isso, `AUDITORIA_*.md` (com endereço) e `AUDITORIA_SUPERFICIE.md` (o que
> ainda não tem). Ponto de entrada: `RELATORIO_2026-08-12.md`.

# Avaliação arquitetural · 2026-08-12

Pergunta: o porte está sendo feito da maneira certa, arquiteturalmente?

Resposta curta: **a aposta central está certa e já se pagou. A dívida é outra, e
tem nome.**

---

## 1. O que está certo, e a evidência disso

A decisão fundadora foi: **interpretar os `.dat` em runtime, zero lógica de
personagem hardcoded**. Um `Fighter` genérico anda o grafo de frames de qualquer
arquivo; a diferença entre Dennis e Firen está 100% nos dados.

Isso não é opinião — foi testado por contradição. Nesta sessão, corrigir a
reação a dano (quatro divergências: `fall`, `bdefend`, frames 222/224,
decaimento) exigiu mexer em **um** lugar, `Player::hit()`. Numa arquitetura com
lógica por personagem, seriam 26 lugares, e a chance de corrigir 25 e esquecer um
seria alta — que é exatamente o bug que o histórico registra em `zwidth` e
anti-juggle quando havia quatro caminhos de ataque copiados.

Também está certo:

- **Pool fixo de objetos**, sem alocação no laço de jogo. Correto para Vita.
- **Testes de host sem SDL** — `dat.hpp`/`fighter.hpp`/`player.hpp` são puros, e
  por isso 238 CHECKs rodam em segundos. É o que torna a auditoria de fidelidade
  viável; sem isso, cada verificação custaria um ciclo de device.
- **Harness headless** rodando o `main.cpp` real. Pega classe de bug que teste
  unitário não pega (pool entupindo, objeto que não aposenta).
- **Adiar o GXM** (seção 7 do checklist). 2D sobra via SDL2; trocar backend antes
  do conteúdo estabilizar seria otimização prematura.

---

## 2. A dívida real: o modelo de objeto **reinterpreta** o original em vez de espelhá-lo

Esta é a única crítica arquitetural séria, e é a raiz de quase todos os erros de
fidelidade encontrados.

O LF2 tem **um** `object_t` plano, com offsets fixos, compartilhado por
personagens, armas e projéteis. O porte tem `Player` (com `x`/`z`/`h`/`vy`
próprios), `Fighter` (com `x`/`y`/`z`/`vx`/`vy`) e `Object` — três modelos, e
nenhum deles é o do original.

As divergências concretas, todas descobertas *depois* de causarem erro:

| Original | Porte | Consequência |
|---|---|---|
| `y` único, negativo = no ar | `z` (profundidade) + `h` (altura) separados | o teste `cmpl $0x0,0x14` do binário não tem tradução direta |
| `fall` (`+0xB0`), inteiro | `fp` + `fpAcc` em centésimos | modelo inteiro de decaimento errado por 3 sessões |
| `bdefend` (`+0xB8`) | não existia | quebra de guarda inventada a partir de `fall >= 60` |
| `shaking` (`+0xB4`) | não existe | hitstop ausente até hoje |
| `frame_id1`/`3`/`4` | `frameId` único | ainda não sabemos quando o original os deixa divergir |
| `vrest_of_objects[400]` na vítima | `victimRest[]` no atacante | equivalente hoje, divergente se surgir 3º ator |
| — | `knockedDown` | estado paralelo sem contrapartida; causou bug real |
| tabela de 3000 bytes + 2 cursores (`0x44FF90`) | **não existe** | 264 pontos de decisão aleatória do engine sem contrapartida (A13) |

O padrão é sempre o mesmo: **inventamos uma representação, depois descobrimos a
do original, depois convertemos.** Cada conversão é uma chance de errar, e a
taxa-base registrada em `AUDITORIA_SUPERFICIE.md` é de nove erros em nove
verificações.

**A última linha da tabela é a mais barata de acertar, porque ainda não erramos
nela.** O porte não tem fonte de aleatoriedade nenhuma — não há representação
inventada para converter depois. Se `engine_random` entrar com a forma do
original (tabela + dois cursores, `AUDITORIA_2026-08-12.md#a13`) **antes** de
qualquer comportamento ramificado, este é o primeiro campo que nasce espelhado
em vez de convertido. É a convergência incremental funcionando de forma
preventiva, e não corretiva.

Se o modelo tivesse espelhado `object_t` desde o início, as perguntas teriam sido
"o que escreve este campo?" — que é respondível com `grep` no disassembly — em
vez de "qual deveria ser este valor?", que convida a chutar.

**Não recomendo reescrever.** Recomendo **convergir incrementalmente**, que já
começou sozinho: `fall` e `bdefend` entraram nesta sessão com o nome e a
semântica do original, não com nome inventado. Continuar assim.

---

## 3. Falta uma fronteira entre "estado que precisa bater com o LF2" e "bookkeeping do porte"

`knockedDown` é o caso exemplar: não existe no original (o LF2 lê tudo do state
do frame corrente), é invenção nossa, e produziu um bug em que a vítima travava
em pé dentro do frame 180.

Hoje não há nada no código que distinga os dois tipos de estado. Um leitor novo
— ou eu, três sessões adiante — não consegue dizer se `knockedDown` é fiel ou
conveniência.

**Sugestão barata:** convenção de nome ou de agrupamento. Campos que espelham o
original com o nome do original (`fall`, `bdefend`, `shaking`); campos de porte
num bloco marcado, tipo `// ── estado do PORTE, sem contrapartida no engine ──`.
Custo quase zero, e torna visível a categoria de erro que já custou uma sessão.

---

## 4. Coesão: `Player` faz coisa demais, e `Enemy` tem-um-`Player`

`player.hpp` tem ~800 linhas e acumula controlador de input, física, combate,
máquina de animação e reação a dano. `Enemy` **contém** um `Player`, o que é
semanticamente torto — um inimigo não "tem um jogador".

Isso não está causando bug, então é dívida de manutenibilidade, não de
fidelidade. O nome certo para o que `Player` é hoje seria `Actor`, com `Player` e
`Enemy` sendo dois provedores de input para ele. É a refatoração que o
`AUDITORIA_HIGIENE` item 3 já sugere junto com a unificação dos quatro caminhos
de ataque.

**Recomendo fazer as duas juntas, e só depois do teste no device desta entrega.**

---

## 5. O que eu mudaria na ordem de prioridade

O checklist ainda trata o eixo como "conteúdo → áudio → GXM". Depois desta
sessão, discordo. A ordem que os dados sustentam é:

1. **Fidelidade contra o assembly** (em curso) — porque a taxa-base diz que o que
   está implementado provavelmente está errado, e conteúdo novo construído sobre
   base errada multiplica o erro.
2. **Convergir o modelo de objeto** — reduz a chance do próximo erro.
3. **Teste no device** de cada entrega de fidelidade.
4. Só então conteúdo restante (`cpoint`, `stage.dat`, roster completo).
5. Áudio.
6. GXM, se algum dia.

Trocar a ordem 1↔4 seria o erro clássico de porte: acumular features sobre uma
base cuja fidelidade ninguém verificou.

---

## Veredito

A arquitetura está **certa no essencial** e a evidência disso é concreta: a
correção de fidelidade mais profunda até agora coube num arquivo.

A dívida não é de estrutura — é de **modelagem de dados**, e é corrigível de
forma incremental sem reescrever nada. O maior risco do projeto hoje não é
arquitetural: é a superfície de parâmetros ainda sem evidência do binário,
documentada em `AUDITORIA_SUPERFICIE.md`.
