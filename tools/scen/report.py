#!/usr/bin/env python3
"""Markdown report of the scenario tests (Portuguese, for the project docs).

  python3 tools/scen/report.py <game_dir> <out.md> <scen_dir>...
"""
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

TITLES = {
    "s1_davis_firen": "S1 Davis x Firen — projéteis, combos, lançamento, fogo",
    "s2_freeze_woody": "S2 Freeze x Woody — gelo, estilhaçar, redemoinho, teleporte",
    "s3_rudolf_john": "S3 Rudolf x John — agarrão, transformação, clones, invisibilidade, cura",
    "s4_louis_deep": "S4 Louis x Deep — transformação em LouisEX, agarrão, espada",
    "s5_henry_dennis": "S5 Henry x Dennis — armas (F8), arremessos, bebida, flechas, flauta",
    "s6_firzen_julian": "S6 Firzen x Julian — personagens ocultos (lf2.net)",
    "s7_knockback": "S7 Golpes fracos x fortes — velocidade, altura e distância do arremesso",
    "s8_chaos": "S8 Caos com 8 lutadores — 6 CPUs, armas, status cruzados",
    "s9_stage": "S9 Stage mode — inimigos, IA de fase, contagem de abates",
}


def fmt_kb(kb):
    if not kb:
        return "—"
    return ("vx %s · sobe %s · alt %s · dist %s · deitado em %s" %
            (kb["max_abs_vx"], kb["max_up_vy"], kb["max_height"], kb["distance_x"],
             "%s f" % kb["frames_to_lying"] if kb["frames_to_lying"] is not None else "—"))


def fighter_cell(f):
    if not f:
        return "—"
    hp0, hpmin, _ = f["hp"]
    st = ", ".join(f["status"]) or "—"
    return "%s · HP %d→%d · %s" % (f["fighter"], hp0, hpmin, st)


def main():
    game, out, dirs = sys.argv[1], sys.argv[2], sys.argv[3:]
    lines = []
    summary = []
    for d in dirs:
        name = os.path.basename(d.rstrip("/"))
        cmp_txt = open(os.path.join(d, "compare.txt")).read() if os.path.exists(os.path.join(d, "compare.txt")) else "(sem replay)"
        if not os.path.exists(os.path.join(d, "analysis.json")) and os.path.exists(os.path.join(d, "oracle_obj.txt")):
            subprocess.run([sys.executable, os.path.join(HERE, "analyze.py"), d, game], check=True,
                           stdout=subprocess.DEVNULL)
        an = json.load(open(os.path.join(d, "analysis.json"))) if os.path.exists(os.path.join(d, "analysis.json")) else []
        m = json.load(open(os.path.join(d, "marks.json")))
        ok_hash = "identical:" in cmp_txt
        ok_obj = "object-rows; identical" in cmp_txt
        ok_mem = "memory frame" in cmp_txt and ": identical" in cmp_txt.split("final memory")[-1]
        summary.append((name, m["frames"], len(m["marks"]), ok_hash, ok_obj, ok_mem, cmp_txt))
        lines.append("### %s\n" % TITLES.get(name, name))
        lines.append("```\n%s```\n" % cmp_txt)
        lines.append("| Teste (frame) | P1 | P2 | Projéteis criados | Arremesso de P1 | Arremesso de P2 |")
        lines.append("|---|---|---|---|---|---|")
        for r in an:
            p1, p2 = r.get("0"), r.get("1")
            lines.append("| `%s` (%d) | %s | %s | %s | %s | %s |" % (
                r["label"], r["from"], fighter_cell(p1), fighter_cell(p2),
                ", ".join(r.get("projectiles") or []) or "—",
                fmt_kb(p1["knockback"]) if p1 else "—", fmt_kb(p2["knockback"]) if p2 else "—"))
        lines.append("")
    head = ["| Cenário | Frames | Testes | Hash por frame | Traço de objetos | Memória final |",
            "|---|---:|---:|---|---|---|"]
    for name, fr, n, a, b, c, _ in summary:
        head.append("| %s | %d | %d | %s | %s | %s |" % (TITLES.get(name, name).split(" — ")[0], fr, n,
                    "idêntico" if a else "**DIFERENTE**", "idêntico" if b else "**DIFERENTE**",
                    "idêntica" if c else "**DIFERENTE**"))
    open(out, "w").write("\n".join(head) + "\n\n" + "\n".join(lines) + "\n")
    print(out)


if __name__ == "__main__":
    main()
