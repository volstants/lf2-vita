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
