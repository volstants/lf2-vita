# Lendo o `lf2.exe` direto — receitas validadas (2026-07-29)

> Registro para instâncias novas. As três fontes têm **pontos cegos diferentes**;
> a técnica é combiná-las, não escolher uma. Nada aqui precisa de Ghidra.

## Por que isso existe

A saída C do Ghidra **nunca mostra literal de ponto flutuante** — ela referencia o
endereço onde a constante mora. Foi por isso que procurar `1.7` e `0.45` no
`lf2_decomp.c` não devolveu nada, e a gravidade ficou meses documentada como
"community-documented" em vez de "binário".

## Receita 1 — achar uma constante numérica no binário

```bash
python3 - <<'PY'
import struct
d = open("reference/decomp/lf2.exe","rb").read()
for v in [1.7, 0.425, 0.5]:
    dbl, flt = struct.pack("<d", v), struct.pack("<f", v)
    print(v, "double:", d.count(dbl), "float:", d.count(flt),
          hex(d.find(dbl)) if d.count(dbl) else "")
PY
```

Resultados já confirmados:

| valor | onde | significado |
|---|---|---|
| `1.7` | `0x48348` (double, **única** ocorrência) | gravidade |
| `1.133333` | `0x48368` | 1.7 × 2/3 |
| `0.566667` | `0x48350` | 1.7 / 3 |
| **`0.425`** | `0x48358` | **1.7 / 4 — gravidade de arma em voo** |
| `0.17` | `0x48360` | 1.7 / 10 |

`0.45` **não existe** no arquivo em nenhuma precisão — era invenção minha,
calibrada por captura de tela. Ficou a 6% do valor real por sorte.

## Receita 2 — varrer o pool de constantes inteiro

```bash
python3 - <<'PY'
import struct
d = open("reference/decomp/lf2.exe","rb").read()
for off in range(0x47900, 0x48400, 8):
    v = struct.unpack("<d", d[off:off+8])[0]
    if v == v and v != 0 and 0.001 < abs(v) < 1000:
        print(hex(off), round(v, 6))
PY
```

O pool `0x479xx-0x483xx` é a física do jogo. Além das gravidades, ele tem
`±17`, `±16`, `±14`, `±13`, `±2.4`, `±2.2`, `±1.5`, `0.85`, `1.2`, `1.4`, `0.25`,
`0.4`, `0.7`, `8.5`, `9.9`, `11`, `12` — quase certamente as velocidades de
knockback e as faixas de tombo por `dvy` (o que o F.LF declara como lookup em
`GC.fall.wait180`). **Atacar `hitstop`, `bdefend` e o tombo daqui, com fonte
primária, em vez de copiar do F.LF.**

## Receita 3 — OpenLF2 como pedra de roseta

```bash
cd ~ && git clone --depth 1 https://github.com/xsoameix/openlf2   # HOME, não o mount
```

Duas coisas que só ele dá:

1. **`include/*.h` com offsets anotados** — `object.h`, `frame.h`, `itr.h`,
   `bdy.h`, `file.h`. Onde o decomp escreve
   `*(int *)(this + iVar11 + 0x4d45dcc)`, o `object.h` diz que `0x10` é `x`,
   `0x40` é `x_velocity`, `0x70` é `frame_id1`. O `itr.h` confirmou campo por
   campo o layout que eu havia deduzido à mão do `fprintf` do decomp.
2. **Endereços nomeados** — `func_4171C0_is_itr_bdy_overlap`,
   `func_403270_teleport`, `func_417400_does_attack_success`,
   `func_417170_random`. Casam direto com os `FUN_004171c0` do nosso decomp: dá
   nome a função anônima, e daí se navega pelos chamadores.

`src/const.c` é a tabela de constantes nomeadas (itr kinds 0-16, estados,
ids de `.dat`). `src/class_global.c` é a rotina de colisão completa.

## Pipeline recomendado

1. **OpenLF2** nomeia o endereço e dá a semântica.
2. **decomp** mostra o fluxo de controle daquela função (é o único que mostra lógica).
3. **binário cru / objdump** resolve as constantes que os dois anteriores escondem.
4. **F.LF** só quando os três acima não têm o dado.

O decomp **não** ficou obsoleto: ele é o único que mostra *lógica* legível. Foi
dele que saiu o parser do `bg.dat` (`FUN_0040bff0`), o draw com parallax
(`FUN_0041a250`) e o layout do `itr` (via `FUN_0040d0a0`, que contém
`fprintf("zwidth: %d")`). O que a leitura direta faz é tapar o buraco dos floats.

## Ferramental disponível na sandbox

- `objdump` (instalado) — disassembly de PE/x86.
- `capstone` via `pip install capstone` — disassembly programável.
- `apt-get download <pkg>` + `dpkg-deb -x` — **não precisa de root**; foi assim
  que se obtiveram os headers reais do SDL2 (ver `tools/host_sdl.sh`).
- **Não** há decompilador (Ghidra/IDA/RetDec): dá para ler assembly e dados, não
  para gerar C.

## Receita 4 — recursos de UI embutidos no `.rsrc`

64 bitmaps de menu/HUD moram DENTRO do `lf2.exe` como `RT_BITMAP` (~3 MB):
`CHARMENU` e `BATTLEMODE` (705x487), `MENU_BACK1-13`, `ENDING`, `SCORE_BOARD1-4`,
`WORDS0-5`, `LF2_CURSOR`. Extraiveis **sem a instalacao do jogo**:

```bash
python3 tools/rsrc_extract.py reference/decomp/lf2.exe assets
python3 tools/bmp2png.py assets $(cd assets && ls ui_*.bmp | sed 's/\.bmp$//')
```

Duas armadilhas:
1. `RT_BITMAP` e' DIB **sem** o `BITMAPFILEHEADER` de 14 bytes — o `rsrc_extract`
   reconstroi. Gravar o blob cru produz arquivo invalido.
2. **Todos os 64 sao BI_RLE8.** O Pillow decodifica 22 por sorte e falha em 42
   com "not enough image data". Use `bmp2png.py`, que chama o ImageMagick — foi
   escrito exatamente para isso.

## Armadilha — funcoes falsas no `ghidra_export_c.py`

Alem dos stubs `int3` ja conhecidos, o modo "orphan code" promove **padding de
hot-patch do MSVC** a funcao. Exemplo em `0x0040d6e7`:

```
40d6e5:  jmp  0x40d6f0            ; o fluxo real salta POR CIMA
40d6e7:  lea  0x0(%esp),%esp      ; 7 bytes de NOP
40d6ee:  mov  %edi,%edi           ; 2 bytes de NOP
40d6f0:  <codigo real>
```

Sintoma no decomp: cascata de `unaff_EBP`/`unaff_ESI`/`in_stack_0000...` logo no
inicio. Isso NAO e' ruido de FPO — e' o Ghidra cravando o boundary no lugar
errado. Ao ver esse padrao, desassemble o endereco antes de ler o pseudocodigo.

## Ancoras textuais — chaves do parser e formatos de escrita do `.dat`

Dois conjuntos de strings, uteis para validar o nosso parser token a token:

- `0x47c10`-`0x47f80` — formatos de **escrita**: `"kind: %d  x: %d  y: %d  w: %d  h: %d"`,
  `"arest: %d "`, `"vrest: %d "`, `"effect: %d "`, `"bdefend: %d "`, `"zwidth: %d "`,
  `"injury: %d "`, `"respond: %d "`, `"catchingact: %d %d "`, e a linha completa de
  `wpoint` e de `opoint`.
- `0x483d0`-`0x48620` — **chaves do parser**, sem `%d`: `kind:`, `effect:`,
  `zwidth:`, `injury:`, `bdefend:`, `respond:`, `vrest:`, `arest:`, `weaponact:`,
  `catchingact:`.

Ressalva: ordem na tabela de strings **nao e'** ordem no struct. Os offsets do
`itr` vem do assembly (`dvx` +0x14, `fall` +0x1C, `arest` +0x20, `vrest` +0x24,
`effect` +0x2C, `bdefend` +0x40, `injury` +0x44), nao da sequencia de literais.


## Correcao na Receita 1 — os DOIS `0.17` nao sao o mesmo double

Verificado byte a byte (offsets de ARQUIVO, nao RVA):

```
0x47978  c3 f5 28 5c 8f c2 c5 3f  = 0.17                  (literal exato)
0x48360  c2 f5 28 5c 8f c2 c5 3f  = 0.16999999999999998   (== 1.7/10)
```

Um ULP de diferenca, ultimo byte `c3` vs `c2`. Cada padrao aparece **uma** vez no
arquivo. `struct.pack("<d", 0.17)` acha so' o de `0x47978`.

Isso **reforca** a leitura da familia da gravidade em vez de contradize-la: o
valor em `0x48360` ser `1.7/10` computado, e nao um `0.17` escrito a mao, e'
evidencia de que ele pertence a familia do 1.7. O de `0x47978` e' um literal
independente, no cluster de knockback (`±17`/`±16`).

**Licao de metodo:** o `round(v, 6)` do scan da Receita 2 colide valores a um ULP
de distancia. Para o registro, anotar sempre a precisao total — senao uma busca
por bytes exatos no futuro nao acha a entrada.

## Pool de constantes — entradas que faltavam no resumo

Mesma faixa `0x47900`-`0x48400`, todas confirmadas byte a byte:

| Offset | Valor | | Offset | Valor |
|---|---|---|---|---|
| `0x47928` | 300.0 | | `0x482e8` | -0.1 |
| `0x47930` | -200.0 | | `0x482f0` | -0.7 |
| `0x47938` | 580.0 | | `0x482f8` | -10.0 |
| `0x47940` | 6.0 | | `0x48308` | -5.0 |
| `0x47980` | 8.0 | | `0x48310` | 0.5 |
| `0x479a8` | 40.0 | | `0x48328` | -3.5 |
| `0x479c0` | 4.0 | | `0x48330` | -7.0 |
| `0x479d8` | -2.0 | | `0x48340` | -8.0 |
| `0x47a00` | 10.0 | | `0x48390` | 0.2 |
| `0x47a08` | 1.0 | | | |
| `0x47a18` | 0.3 | | | |
| `0x47a40` | 3.0 | | | |

**`300.0`/`-200.0`/`580.0` identificados:** nao sao limite de tela. Sao a posicao
inicial de spawn, gravada na construcao do objeto em `0x0040689e`-`0x004068d0`:

```
40689e:  fldl  0x447938        ; 580.0
4068b8:  fstpl 0x58(%ebx)      ; objeto->x_position = 580
4068bb:  fldl  0x447930        ; -200.0
4068c7:  fstpl 0x60(%ebx)      ; objeto->y_position = -200  (acima do chao)
4068ca:  fldl  0x447928        ; 300.0
4068d0:  fstpl 0x68(%ebx)      ; objeto->z_position = 300
```

Offsets `+0x58`/`+0x60`/`+0x68` = `x_position`/`y_position`/`z_position`
(OpenLF2 `object.h`, Nivel C). **Isso NAO resolve** `Z_MIN`/`Z_MAX` do porte —
e' um spawn padrao, nao um limite. O item continua aberto na
`AUDITORIA_SUPERFICIE.md`.

**`-8.0` (`0x48340`) e `8.0` (`0x47980`)** ja tem uso confirmado: sao os limiares
de selecao dos frames de queda 180-183 em `0x0040e242`. Ver `AUDITORIA_SUPERFICIE`.


## Receita 5 — validar as fronteiras de funcao do decomp

`tools/fn_boundary_check.py`. Desassembla o `.text` uma vez e cruza com os
rotulos `FUN_` do `lf2_decomp.c`, marcando como LIXO todo endereco que:

- comeca com `int3` (os stubs ja conhecidos);
- comeca com NOP de alinhamento (`lea 0x0(%esp),%esp`, `mov %edi,%edi`, `nop`);
- tem um `jmp` imediatamente antes saltando **por cima** dele.

```bash
python3 tools/fn_boundary_check.py reference/decomp/lf2.exe \
                                   reference/decomp/lf2_decomp.c \
  > reference/decomp/FUNCOES_FALSAS.txt
```

**Resultado na primeira execucao (2026-08-12):**

| Veredito | Quantidade |
|---|---|
| entrada de funcao plausivel | 285 |
| **LIXO — nao e' entrada de funcao** | **187** |
| prologo atipico (aviso, pode ser FPO real) | 2 |
| fora do intervalo desassemblado | 7 |

**39% do que o decomp chama de funcao nao e' funcao.** Exemplo verificado a mao:

```
408d1a:  push %ebp
408d1b:  jmp  0x408d20        ; salta POR CIMA
408d1d:  lea  0x0(%ecx),%ecx  ; <- FUN_00408d1d, 3 bytes de NOP
408d20:  cmp  %edi,%ebx       ; codigo real
```

**Como usar:** antes de gastar tempo lendo o pseudocodigo de um `FUN_xxxxxxxx`,
conferir se ele esta em `FUNCOES_FALSAS.txt`. O sintoma no decomp — cascata de
`unaff_EBP`/`unaff_ESI`/`in_stack_0000...` logo no inicio — e' facil de confundir
com ruido de FPO; nao e'. E' boundary errado, e ler dali leva a conclusoes sobre
codigo que nunca executa como entrada.

Prologo atipico e' **aviso, nao veredito**: com FPO, muita funcao real nao comeca
com `push %ebp`. `FUN_004061d0` cai nesse caso (comeca com `fldl`) e e' funcao
legitima — foi dela que saiu o `xor %ebx,%ebx` usado para refutar o `+0xEA`.

Origem da ideia: ferramentas de decompilacao assistida detectam fronteira por
prologo via Capstone. Aqui usamos `objdump`, que ja esta instalado e ja e' a
nossa camada de Nivel A — sem dependencia nova nem chave de API.


## Licoes da avaliacao de ferramentas de decompilacao assistida (2026-08-12)

Avaliados `Caleb-Balboni/AI-Decompiler` (LIEF+Capstone+PyQt6+LLM) e
`louisgthier/decompai` (LangGraph+Gradio, runner Kali com Ghidra/radare2).
**Nenhuma das duas adotada.** Uma ideia roubada. As licoes valem mais que o
veredito.

### 1. Avaliar contra o GARGALO, nao contra a capacidade

As duas fazem coisas impressionantes. Nenhuma ataca o que nos trava. Nosso
gargalo nao e' **produzir** C decompilado — ja temos 122.667 linhas de Ghidra —
e' **verificar** contra o assembly. A pergunta certa nao e' "isso e' bom?", e'
"isso ataca o que me trava?".

### 2. Gerador de artefato derivado AUMENTA a superficie de erro

A taxa-base do projeto: 9 em 9 parametros vindos de fonte derivada (F.LF,
comunidade, invencao) estavam errados. Uma ferramenta cujo produto e' C gerado
por LLM a partir do assembly adiciona **mais** material derivado.

E pior que o Ghidra num aspecto especifico: **o Ghidra e' ruim mas honesto.**
`unaff_ECX_07`, `undefined4`, `in_stack_0000001c` sao sinal VISIVEL de que ele
esta chutando — foi assim que percebemos o boundary errado em `0x40d6e7`. Um LLM
produz C limpo e plausivel mesmo quando erra. **A fluencia e' antifeature aqui.**

Botoes tipo "Explain" e "ID Algorithm" sao gerador de corroboracao falsa em forma
de produto: um modelo dizendo "isto parece calculo de knockback" e' Nivel D
vestido de Nivel A.

### 3. Checar compatibilidade ANTES de arquitetura

`decompai` diz no README: "x86 Linux ELF binaries only", Windows PE no roadmap.
Nosso alvo e' PE32. Bloqueio duro, visivel na primeira tela — e eu gastei
analise de arquitetura antes de conferir o requisito basico.

### 4. A IDEIA vale mais que a FERRAMENTA

As duas ferramentas juntas renderam **uma** ideia aproveitavel: deteccao de
fronteira de funcao por prologo. Virou `tools/fn_boundary_check.py`, ~100 linhas,
sem dependencia nova, usando `objdump` que ja tinhamos.

Retorno: **187 dos 481 `FUN_` do decomp nao sao entrada de funcao** (39%).
Nenhuma das duas ferramentas teria dado isso — a primeira nao roda headless, a
segunda nao le PE.

### 5. Nao confundir "projeto imaturo" com "ideia ruim"

`AI-Decompiler` tem 2 estrelas e um README que manda clonar a org errada com o
nome escrito errado. Sinal de maturidade real, e vale registrar. Mas **o
argumento decisivo nao foi esse** — foi o gargalo. E a ideia que aproveitamos
veio justamente do projeto de 2 estrelas, nao do de 207.

### 6. Reconhecer quando o fluxo atual JA e' a ferramenta

`decompai` e' um agente conversacional sobre `objdump`, `gdb` e Ghidra. E'
exatamente o laco que ja executamos: temos bash, o binario, o decomp e as
ferramentas. Adotar um wrapper para capacidade que ja existe custa dependencia
sem ganho.

### 7. O que nenhuma das duas resolve

Reconstruir o fluxo de controle de uma funcao especifica — decidir o que
`0x0042ea8c`-`0x0042ec33` faz — e' raciocinio sobre desvios, nao geracao de
texto. Continua sendo trabalho manual, e e' onde esta o valor.

---

## Segunda rodada de avaliacao — `LLM4Decompile` e `python-decompile3` (2026-08-12)

Veredito: **nenhum dos dois e' adotavel.** Mas um deles traz um numero que fecha
uma discussao que ate' aqui vinha sendo feita por intuicao.

### 8. `python-decompile3` — descartado pela Licao 3, em trinta segundos

Decompila **bytecode Python 3.7-3.8**. Nosso alvo e' PE32 i386 compilado com
MSVC 8.0. Nao existe bytecode Python em lugar nenhum do projeto.

Compatibilidade checada ANTES de arquitetura, que e' exatamente o que a Licao 3
manda fazer. Custo da avaliacao: um `web_fetch`.

O README, porem, tem uma frase do autor (linhagem `uncompyle6`, 3422 commits no
problema):

> *"There are numerous bugs in decompilation. And that's true for every other
> CPython decompiler I have encountered, even the ones that claimed to be
> 'perfect'."*

E o motivo tecnico que ele da': casar padroes de desvio parou de funcionar
quando os compiladores comecaram a otimizar, e foi preciso ir para dominadores e
dominadores reversos.

Isso e' **corroboracao independente da nossa classificacao de Nivel B**. Se vale
para bytecode Python — que e' de alto nivel, tipado por instrucao, sem registros,
sem FPO e sem alocacao — vale com muito mais forca para x86 otimizado do MSVC.
Nao e' evidencia sobre o `lf2.exe`; e' evidencia sobre a *classe* de ferramenta.

### 9. `LLM4Decompile` — incompativel, e o numero publicado explica por que isso importa

Suporte declarado: **Linux x86_64**, GCC O0-O3. Nosso binario e' **PE32 i386,
MSVC 8.0**. Tres incompatibilidades simultaneas: 32 vs 64 bits, PE vs ELF, MSVC
vs GCC. O modelo recebe texto de `objdump`, entao tecnicamente aceitaria
assembly i386 — mas foi treinado so' em x86_64/GCC, e para um modelo estatistico
"fora da distribuicao em tres eixos" nao e' detalhe.

**O numero.** Eles publicam re-executabilidade medida:

| Modelo | Re-executabilidade |
|---|---|
| `llm4decompile-1.3b-v1.5` | 27,3% |
| `llm4decompile-6.7b-v1.5` | 45,4% |
| `llm4decompile-6.7b-v2` | 52,7% |
| `llm4decompile-22b-v2` | 63,6% |
| **`llm4decompile-9b-v2`** | **64,9%** |

E o benchmark e' o **HumanEval-Decompile**: 164 funcoes C que dependem
exclusivamente de biblioteca padrao. Funcoes pequenas, autocontidas, de
livro-texto, GCC, x86_64 — o caso mais facil e mais favoravel possivel.

Ou seja: **o estado da arte, no caso facil, produz codigo que roda certo 65% das
vezes.** Um terco sai nao-funcional. E "re-executavel" e' uma barra mais BAIXA
que "semanticamente identico" — uma funcao pode passar nas assercoes e ainda
errar caso de borda.

Nosso alvo sao metodos `__thiscall` de milhares de linhas com FPO, e a precisao
que precisamos nao e' "passa nos testes": e' "o limiar e' 30, nao 60" e "o campo
e' `+0xB8`, nao `+0xEA`". A taxa-base de 9/9 de `AUDITORIA_SUPERFICIE.md` ja
dizia isso; agora existe numero publicado pelos proprios autores da ferramenta.

Detalhe util: a serie V2 (`LLM4Decompile-Ref`) **refina pseudo-codigo do Ghidra**
em vez de traduzir assembly cru, e bate a serie end-to-end. Valida o arranjo em
camadas — mas valida "usar o Ghidra como entrada", que e' o que ja fazemos.

### 10. A ideia que vale roubar: medir COMPORTAMENTO, nao aparencia

O aproveitavel do `LLM4Decompile` nao e' o modelo. E' a metrica.

Eles nao avaliam decompilacao por quanto o codigo *parece* certo — compilam e
rodam as assercoes originais. Metrica objetiva, nao escore de similaridade.

Nosso analogo seria um **oraculo diferencial contra o LF2 original**: mesma
sequencia de entrada nos dois engines, comparacao de traco de estado. Isso tira
o projeto de "verificar constante por constante" e leva para "verificar
comportamento por atacado".

**Isto deixou de ser especulacao em 2026-08-12.** O mecanismo esta identificado
e documentado em `AUDITORIA_2026-08-12.md#a13`:

- A aleatoriedade da partida nao vem de `rand()`. Vem de uma **tabela de 3000
  bytes** em `0x0044FF90`, gerada uma vez, consumida por `FUN_00417170` com dois
  cursores (`0x00450C34` mod 1234, `0x00450BCC` mod 3000).
- A tabela e o cursor sao **serializados no `.lfr`** (`0x0043da30` grava,
  `0x0043e3e0` restaura, `0x0040304a`/`0x00428679` fazem o disco, tamanho
  `0xbb9`).
- Por isso o replay exige `.dat` identicos — a mensagem em `0x49b60` diz isso com
  todas as letras. O `.lfr` guarda **entrada + semente**, e o engine **re-simula**.

Consequencia: **uma partida do LF2 e' funcao deterministica de (tabela, cursores,
entradas, `.dat`)**. Nao ha PRNG a bit-casar, nao ha ponto flutuante de
plataforma no sorteio, nao ha relogio. O oraculo e' construivel.

O que falta, e nao e' pouco: reverter o formato do `.lfr` (ha checksum e teste de
versao), e instrumentar o porte para emitir traco comparavel. E' projeto maior
que qualquer item aberto da auditoria de superficie. **Registrado como plano, nao
como proxima tarefa.**

### 11. Contabilidade das quatro ferramentas avaliadas

| Ferramenta | Adotada | O que rendeu |
|---|---|---|
| `AI-Decompiler` | nao | fronteira de funcao por prologo → `fn_boundary_check.py` |
| `decompai` | nao | nada; o fluxo atual ja e' a ferramenta |
| `python-decompile3` | nao | corroboracao independente do Nivel B |
| `LLM4Decompile` | nao | 64,9% como teto medido; a ideia do oraculo comportamental |

Quatro avaliacoes, zero adocoes, dois itens de metodo e um script de 100 linhas.
A licao agregada e' a Licao 4 confirmada: **avaliar ferramenta pela ideia que ela
carrega, nao pela capacidade que ela anuncia** — e o custo de avaliar assim e'
baixo o suficiente para continuar fazendo.

---

## Terceira rodada — `Pepper` e `rdecomp` (2026-08-12)

Primeira rodada em que **a compatibilidade passa nos dois**. `Pepper` le' PE32
x86; `rdecomp` le' PE e x86. A Licao 3 nao descarta nenhum dos dois, entao foi
preciso avaliar de verdade.

Resultado: **nenhum adotado, e os dois renderam.**

### 12. `Pepper` — nao adotado, mas apontou DOIS lugares que nunca olhamos

`Pepper` e' um visualizador de PE32/PE32+ construido sobre `libpe`. 179 estrelas,
244 commits, autor serio (o mesmo do `HexCtrl`). GUI Windows, com MFC.

Contra a adocao: tudo que ele mostra — cabecalhos, imports, secoes, recursos —
nos ja' extraimos com script. A tabela de import saiu em uma chamada de Python
nesta mesma sessao; os 64 recursos sairam pelo `rsrc_extract.py`. Adotar uma GUI
para capacidade que ja' temos scriptada, e num fluxo que roda headless em Linux,
seria andar para tras.

Mas a **lista de features dele funcionou como checklist do que nao tinhamos
olhado**, e dois itens dessa lista deram resultado:

**Rich Header.** Presente e integro (chave `0x418e5e9a`). Traz a versao de cada
ferramenta que gerou objeto. Build dominante **50727** = `14.00.50727` = **Visual
Studio 2005 SP1** — confirmacao independente do "MSVC 8.0" que ate' aqui vinha do
manifesto e do campo de versao do linker, e mais precisa que eles. De quebra:
existem objetos de toolchains bem mais antigas (builds `9466`, `9178`, `7299`,
`4035`), o que indica bibliotecas estaticas ou objetos legados relinkados —
explica o codigo nao ser estilisticamente uniforme ao longo do `.text`.

**Debug Directory.** **Ausente** (RVA 0, tamanho 0). Nenhuma entrada CodeView,
logo nenhum caminho de PDB e nenhum nome de arquivo-fonte. Era a aposta de maior
retorno possivel e deu negativo — o que tambem e' resultado: fecha a porta e
evita que alguem gaste tempo nela de novo.

Registrado em `AUDITORIA_2026-08-12.md`, no bloco de identificacao do binario.

Licao: **um catalogo de features de uma ferramenta madura vale como lista de
verificacao, mesmo quando a ferramenta nao vai ser instalada.**

### 13. `rdecomp` — recusado pelo mesmo argumento da Licao 2

Decompilador x86/x64 em Rust, le' ELF e PE. Pipeline completo: loader, descoberta
de funcao, CFG, IR, passes, geracao de pseudocodigo. 12 commits, 1 estrela,
`Status: Experimental`, e o README diz com todas as letras que a saida deve ser
tratada como *"recovered pseudocode, not source-equivalent reconstruction"*.

O argumento decisivo **nao** e' a imaturidade — a Licao 5 existe justamente para
nao confundir isso. O argumento e' a Licao 2:

> Gerador de artefato derivado AUMENTA a superficie de erro.

`rdecomp` produz exatamente a classe de artefato que ja' rebaixamos a "indice,
nao fonte": pseudocodigo C de Nivel B. Ja' temos essa camada, produzida pelo
Ghidra, que e' o estado da arte da categoria. Trocar por uma implementacao de 12
commits, ou pior, somar uma segunda fonte de Nivel B divergente, e' negativo em
qualquer cenario.

Dois agravantes especificos, e vale separa-los do argumento principal:

1. Os 559 testes sao **estruturais** (a saida contem um `while`?), nao de
   re-executabilidade. Nao existe numero de correcao publicado — nesse quesito e'
   pior que o `LLM4Decompile`, que ao menos publica 64,9%.
2. Os binarios PE do corpo de teste sao compilados com **gcc** em O1/O2/O3.
   MSVC 8.0 com FPO e `__thiscall` nao esta coberto.

### 14. A ideia roubada: inventario de campos por deslocamento

O que salvou o `rdecomp` da avaliacao foram dois arquivos do repositorio dele:
`struct_recovery.rs` e `typing.rs`.

Recuperacao de estrutura **e' exatamente o nosso gargalo**. Todo campo do
`object_t` foi descoberto de forma REATIVA: um bug aparece, vamos ao assembly,
achamos `+0xB8`. Foi assim com `fall`, `bdefend` e `shaking` — e por isso os tres
passaram sessoes inteiras sem existir no porte.

Virou `tools/struct_harvest.py`, ~90 linhas sobre `objdump`, sem dependencia
nova. Mesmo padrao do `fn_boundary_check.py`: a ideia veio de fora, o codigo e'
nosso e usa a camada de Nivel A que ja' tinhamos.

Primeira execucao, sobre as tres rotinas ja' auditadas:

```
deslocamentos distintos: 113   ja provados: 29   candidatos: 84
```

**84 deslocamentos que o engine toca em codigo que ja' auditamos e que nunca
olhamos.** Os de maior volume:

| off | R | W | larg | leitura provavel |
|---|---|---|---|---|
| `0x194` | 289 | 0 | 4 | tabela global de objetos, `[esi + slot*4]` |
| `0x364` | 9 | 7 | 4 | vizinho de `+0x368` (`file`) |
| `0x354` | 8 | 4 | 4 | **indice de slot** — usado como indice em `+0x194` |
| `0x300`/`0x348`/`0x34C` | 0 | 2-3 | 4 | so' escrita, via `add` — acumuladores |
| `0xBE`-`0xC5` | 7-2 | 2 | 1 | oito bytes consecutivos, comparados com 5 |

O caso `+0x354` ja' rendeu leitura util em `0x0042e9af`-`0x0042e9c3`: quando a
vitima e' personagem (`file->type == 0`) e `+0x2f4 == -1`, o dano tambem e'
somado em `+0x348` do objeto **referenciado pelo `+0x354` do atacante**. Isso e'
atribuicao de dano ao criador — quem leva o credito quando um projetil acerta.

**Ressalva que precisa ficar colada no resultado.** O script nao sabe para qual
struct o registrador-base aponta. No bloco de acerto, `%edx` ora e' `object_t`,
ora e' `itr` (passo 0x50), ora e' `frame`. Prova disso esta na propria saida:
`0x2c` aparece com 16 leituras e o exemplo e' `cmpl $0x15,0x2c(%edx)` — que nao
e' `object_t+0x2C`, e' `itr->effect` comparado com 21.

Entao **cada linha da saida e' Nivel D**. O script economiza a busca, nao a
prova. Usar a saida como mapa de campos seria cometer o vicio que a auditoria de
superficie documenta, so' que industrializado.

### 15. Contabilidade atualizada — seis ferramentas

| Ferramenta | Adotada | O que rendeu |
|---|---|---|
| `AI-Decompiler` | nao | fronteira de funcao por prologo → `fn_boundary_check.py` |
| `decompai` | nao | nada |
| `python-decompile3` | nao | corroboracao independente do Nivel B |
| `LLM4Decompile` | nao | 64,9% como teto medido; oraculo comportamental |
| `Pepper` | nao | Rich header (VS2005 SP1) e debug dir ausente |
| `rdecomp` | nao | inventario de deslocamento → `struct_harvest.py` |

Seis avaliacoes, zero adocoes, dois scripts e varios itens de metodo. O padrao
esta estavel o bastante para virar regra: **nao instalamos ferramenta de
decompilacao; lemos o que ela se propoe a fazer e implementamos a parte que
ataca o nosso gargalo em cima do `objdump`.**
