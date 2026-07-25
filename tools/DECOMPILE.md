# Decompilar o LF2 original (Ghidra headless)

Objetivo: ter o C decompilado do `lf2.exe` para consultar a mecânica **na fonte**,
em vez de inferir dos `.dat` ou confiar em reimplementações (F.LF / OpenLF2).

## 1. Instalar (no WSL Ubuntu)

```bash
sudo apt update && sudo apt install -y openjdk-21-jdk unzip wget
cd ~ && wget https://github.com/NationalSecurityAgency/ghidra/releases/download/Ghidra_11.3.1_build/ghidra_11.3.1_PUBLIC_20250219.zip
unzip -q ghidra_11.3.1_PUBLIC*.zip && mv ghidra_11.3.1_PUBLIC ~/ghidra
```
(Se a URL falhar, pegue o .zip mais recente em github.com/NationalSecurityAgency/ghidra/releases)

## 2. Rodar (leva ~10-40 min; usa bastante RAM)

Instalação do LF2 neste PC: `C:\Program Files (x86)\LittleFighter`

```bash
cd /mnt/c/Users/rodrigo.chiesa/Documents/LittleFighter2Vita
mkdir -p reference/decomp && cd reference/decomp

# 1. ver o nome exato do exe
ls -la "/mnt/c/Program Files (x86)/LittleFighter/"*.exe

# 2. copiar (ajuste o nome se não for lf2.exe)
cp "/mnt/c/Program Files (x86)/LittleFighter/lf2.exe" .

# 3. decompilar (com análise agressiva — bem mais funções que o padrão)
rm -rf lf2proj*
export LF2_DECOMP_OUT=$PWD/lf2_decomp.c
~/ghidra/support/analyzeHeadless $PWD lf2proj \
  -import lf2.exe \
  -preScript ../../tools/ghidra_pre_aggressive.py \
  -postScript ../../tools/ghidra_export_c.py \
  -scriptPath ../../tools \
  -max-cpu 4 \
  -deleteProject
```

O `-preScript` liga o *Aggressive Instruction Finder* (acha código que a análise
por referências não alcança) e o *Decompiler Parameter ID*. O `-postScript`
ainda recupera funções órfãs (desassembla bytes indefinidos no `.text`, promove
alvos de `call` sem função) antes de decompilar. Esperado: de ~350 para milhares
de funções; leva bem mais tempo (10-40 min) e o `.c` fica maior.

Saída: `reference/decomp/lf2_decomp.c` (dezenas de MB — é normal).
`reference/` já está no `.gitignore`.

## 3. Usar

Não é para ler inteiro — é para **grep direcionado**. Exemplos do que procurar
quando bater dúvida de mecânica:

```bash
grep -n "0x3fd99999\|1.7"      lf2_decomp.c   # gravidade
grep -n "== 60\|> 60"          lf2_decomp.c   # KO / falling points
grep -n "1000\|999"            lf2_decomp.c   # códigos de next
grep -nA40 "FUN_00417" lf2_decomp.c           # função específica
```

Peça ao Claude para fazer a busca: ele lê o arquivo por grep/sed sem precisar
carregar tudo no contexto.

## Notas

- Ghidra não recupera nomes (sem símbolos no exe): as funções saem como
  `FUN_00401234`. Identifica-se pelo uso de constantes e strings próximas.
- O `openlf2.exe` do repo de referência é o LF2 com uma DLL de hook injetada;
  prefira o exe limpo.
- Alternativa mais leve p/ perguntas pontuais: `objdump -d` + busca por
  constante (a sandbox do Claude tem objdump/capstone/pefile).
