# astra_fusion

Firen/Freeze da equipe 1 correm um contra o outro; Julian da equipe 2 permite o combate. O código lf2.net habilita a fusão com HP cheio: o limiar normal de vida baixa não é coberto. Firzen surge no frame 974 e se separa após 4500 ticks (marca 5584).

Formato original: events.txt contém frame e mensagem/wParam/lParam hexadecimal; marks.json registra frames e marcas. Executar 5703 frames (marks.frames - 1).

Comparação estrita: identical: 5703 frames (data every frame, heap on 570 frames), last 5703, run completed. Os logs mantêm a .data a cada frame e o heap do oráculo a cada 10 frames.

Regenerar com `python3 private/tools/astra_validate_scenarios.py --oracle-python /caminho/venv/bin/python astra_fusion`. Os patches privados desta entrega são necessários para gravação/replay. O roteiro não está em suite.txt.
