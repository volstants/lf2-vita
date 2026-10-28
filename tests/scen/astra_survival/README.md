# astra_survival

Survival selecionado pela interface (stage_count=50); Davis e Julian, início e ataques. Não testa uma corrida completa de Survival.

Formato original: events.txt contém frame e mensagem/wParam/lParam hexadecimal; marks.json registra frames e marcas. Executar 1452 frames (marks.frames - 1).

Comparação estrita: identical: 1452 frames (data every frame, heap on 145 frames), last 1452, run completed. Os logs mantêm a .data a cada frame e o heap do oráculo a cada 10 frames.

Regenerar com `python3 private/tools/astra_validate_scenarios.py --oracle-python /caminho/venv/bin/python astra_survival`. Os patches privados desta entrega são necessários para gravação/replay. O roteiro não está em suite.txt.
