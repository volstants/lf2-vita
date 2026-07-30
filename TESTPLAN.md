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
