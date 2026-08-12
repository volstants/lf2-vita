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
