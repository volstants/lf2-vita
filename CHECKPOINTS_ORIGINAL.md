# Checkpoints de observação no LF2 v2.0a original

> **Nível 4 de precedência enquanto estiver vazio.** Este arquivo é um
> **protocolo**, não uma fonte. Ele não contém parâmetro de mecânica e não deve
> ser citado como evidência. Vira evidência só depois de executado, e o
> resultado vai para uma `AUDITORIA_*.md` com data, não para cá.

Binário alvo: `reference/decomp/lf2.exe`, LF2 v2.0a, SHA256
`12dfa00f…eab8fc5` — o mesmo que foi desassemblado. Rodar outro build invalida
tudo.

---

## 0. Por que este documento existe

Os achados A14-A18 foram reconstruídos **inteiramente do disassembly estático**.
Nenhum deles foi visto acontecer. A cadeia inteira — acumulador → contador de
golpes → commit dividido → hitstop que adia o commit — é uma leitura de fluxo de
controle, e leitura de fluxo de controle é exatamente o que o projeto ainda não
tem como conferir.

O precedente que obriga isto: o candidato `+0xEA` era uma leitura plausível,
sustentada por vizinhança, e estava errado. A diferença é que A14-A18 têm
endereço em cada elo — mas ter endereço prova que **a instrução existe**, não que
**o efeito é o que eu digo que é**.

Cada checkpoint abaixo é um **teste de refutação**: uma previsão numérica que só
sobrevive se a reconstrução estiver certa, e que falha de um jeito específico se
estiver errada.

**Não teste o porte com isto.** O porte não implementa A14-A18; medi-lo hoje
mede o modelo velho. O alvo aqui é o `lf2.exe`.

---

## 1. O instrumento

### 1.1 O que se usa

| Instrumento | Para quê | Custo |
|---|---|---|
| **Gravação de tela a 60 fps** + player com avanço quadro a quadro | contar ticks | zero, decide tudo |
| **`.dat` customizado** | controlar `itr->fall`, `dvx`, `dvy` | médio, e é o que dá rigor |
| **`tools/datdump`** | ler `jump_height` do alvo para calibrar | já existe |
| **Cronômetro** | só os checkpoints de MP | zero |

O jogo roda a **30 Hz** (a confirmar em CP0). A 60 fps de gravação, **1 tick do
jogo = 2 quadros de vídeo**. Todas as previsões abaixo estão em ticks; dobre para
quadros de vídeo.

### 1.2 O que NÃO está disponível, e o que se sabe do binário

- **Não confirmei tecla de pausa nem de avanço quadro a quadro.** Procurei e não
  fechei; não vou afirmar que `F1`/`F2` fazem isso. O protocolo foi desenhado
  para **não depender** disso — a gravação de tela substitui.
- **`F6`, `F7`, `F8` e `F9` são teclas de trapaça e são contadas.** Prova: a
  string em `0x004491a8` é
  `"Function Keys Used:    F6: %d time(s)    F7: %d time(s)    F8: %d time(s)    F9: %d time(s)"`,
  e existe o estado `"Function Keys Locked"` em `0x00449190`.
  **Não use nenhuma delas durante uma medição** — elas mudam estado e o próprio
  jogo registra que foram usadas.
- **O replay `.lfr` é uma re-simulação determinística** — isto está provado
  (A13): o arquivo guarda tabela de aleatoriedade + cursores + entradas, e o
  engine roda tudo de novo. Consequência prática grande: **grave a luta uma vez e
  reveja quantas vezes precisar**, medindo uma coisa diferente a cada passada,
  com a garantia de que é literalmente a mesma partida. A tecla que inicia a
  gravação não foi verificada no binário — confirme no jogo antes de confiar.
- **Use modo VS com dois jogadores humanos**, um deles parado. IA introduz
  decisões aleatórias (264 sítios, A13) e destrói a repetibilidade.

### 1.3 O `.dat` de teste — construir uma vez, serve para CP6, CP7 e CP9

Copie um personagem para `data/atest.dat`, registre em `data/data.txt` com um
`id` livre, e edite **um** frame de ataque para ter um único `itr`:

```
itr:
kind: 0  x: <cobrindo o alvo>  y: <…>  w: <…>  h: <…>
dvx: 0  dvy: 0  fall: 70  vrest: 200  bdefend: 0  injury: 1
itr_end:
```

Por que cada campo:

- `fall: 70` — passa do limiar de 60 (A8, `0x0042eb20`), então **derruba no
  primeiro golpe**, sem depender de acumular.
- `dvy: 0` — é isto que aciona o ramo do padrão `-7.0` (A14, `0x0042f215`). Se
  você escrever `dvy: -7`, o resultado é o mesmo por outro caminho e o teste
  perde o poder de discriminar.
- `dvx: 0` — zera o horizontal, deixando a trajetória vertical limpa.
- `injury: 1` — a vítima não morre e você repete à vontade.
- `vrest: 200` — impede reacerto acidental sujando a medição.

Variante para CP9 (controle negativo): o mesmo frame com `fall: 20`.
Variante para CP7: um `opoint` que solta **dois** projéteis idênticos nas mesmas
coordenadas e com o mesmo `dvx` — eles viajam colados e acertam no mesmo tick.

---

## 2. CP0 — Calibração · **porteiro: nada abaixo vale sem isto**

**Pergunta.** A minha régua está certa?

**Monta.** Gravação a 60 fps, modo VS, dois humanos, cenário sem rolagem de
câmera (fique longe das bordas).

**Faz.**
1. Rode `tools/datdump` no `.dat` do personagem que vai apanhar e **anote
   `jump_height`**. Todo o CP6 se calibra por ele.
2. Grave um **salto parado** do alvo, do primeiro quadro fora do chão até o
   primeiro quadro de volta no chão. Conte os quadros de vídeo.

**Previsão.** Com `jump_height = -16.3` (Davis) e gravidade `1.7`
(`0x00448348`), o salto dura **20 ticks = 40 quadros de vídeo**, com ápice de
**86,5 px** no tick 10.

**Refuta se.** A contagem der longe de 40 quadros. Aí ou a gravação não está a 60
fps, ou o jogo não está a 30 Hz, ou `jump_height` é outro — e nada abaixo pode
ser interpretado.

**Se refutar, cai:** nada da auditoria; cai a régua. Conserte a régua primeiro.

---

## 3. Bloco MP — o mais barato e o de maior erro medido (A18)

### CP1 — A barra de MP começa cheia

**Pergunta.** `mp` inicial é 500 ou 200?

**Faz.** Comece uma partida. Olhe a barra de MP antes de qualquer ação.

**Previsão A18.** **Cheia.** `0x004063ca`-`0x004063e1` carrega `0x1f4` = 500 em
`hp`, `hp` máximo, teto recuperável e `mp` na mesma tacada.

**Refuta se.** A barra começar em ~40%. Aí `mp_start = 200` do F.LF estava certo
e eu li o campo errado.

**Se refutar, cai:** A18 inteiro, e com ele a identificação de `+0x308` como
`mp`. Custo do teste: 30 segundos. **Faça este primeiro.**

### CP2 — A regeneração acelera conforme o HP cai

**Pergunta.** A taxa é fixa ou função do dano?

**Faz.** Com um personagem qualquer que não seja Firzen nem Julian:
1. Gaste todo o MP (especiais até zerar). Cronometre o tempo até a barra encher
   de novo, **com HP cheio**.
2. Leve o mesmo personagem a HP baixo (~10%), zere o MP de novo, cronometre.

**Previsão A18.** `mp += (500 − min(hp,500))/100 + 1` por tick
(`0x0041fa90`-`0x0041faf2`), a 30 Hz:

| HP | ganho/tick | 0 → 500 MP |
|---|---|---|
| 500 (cheio) | +1 | ≈ **16,7 s** |
| 250 | +3 | ≈ **5,6 s** |
| 50 | +5 | ≈ **3,3 s** |
| 0 | +6 | ≈ **2,8 s** |

**Refuta se.** Os dois tempos forem iguais. Aí a regeneração não depende do HP e
a conta que eu li faz outra coisa.

**Se refutar, cai:** metade de A18. `mp = 500` (CP1) sobrevive independente.

### CP3 — Firzen e Julian regeneram mais rápido no mesmo HP

**Pergunta.** O ramo de `id` 51/52 existe mesmo?

**Faz.** Repita CP2 com **Firzen** e com **Julian**, os dois com HP cheio, e
compare com o tempo de HP cheio de CP2.

**Previsão A18.** `0x0041fac9`-`0x0041fad6` divide o `hp` por 2 antes da conta
para `file->id` 51 e 52. `data/data.txt` diz: **51 = Firzen, 52 = Julian.**
Então, com HP cheio, eles ganham `(500 − 250)/100 + 1 = +3`/tick contra `+1`/tick
dos outros — **≈ 5,6 s contra ≈ 16,7 s, três vezes mais rápido.**

**Refuta se.** Os três tempos forem iguais.

**Nota de método.** A comunidade "sabe" que Firzen e Julian regeneram MP mais
rápido. Isso é Nível D e **não é corroboração** — é justamente o tipo de
concordância que já enganou este projeto nove vezes. O que confirma é o
cronômetro.

---

## 4. Bloco hitstop — o elo que muda o timing de tudo (A17)

### CP4 — Os dois congelam, e o atacante descongela antes

**Pergunta.** `shaking` congela o objeto inteiro, e o sinal só muda o desenho?

**Faz.** Grave um golpe simples que conecte. No player, ache o quadro em que o
golpe conecta e conte, separadamente:
- quantos quadros o **atacante** fica com a animação parada;
- quantos quadros a **vítima** fica com a animação parada;
- se a vítima **treme lateralmente** durante a parada, e se o atacante treme.

**Previsão A17.**
- Vítima: `shaking = -5` (`0x004300b7`) → **5 ticks = 10 quadros** congelada.
- Atacante: `shaking = +3` (`0x004300a6`) → **3 ticks = 6 quadros** congelado.
- **O atacante volta a se mover ~2 ticks (4 quadros) antes da vítima.**
- Só a vítima treme: o deslocamento lateral de `6n − 3` em
  `0x0040de45`-`0x0040de50` só roda quando `shaking < 0`.
- Enquanto congelado, o objeto **não anda, não cai, não avança frame e não decai
  `fall`** — `FUN_0040e490` retorna antes de tudo (`0x0040e494`-`0x0040e4c2`).

**Refuta se.** (a) ninguém congela; (b) só a vítima congela; (c) os dois
congelam pelo mesmo tempo; (d) o atacante também treme.

**Se refutar, cai:** A17, e junto o adiamento do commit em CP5. A12 volta a ser
"não foi possível comprovar".

**Cuidado.** Há um segundo par de valores no binário — `+3`/`-3` em
`0x0042f2b7`/`0x0042f2cc` e `+2`/`-3` em `0x00418986`/`0x00418997`. Se a
contagem der 3/3 em vez de 5/3, **não é refutação**: é outro sítio de acerto. Só
conta como refutação se a contagem for 0, ou se atacante e vítima empatarem em
todos os golpes que você testar.

### CP5 — O empurrão sai DEPOIS do congelamento, não no acerto

**Pergunta.** O empurrão é aplicado no tick do acerto ou no commit?

**Faz.** Na mesma gravação de CP4, marque o quadro do acerto e o **primeiro
quadro em que a vítima muda de posição**.

**Previsão A16 + A17.** A vítima fica **exatamente parada** durante os ~5 ticks
(10 quadros) de congelamento e só então sai. `FUN_004196f0` pula o slot enquanto
`shaking != 0` (`0x0041970c`), e **não zera o acumulador** — o empurrão fica
guardado.

**Refuta se.** A vítima começar a deslizar no mesmo tick do acerto, ou dentro de
1-2 quadros dele.

**Se refutar, cai:** o adiamento. O acumulador (A16) ainda pode existir; o que
cai é a cancela de `shaking` sobre ele.

---

## 5. Bloco empurrão — o achado central (A14, A16)

### CP6 — Altura e tempo de voo de um tombo sem `dvy`

**Pergunta.** O impulso padrão é `-7.0`, `-8.0` ou `-6.0`?

**Monta.** O `.dat` de teste do §1.3 (`fall: 70`, `dvx: 0`, `dvy: 0`). Alvo
parado no chão, longe das bordas.

**Faz.** Grave o acerto. Conte, **a partir do primeiro quadro em que a vítima
sai do chão** (não do quadro do acerto — CP5 explica os 10 quadros de atraso):
- quantos quadros ela fica no ar;
- a altura máxima, em pixels, comparada com o ápice do salto medido em CP0.

**Previsão A14 + A16.** Um golpe só ⇒ `attacks = 1` ⇒
`vy = (-7,0 × 2,0) / 2 = -7,0` exatamente.

| Modelo | v inicial | ápice | ticks no ar | quadros | % do ápice do salto |
|---|---|---|---|---|---|
| **A14** | **-7,0** | **18,0 px** | **10** | **20** | **20,8 %** |
| porte hoje, `launch(-8.f)` | -8,0 | 23,0 px | 11 | 22 | 26,6 % |
| porte hoje, `vy = -6.f` | -6,0 | 13,8 px | 9 | 18 | 16,0 % |

A coluna de porcentagem é a medida robusta: ela se calibra sozinha contra o salto
do próprio personagem e não depende de escala de tela nem de `jump_height`
específico.

Trajetória prevista para `-7,0`, tick a tick (`y += vy`; se `y >= 0` pousa; senão
`vy += 1,7`) — ordem confirmada em `0x0040e6c2`-`0x0040e6c8` e `0x0040e788`:

```
tick  1   -7,00      tick  6  -16,50
tick  2  -12,30      tick  7  -13,30
tick  3  -15,90      tick  8   -8,40
tick  4  -17,80      tick  9   -1,80
tick  5  -18,00 ←    tick 10   pousa
```

**Refuta se.** 22 quadros / ~27 % (então é `-8,0` e o porte estava certo por
acaso), ou 18 quadros / ~16 % (então é `-6,0`), ou qualquer coisa fora de
20 ± 1 quadro.

**Se refutar, cai:** o valor `-7,0` de A14. **A16 não cai** — o mecanismo de
acumulador e commit é independente do número, e CP5 e CP7 continuam valendo.
Separar as duas coisas é o ponto deste checkpoint.

### CP7 — Dois golpes no mesmo tick não somam: dividem · **o teste decisivo de A16**

**Pergunta.** Existe divisão por `golpes + 1` ou não?

**Monta.** O `.dat` de teste com um `opoint` que solta **dois** projéteis
idênticos nas mesmas coordenadas, com o mesmo `dvx`, cada um com o `itr` do
§1.3. Eles chegam juntos e acertam no mesmo tick. Confirme na gravação que os
dois acertos caem no **mesmo quadro** — se caírem em quadros diferentes, a
tentativa não vale, repita.

**Faz.** Meça o tempo de voo, exatamente como em CP6.

**Previsão A16.** Cada `itr` soma `-7,0` no acumulador ⇒ `-14,0`, e
`attacks = 2` ⇒ `vy = (-14,0 × 2,0) / 3 = -9,33`.

| Modelo | v inicial | ápice | ticks no ar | quadros |
|---|---|---|---|---|
| **A16** (`×2 / (golpes+1)`) | **-9,33** | **30,5 px** | **12** | **24** |
| soma crua, sem divisão | -14,0 | 64,8 px | 18 | 36 |
| divisão por `golpes` em vez de `golpes+1` | -10,5 | 37,8 px | 14 | 28 |

**A separação é enorme: 24, 36 ou 28 quadros.** Não há como confundir, e não
depende de precisão de pixel.

**Refuta se.** Der 36 quadros — aí não há divisão nenhuma, e A16 está errado no
essencial. Der 28 — a divisão existe mas o divisor é outro, e é só corrigir a
fórmula.

**Se refutar com 36, cai:** A16 inteiro, o anti-juggle de tick, e o item 1 do
plano de implementação da próxima sessão. É o checkpoint de maior consequência
do documento.

**Alternativa sem `.dat` customizado**, se você não quiser montar o arquivo: dois
jogadores humanos batendo no mesmo alvo, gravando muitas tentativas e
aproveitando só aquelas em que os dois acertos caem no mesmo quadro. Funciona,
mas o `dvy` dos ataques reais não é zero e a previsão numérica muda — nesse caso
o que se compara é **um golpe contra dois golpes simultâneos do mesmo ataque**, e
a previsão qualitativa é: **o dobro de golpes dá bem menos que o dobro de
lançamento** (fator 4/3, não 2).

---

## 6. Bloco frames e controle negativo

### CP8 — Frame de queda 180 vs 186 pela direção (A15)

**Faz.** Derrube o alvo duas vezes com o mesmo golpe: uma **de frente** e uma
**pelas costas** (alvo virado para o mesmo lado que o atacante).

**Previsão A15.** Sprites de queda diferentes nos dois casos: cair para trás usa
o frame 180, cair para a frente usa o 186 (`0x0042f248` / `0x0042f251`). O eixo é
direção, não intensidade — mesmo padrão que A9 provou para 222/224.

**Refuta se.** O mesmo sprite nos dois casos.

### CP9 — Controle negativo: golpe leve não levanta ninguém (A14)

**Faz.** O `.dat` de teste com `fall: 20`.

**Previsão A14.** **Zero** movimento vertical. Todo o bloco de impulso está sob
`fall == 80` (`0x0042f18a`); sem tombo, não existe impulso vertical nenhum.

**Refuta se.** Houver qualquer pulinho.

**Por que este checkpoint existe.** Os outros oito procuram confirmação. Este
procura o contrário: se um golpe leve levantar a vítima, existe um caminho de
impulso vertical que eu não achei, e o inventário de escritas em `+0x30` que
sustenta A16 está incompleto. É o checkpoint mais barato e o que mais me
preocupa.

---

## 7. Ordem de execução

| # | Checkpoint | Custo | Bloqueia |
|---|---|---|---|
| 1 | CP1 — MP cheio | 30 s | — |
| 2 | CP0 — calibração | 15 min | tudo que conta quadro |
| 3 | CP9 — controle negativo | 10 min | A16 se falhar |
| 4 | CP2, CP3 — regen de MP | 20 min | — |
| 5 | CP4, CP5 — hitstop | 30 min | CP6 e CP7 (definem de onde contar) |
| 6 | CP6 — tombo simples | 30 min | — |
| 7 | CP7 — dois golpes | 45 min | **decide A16** |
| 8 | CP8 — 180 vs 186 | 10 min | — |

CP1 e CP9 primeiro de propósito: são os mais baratos e os que mais rápido
derrubam alguma coisa. Uma sessão que refuta A16 em 45 minutos vale mais que uma
que confirma A18 em três horas.

---

## 8. Registro

Preencha aqui e depois mova o resultado para uma `AUDITORIA_*.md` com data. **O
que fica registrado é o número medido, não o veredito** — "20 quadros" e não
"confirmou A14".

| CP | Data | Medido | Previsto | Bate? | Observação |
|---|---|---|---|---|---|
| CP0 | | quadros de salto: | 40 | | `jump_height` = |
| CP1 | | barra inicial: | cheia | | |
| CP2 | | s @HP cheio / s @HP baixo: | 16,7 / 3,3 | | |
| CP3 | | s Firzen / s Julian / s outro: | 5,6 / 5,6 / 16,7 | | |
| CP4 | | quadros parado atacante / vítima: | 6 / 10 | | tremor só na vítima? |
| CP5 | | quadros entre acerto e 1º movimento: | 10 | | |
| CP6 | | quadros no ar / % do salto: | 20 / 20,8 % | | |
| CP7 | | quadros no ar: | 24 | | acertos no mesmo quadro? |
| CP8 | | sprites diferentes? | sim | | |
| CP9 | | movimento vertical? | nenhum | | |

---

## 9. O que este protocolo NÃO consegue medir

Registrado para ninguém achar que a lista acima cobre A14-A19:

- **A trava de chão em `12,0`** (`0x0042f1f6`). Precisaria de um `itr` com `dvy`
  positivo grande e da vítima no ar em altura controlada. Não desenhei o teste.
- **O fator `2,0`** (`0x004479e0`) separado do divisor `golpes + 1`. CP6 e CP7
  medem o produto dos dois; se ambos derem errado de forma compensatória, o
  protocolo não distingue. Só um caso com 3 golpes simultâneos separaria.
- **`+0x340`, o divisor de dano** (`0x0042e8c6`). Campo novo, semântica não
  revertida, provavelmente ligado à dificuldade — mas isso é palpite e não entra
  em teste antes de virar leitura de assembly.
- **A ordem exata entre `FUN_0040e490` e `FUN_004196f0` dentro do frame.** É por
  isso que CP5 admite ±1 tick.
- **Qualquer coisa sobre o porte.** Nenhum checkpoint aqui mede o porte, por
  construção.
