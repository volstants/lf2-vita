# astra_championship

Championship 2 on 2: Davis humano e sete CPU aleatórios; confirma o bracket, ajustes e entrada em combate. Não percorre o torneio inteiro; não cobre 1 on 1 (432ab0).

Formato original: events.txt contém frame e mensagem/wParam/lParam hexadecimal; marks.json registra frames e marcas. Executar 1263 frames (marks.frames - 1).

Comparação estrita: identical: 1263 frames (data every frame, heap on 126 frames), last 1263, run completed. Os logs mantêm a .data a cada frame e o heap do oráculo a cada 10 frames.

Regenerar com `python3 private/tools/astra_validate_scenarios.py --oracle-python /caminho/venv/bin/python astra_championship`. Os patches privados desta entrega são necessários para gravação/replay. O roteiro não está em suite.txt.
