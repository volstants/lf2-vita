# astra_recording

Habilita gravação pela interface, inicia VS Julian/Bandit, derrota Bandit e salva recording\20090711_120000_VS.lfr pelo jogo. Não edita a gravação.

Formato original: events.txt contém frame e mensagem/wParam/lParam hexadecimal; marks.json registra frames e marcas. Executar 1798 frames (marks.frames - 1).

Comparação estrita: identical: 1798 frames (data every frame, heap on 179 frames), last 1798, run completed. Os logs mantêm a .data a cada frame e o heap do oráculo a cada 10 frames.

Regenerar com `python3 private/tools/astra_validate_scenarios.py --oracle-python /caminho/venv/bin/python astra_recording`. Os patches privados desta entrega são necessários para gravação/replay. O roteiro não está em suite.txt.
