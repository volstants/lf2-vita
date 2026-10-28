# Auditar e extrair o LF2 com Ghidra

Procedimento revisado em 2026-09-20. Resultado executado e evidencias:
`AUDITORIA_2026-09-20_GHIDRA.md`.

O pseudocodigo e uma interpretacao. O assembly e os bytes do executavel
continuam sendo a referencia. Sucesso da decompilacao nao prova equivalencia
semantica, tipos corretos ou recuperacao do codigo-fonte original.

## Ambiente validado

- Executavel: `reference/decomp/lf2.exe`, LF2 v2.0a, SHA256
  `12dfa00f6b767508612550e9ab27ab74b4201ff4cb9ff31d068925924eab8fc5`.
- Ghidra instalado: `C:/Ghidra/ghidra_12.1.3_PUBLIC`.
- Projeto existente: `reference/ghidra/lf2.gpr`, programa `/lf2.exe`.
- JDK: `C:/Program Files/Microsoft/jdk-21.0.12.8-hotspot`.
- Python: `.venv/Scripts/python.exe`, com PyGhidra, pefile 2024.8.26 e
  capstone 5.0.9. Os dois ultimos foram instalados nessa venv na auditoria.

## Procedimento reproduzivel, em PowerShell

Execute na raiz do repositorio, com o projeto Ghidra fechado para obter uma
copia consistente. Escolha uma pasta de saida nova para cada auditoria.
O script copia o projeto para `SAIDA/project/`; so essa copia recebe alteracoes.
Nao substitui `.ghidra-exports`, `lf2_decomp.c` ou o banco original.

```powershell
$env:JAVA_HOME = 'C:/Program Files/Microsoft/jdk-21.0.12.8-hotspot'
$lfAuditOut = 'reference/audit-ghidra-nova'
.\.venv\Scripts\python.exe tools/ghidra_audit.py --output $lfAuditOut
.\.venv\Scripts\python.exe tools/lf2_binary_inventory.py --output $lfAuditOut
.\.venv\Scripts\python.exe tools/ghidra_audit.py --output $lfAuditOut --stage probe --probe
.\.venv\Scripts\python.exe tools/ghidra_audit.py --output $lfAuditOut --stage enhanced --enhance --fix-callback-conventions --export --supplement
.\.venv\Scripts\python.exe tools/lf2_verify_ghidra_audit.py $lfAuditOut
```

`--probe` registra a tentativa normal e outra com limites maiores para
`0041bc90`, alem de candidatos a entradas orfas. E diagnostico, nao necessario
para a exportacao. Na auditoria, ambas as tentativas funcionaram: a causa da
falha da exportacao anterior nao foi reproduzida.

Os scripts de recuperacao e complemento SEH sao especificos deste binario.
O SHA256 do programa Ghidra precisa coincidir com o executavel; referencias
de tabelas e retornos dos callbacks recebem verificacoes adicionais.

## Artefatos

- `baseline/`: inventario do banco anterior, lacunas, assembly e constantes.
- `enhanced/exports/`: JSONs por funcao com C, bytes, referencias a dados,
  chamadas com endereco da instrucao, P-code e CFG.
- `enhanced/lf2_recovered.c`: pseudocodigo consolidado.
- `enhanced/text.asm`: instrucoes reconhecidas, bytes e funcao proprietaria.
- `enhanced/float_constants.json`: valores candidatos, largura da instrucao
  e referencias; nao deduz tipos somente pela sequencia de bytes.
- `enhanced/seh_fragments/`: filtros e handlers contextuais de excecao.
  As funcoes temporarias usadas para decompila-los sao revertidas.
- `enhanced/gap_review.json`: classificacao de todas as faixas indefinidas.
- `binary_inventory.json`: imports, DLLs locais candidatas e manifesto.
- `verification.json`: verificacao de bytes, limites e hashes preservados.

O P-code novo usa o modo `decompile` (otimizado); os JSONs antigos usavam
`normalize`. Comparar contagens de operacoes entre eles nao mede melhoria.

## Exportador C simples

`ghidra_export_c.py` continua sendo um post-script para o Ghidra. Agora usa
sintaxe Python 3, exporta somente funcoes reconhecidas e registra falhas em
`ARQUIVO.c.status.json`. Nao desassembla bytes arbitrarios nem promove padding.

Variaveis de ambiente: `LF2_DECOMP_OUT` (arquivo C), `LF2_DECOMP_TIMEOUT`
(padrao 180 segundos por funcao), `LF2_DECOMP_PAYLOAD_MB` (padrao 128),
`LF2_DECOMP_ADDRESSES` (opcional, enderecos hexadecimais de oito digitos,
separados por virgula, para uma exportacao direcionada).

O pre-script `ghidra_pre_aggressive.py` descobre candidatos. Quantidade maior
de funcoes, C maior ou analise agressiva nao sao medidas de fidelidade.
O procedimento antigo que criava funcoes de todo trecho orfao foi removido.
