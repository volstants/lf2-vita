> ## ⚠ Critérios de teste ≠ evidência de fidelidade
>
> Passar num teste daqui prova que o porte faz o que **nós** decidimos que ele
> faria. Não prova fidelidade ao LF2 original — isso só sai de
> `AUDITORIA_*.md`, com endereço. Onde um critério depender de valor Nível D,
> está marcado no próprio item: anote o observado, não compare com o esperado.

# Roteiro adicional — entrega 2026-08-12 (nucleo de reacao a dano)

Cobre `AUDITORIA_2026-08-12.md`. Esta entrega mexe no que o jogo tem de mais
sensivel ao tato: quantos golpes derrubam, o que a defesa aguenta e qual
animacao de dano toca. Espere que a sensacao mude — a questao e' se mudou PARA
o original.

| # | Teste | Esperado |
|---|---|---|
| 29 | Dois socos fracos seguidos (fall 25) | **derruba no segundo**, nao no terceiro **[N]** |
| 30 | Socos espacados (1 s entre eles) | nao derruba: o contador decai 1/tick **[N]** |
| 31 | Golpe medio isolado (fall 45) | Dance of Pain, atordoamento longo **[N]** |
| 32 | Golpe de frente (fall 25) | animacao de dano **frontal** (222) **[N]** |
| 33 | Mesmo golpe **pelas costas** | animacao **de costas** (224) — antes vinha por intensidade **[N]** |
| 34 | Defender um soco fraco | perde ~1/10 do dano; antes era zero **[!]** |
| 35 | Defender golpes ate a guarda ceder | quebra por `bdefend` acumulado > 30, nao por um golpe pesado unico **[N]** |
| 36 | Defender, esperar, defender de novo | a guarda "recupera": bdefend decai 1/tick **[N]** |
| 37 | Acertar alguem no ar | derruba sempre, qualquer que seja o fall **[R]** |
| 38 | Acertar alguem congelado | estilhaça e tomba **[R]** |
| 39 | Partida completa contra 3 inimigos | dificuldade mudou, mas ninguem fica imortal nem preso **[!]** |

**Risco assumido:** o item 34 muda comportamento visivel — defender deixou de
ser gratuito. E o 29 torna combos mais letais. Os dois vem do assembly
(`0x0042ff6a` e `0x0042eb6c`), mas nunca rodaram no device.

**Nao implementado:** hitstop (`shaking`, achado A12). Se os golpes parecerem
"sem peso" comparados ao original, e' esse o motivo, e nao um bug desta entrega.

---

# Roteiro de teste no device — entrega 2026-07-30 (auditoria de assembly)

Cobre os seis achados da `AUDITORIA_2026-07-30.md`. Nada validado em hardware:
só suíte host (222 CHECKs), harness headless (dennis e henry) e compile-check.

Marque `OK` / `ERRO` + uma linha do que viu. **[N]** = comportamento novo ·
**[R]** = regressão que não pode ter voltado · **[!]** = risco assumido nesta
entrega, olhar com atenção redobrada.

```
cd /mnt/c/Users/rodrigo.chiesa/Documents/LittleFighter2Vita/build && cmake .. && make -j$(nproc)
```

---

## 1. Gate de `itr->effect` — A1 (lf2.exe 0x00417400)

| # | Teste | Esperado |
|---|---|---|
| 1 | Firen em corrida em chamas atravessa alvo **já queimando** | Firen **não** toma dano e **não** é interrompido **[N]** |
| 2 | Alvo parado dentro do fogo do chão do Firen | **desaba** ao fim da queimadura; não reacende em ciclo **[N]** — ⚠ a DURAÇÃO não é critério: `BURN_TICKS = 36` é Nível D, o candidato `+0xEA` foi refutado e como o original sai do state 18 segue em aberto (`AUDITORIA_SUPERFICIE.md` item 1). Anotar a duração observada, não comparar com 36 |
| 3 | Alvo queimando levando fogo de novo | HP **não** cai a cada nova chama **[N]** |
| 4 | Firen acerta fogo em quem **não** está queimando | acende normalmente **[R]** |
| 5 | Soco comum do Dennis (effect 0) | acerta normalmente **[R]** |
| 6 | Freeze: gelo em quem já está congelado (frames 200-202) | não reinicia a cadeia **[R]** |

## 2. Fogo no ar e frames 205/206 — A2 (0x0040e893)

| # | Teste | Esperado |
|---|---|---|
| 7 | Incendiar alvo em **pleno salto** | continua em chamas; a pose de pulo **não** apaga o fogo **[N]** |
| 8 | Observar a **descida** do alvo em chamas | troca para a pose horizontal (frames 205/206), sprite deitado **[N]** |
| 9 | Alvo em chamas **aterrissa** | continua queimando no chão até o fim do cronômetro **[N]** |
| 10 | Congelar alvo em pleno salto | idem: o gelo sobrevive ao voo e ao pouso **[N]** |
| 11 | Pulo comum, sem fogo/gelo | pose de pulo normal (210/211/212) **[R]** |

## 3. Knockback em re-acerto — A3 (0x0042ee5b)

| # | Teste | Esperado |
|---|---|---|
| 12 | Segundo projétil num alvo **já caindo** | o alvo é **empurrado na horizontal**, não só re-erguido no lugar **[N]** |
| 13 | Combo de socos em alvo em pé | tranco por faixa de FP, como antes **[R]** |
| 14 | Golpe pesado de primeira (fall 70) | arremesso longo, como antes **[R]** |

## 4. Bola perfurante — A4 (0x0042f0bf / 0x00430536)

| # | Teste | Esperado |
|---|---|---|
| 15 | Henry, tiro concentrado, 2+ inimigos **alinhados** | a flecha **atravessa** e acerta todos **[N]** |
| 16 | Mesmo tiro, alvo único | derruba de primeira, sempre **[N]** |
| 17 | Bola do Dennis / Davis (state 3000) | ainda **some** no primeiro corpo **[R]** |
| 18 | Flecha comum do Henry (arma leve, oid 201) | ainda krava/cai; não atravessa **[R]** |
| 19 | Vento do Henry (state 3005) | agora **atravessa**; conferir que não fica preso na tela **[!]** |
| 20 | Chamas do Firen (state 18) | agora **atravessam**; conferir que não causam dano contínuo absurdo **[!]** |

## 5. `arest` × `vrest` — A5 (0x0042f2c8)

| # | Teste | Esperado |
|---|---|---|
| 21 | Soco simples repetido no mesmo alvo | cadência de re-acerto **mais rápida** que antes (piso caiu de 8 para 4 TU) **[!]** |
| 22 | Golpe de rodopio / many_foot (tem `vrest`) | continua acertando várias vezes, cadência inalterada **[R]** |
| 23 | Flecha perfurante contra o **mesmo** alvo | não re-acerta antes de 15 TU **[N]** |
| 24 | Dois inimigos, golpe com `vrest` | travar um **não** impede acertar o outro **[N]** |

## 6. Regressões gerais **[R]**

| # | Teste | Esperado |
|---|---|---|
| 25 | Partida completa contra 3 inimigos | ninguém fica imortal, preso ou invisível |
| 26 | Pegar, usar e arremessar arma | sem mudança |
| 27 | Especiais de Firen, Henry, Dennis, Louis, Rudolf | disparam e causam dano |
| 28 | Sessão longa (5+ min) | sem engasgo, sem pool de objetos entupindo |

---

## Riscos assumidos nesta entrega (não são bugs a reportar, são o que vigiar)

**Piso do `arest` caiu de 8 para 4 TU** (itens 21). O 8 era invenção; o 4 vem do
assembly (`0x0042f2e4`). Efeito colateral: todo golpe **sem** `vrest` re-acerta
cerca de duas vezes mais rápido. É a mudança com maior impacto na sensação de
jogo desta entrega. Se os combos ficarem fáceis demais, é aqui.

**Objetos em state 18 / 3005 / 3006 deixaram de se gastar** (itens 19, 20). O
binário só manda ao frame `hiting` quem está em 3000. Chamas e vento agora
persistem — comportamento fiel, mas é a primeira vez que roda no device.

**Teto de `arest` em 12 NÃO foi aplicado** (achado A7, "não foi possível
comprovar"). 143 itrs usam `arest`, 51 deles com valor 15. Se o espaçamento de
algum golpe multi-hit parecer longo demais, é o candidato número um.

---

# Roteiro de teste no device — acumulado desde o último reteste

Cobre **tudo** que mudou desde o seu último retorno (o teste do modo auditoria que
reportou "especiais sem dano / imortal / dentro da pedra"). Nada aqui foi validado
em hardware: só suíte host (6 suítes) + harness headless + compile-check.

Marque `OK` / `ERRO` + uma linha do que viu. Itens **[N]** são comportamento novo;
**[R]** são regressões que não podem ter voltado.

```
cd /mnt/c/Users/rodrigo.chiesa/Documents/LittleFighter2Vita/build && cmake .. && make -j$(nproc)
```

---

## A. Efeitos de golpe — `itr.effect` **[N]**
Eram 450 itrs com efeito, ignorados por completo. Fogo e gelo chegavam como dano genérico.

| # | Teste | Esperado |
|---|---|---|
| 1 | Firen: acertar com fogo | alvo **pega fogo** (animação de chamas), não só perde HP |
| 2 | Deixar o alvo queimando | ele **desaba** ao fim (~1,2 s), não queima para sempre |
| 3 | Alvo com arma na mão leva fogo | **derruba a arma** |
| 4 | Freeze: acertar com gelo | alvo **congela** ~3 s, imóvel, e depois tomba |
| 5 | Alvo com arma leva gelo | **derruba a arma** |
| 6 | Acertar fogo em quem já queima | **não** reinicia a animação |
| 7 | Soco comum do Dennis | **sem** fogo/gelo |

## B. Reação a hit em 4 faixas **[N]**
Antes era sempre o mesmo tranco curto (frame 220).

| # | Teste | Esperado |
|---|---|---|
| 8 | 1 soco (fall 25) | tranco curto |
| 9 | 2 socos seguidos | tranco maior, ainda em pé |
| 10 | 3 socos seguidos | **tombo** |
| 11 | 1 flecha do Henry (fall 60) | **Dance of Pain**: atordoamento longo (~1 s parado) |
| 12 | Flecha concentrada / chute giratório (fall 70) | tombo **de primeira** |

## C. Alvo único **[N]**
Regra do original: golpe com `vrest 0` acerta **só o mais próximo**.

| # | Teste | Esperado |
|---|---|---|
| 13 | 2+ inimigos empilhados na mesma linha, um soco | acerta **só um** (o mais próximo) |
| 14 | Golpe de rodopio (tem `vrest`) na mesma pilha | acerta **vários** |

## D. Tipos de `itr` que passaram a causar dano **[N]**

| # | Teste | Esperado |
|---|---|---|
| 15 | Henry: flauta | causa dano — **inclusive em quem está caindo** |
| 16 | Freeze: coluna de gelo | causa dano |

## E. Estados hardcoded por personagem **[N]**
Estados ≥100 eram atropelados pela pose de pulo (era o seu bug do Louis).

| # | Teste | Esperado |
|---|---|---|
| 17 | **Louis: correr + pular + atacar** | investida longa (~400 px) que **completa**; não trava no meio |
| 18 | Woody: teleporte (`hit_Uj`/`hit_Dj`) | animação roda até o fim (o deslocamento em si ainda não é emulado) |
| 19 | Rudolf: transformação | frames rodam sem travar |
| 20 | Deep (se habilitar no roster) | `dash_sword` completa |

## F. Códigos `next` especiais **[N]**

| # | Teste | Esperado |
|---|---|---|
| 21 | Rudolf `hit_Uj` (custa 350 MP) | **desaparece ~5 s, invulnerável**; sombra pisca antes de voltar |
| 22 | Bater nele enquanto invisível | **nenhum dano** |
| 23 | Louis c-throw | ele **inverte o lado** ao fim do golpe |

## G. Física de arma **[N]**
`WEAPON_FLY_GRAVITY` = **0.425 = 1.7/4**, lido do pool de constantes do `lf2.exe`.

| # | Teste | Esperado |
|---|---|---|
| 24 | Flecha do Henry | ~420 px antes de cravar (era 396 com o valor antigo) |
| 25 | Pedra arremessada | arco mais baixo, sem subir demais |

## H. Arma no chão **[N]**
Regra do original: `on_ground_state_1` (arma leve) é imune a mão vazia.

| # | Teste | Esperado |
|---|---|---|
| 26 | Socar a **faca** no chão de mão vazia | **não** quebra |
| 27 | Socar a **pedra/caixa** de mão vazia | quebra (leva vários golpes, 800 HP) |
| 28 | Bater na faca **com arma na mão** | quebra |
| 29 | Ao quebrar | aparece o estilhaço animado, e ele **some** |

## I. Cenário e colisão **[R]**

| # | Teste | Esperado |
|---|---|---|
| 30 | Andar até as duas pontas da fase | céu e montanhas cobrem a tela toda |
| 31 | Parallax | fundo lento, chão 1:1 |
| 32 | Fogo do Firen no chão | apaga em ~3 s, não fica para sempre |
| 33 | **Correndo** contra a pedra | **não atravessa** (colisão varrida em substeps) |
| 34 | Andando contra a pedra | bloqueia |
| 35 | Pular sobre a pedra | passa por cima |
| 36 | Ser jogado para dentro da pedra | consegue **sair andando** (não fica preso) |

## J. Modo auditoria — SELECT **[N]**

| # | Teste | Esperado |
|---|---|---|
| 37 | Apertar SELECT em partida | faixa amarela no topo do HUD |
| 38 | Alvos | posicionados a ~110/260/430 px, virados para você, parados |
| 39 | Cada ~1,7 s | sai o próximo golpe do ciclo `Fa→Ua→Da→Fj→Uj→Dj` |
| 40 | Barras de vida | **mexem** (o dano é visível; não há mais imortalidade) |
| 41 | Ao morrer | revive com HP cheio, a sessão não acaba |
| 42 | Alvos | ficam **em pé** entre golpes (não acumulam knockdown) |
| 43 | Percorrer os 8 do roster | anotar quais golpes não saem ou não causam dano |

## K. Regressões gerais **[R]**

| # | Teste | Esperado |
|---|---|---|
| 44 | Henry: 25+ flechas seguidas | continuam saindo (pool não enche) |
| 45 | Flechas/shurikens no chão | somem sozinhas em ~3 s |
| 46 | Faca e pedra do cenário | **não** somem |
| 47 | Fumaça (Rudolf/Louis/Henry) | some sozinha; **não** pode ser pega |
| 48 | Ninguém é empurrado ao pegar/arremessar | — |
| 49 | Ataque sem direcional com pedra na mão | arremessa na direção que olha |
| 50 | Faca sem direcional | golpeia; com direcional, arremessa |
| 51 | Carregando pedra | corre mais devagar; **não** pula |
| 52 | Correndo + Ataque perto da pedra | sai **Ataque Correndo**, não pega o item |
| 53 | Nenhum personagem desliza durante ataques | — |
| 54 | Nenhum sprite congelado na tela ao fim da partida | — |
| 55 | Primeiro especial de cada personagem | sem engasgo (assets pré-carregados) |

---

## Pontos que eu já sei que estão incompletos (não reportar como bug)

- Woody/Rudolf: os frames de teleporte e transformação **rodam**, mas o efeito
  (deslocar, trocar de personagem) não é emulado — depende de trocar o `.dat` em runtime.
- Agarrão (cpoint) não existe: `itr kind 1/3` não agarram.
- `itr kind 4` (thrown) inerte — depende do agarrão.
- Sem áudio.
- Só a fase Lion Forest; `stage.dat` (waves/bosses) não existe.
- Quebra de guarda usa `fall >= 60` em vez de acumular `bdefend`.
- Sem hitstop.
- Saída da investida do Louis (state 100) é **inferência** — nenhuma das três
  fontes emula esse estado. Se o alcance parecer errado, é o candidato a ajustar.
