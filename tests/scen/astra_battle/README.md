# astra_battle

Battle mode com Davis/Julian e configuração padrão de exércitos; inclui edição, confirmação e combate. army_kinds=0 é ensaio negativo separado.

Formato original: events.txt contém frame e mensagem/wParam/lParam hexadecimal; marks.json registra frames e marcas. Executar 1589 frames (marks.frames - 1).

Comparação estrita: identical: 1589 frames (data every frame, heap on 158 frames), last 1589, run completed. Os logs mantêm a .data a cada frame e o heap do oráculo a cada 10 frames.

Regenerar com `python3 private/tools/astra_validate_scenarios.py --oracle-python /caminho/venv/bin/python astra_battle`. Os patches privados desta entrega são necessários para gravação/replay. O roteiro não está em suite.txt.
