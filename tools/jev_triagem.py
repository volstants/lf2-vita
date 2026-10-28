#!/usr/bin/env python3
"""
jev_triagem.py - triagem de diff com o Jev (TypeSafe AI) ANTES da revisao adversarial.

O Jev nao le assembly, nao faz conta e nao escreve explicacao. Ele escolhe entre
opcoes e da probabilidade. Entao aqui ele NAO decide se o codigo esta certo:
ele so decide ONDE o revisor com o binario deve gastar tempo.

Pipeline por hunk:
  1. checagens deterministicas (regex): cita endereco? mexe em literal numerico?
     remove chamada? - isso regex faz melhor e de graca que qualquer modelo.
  2. Jev: tipo da mudanca, se muda ordem dentro do tick, se remove
     comportamento, se a justificativa vem de F.LF/comunidade, se enfraquece
     teste, risco de divergir do original.
  3. politica em codigo (limiares abaixo) -> REVISAR-A / CONFERIR / LEVE / HUMANO.

Uso:
  python jev_triagem.py                             # diff da arvore de trabalho
  git diff HEAD~3 | python jev_triagem.py --stdin
  python jev_triagem.py --rev HEAD~1..HEAD --out TRIAGEM.md
  python jev_triagem.py --dry-run                   # sem chave: so regex + payload

So stdlib. Chave em TYPESAFE_API_KEY (console.typesafe.ai/settings/keys).
"""
import argparse, json, os, re, subprocess, sys, time, urllib.request, urllib.error
from concurrent.futures import ThreadPoolExecutor

API = "https://api.typesafe.ai/v1/systemone"
MODEL = "jev-latest"
MAX_HUNK_CHARS = 6000          # estado menor = Jev mais preciso
PATHS = ["src", "tests", "Makefile.host", "CMakeLists.txt"]

# ---------------------------------------------------------------- perguntas
# Literais e diretas: "o modelo le suas palavras, nao sua intencao".
QUESTIONS = {
    "tipo": {
        "type": "choice",
        "instructions": "What does this diff hunk of a C++ Little Fighter 2 engine port mainly change?",
        "criteria": {
            "mecanica": "Game behavior: physics, damage, frames, timers, AI, randomness, input handling, order of per-tick updates",
            "porte": "Platform adaptation with no counterpart in the original game: SDL, Vita, clocks, file paths, logging, build",
            "refactor": "Same behavior, code moved, renamed or reorganized",
            "comentario": "Only comments or documentation text",
            "teste": "Test code or assertions",
        },
    },
    "ordem_tick": {
        "type": "noul",
        "instructions": "Does this hunk move an update (a decrement, a frame advance, a velocity commit) to a different point in the per-tick order, such as before or after collisions?",
    },
    "remove_comportamento": {
        "type": "noul",
        "instructions": "Does this hunk delete a line that performed an action (lines starting with '-' that call a function or change a variable) without adding an equivalent line that does the same thing?",
    },
    "fonte_fraca": {
        "type": "noul",
        "instructions": "Do the added comments justify a game value or rule by citing F.LF, OpenLF2, community docs, a guess, or nothing at all, instead of a hexadecimal address in lf2.exe?",
    },
    "enfraquece_teste": {
        "type": "noul",
        "instructions": "Does this hunk delete an assertion, loosen a tolerance, or replace an exact expected value with a weaker check?",
    },
    "risco": {
        "type": "score",
        "instructions": "If this hunk were wrong, how far would the port's gameplay drift from the original lf2.exe?",
        "criteria": [
            "No gameplay effect",
            "Rare edge case",
            "Noticeable in some fights or characters",
            "Changes core combat for every character",
        ],
    },
}

# ---------------------------------------------------------------- politica
LIM_SIM = 0.70        # noul acima disso conta como "sim"
LIM_CONF_HUMANO = 0.50  # choice com confianca abaixo disso -> humano decide
LIM_RISCO = 1.8       # score 0..3

ADDR = re.compile(r"\b(?:0x)?00?4[0-5][0-9a-fA-F]{4}\b|\bFUN_[0-9a-fA-F]{8}\b|\b[0-4][0-9a-f]{5}\b")
NUM_LIT = re.compile(r"(?<![\w.])-?\d+(?:\.\d+)?f?(?![\w.])")
CALL = re.compile(r"\b\w+(?:\.\w+|->\w+)*\s*\(")


def run_git(args, cwd):
    return subprocess.run(["git"] + args, cwd=cwd, capture_output=True, text=True,
                          encoding="utf-8", errors="replace").stdout


def split_hunks(diff):
    hunks, cur_file = [], None
    for block in re.split(r"(?m)^(?=diff --git )", diff):
        m = re.search(r"(?m)^\+\+\+ b/(.+)$", block)
        if not m:
            continue
        cur_file = m.group(1)
        parts = re.split(r"(?m)^(?=@@ )", block)
        for p in parts[1:]:
            hunks.append({"arquivo": cur_file, "cabecalho": p.splitlines()[0], "texto": p})
    return hunks


def deterministico(h):
    add = [l[1:] for l in h["texto"].splitlines()[1:] if l.startswith("+")]
    rem = [l[1:] for l in h["texto"].splitlines()[1:] if l.startswith("-")]
    code = lambda ls: [l for l in ls if not l.strip().startswith("//")]
    num_add = {n for l in code(add) for n in NUM_LIT.findall(l.split("//")[0])}
    num_rem = {n for l in code(rem) for n in NUM_LIT.findall(l.split("//")[0])}
    norm = lambda l: re.sub(r"\s+", "", l.split("//")[0])
    add_norm = {norm(l) for l in code(add)}
    # chamada removida que NAO reaparece no mesmo hunk (mover dentro do hunk nao conta)
    calls_rem = [l.strip() for l in code(rem) if CALL.search(l) and norm(l) not in add_norm]
    build = not h["arquivo"].endswith((".cpp", ".hpp", ".h", ".c"))
    return {
        "cita_endereco": bool(ADDR.search("\n".join(add))),
        "literais_novos": [] if build else sorted(num_add - num_rem - {"0", "1", "-1", "0.f", "1.f"}),
        "chamadas_removidas": len(calls_rem),
        "linhas": (len(add), len(rem)),
    }


def jev(state, key, tentativas=4):
    body = json.dumps({"model": MODEL, "state": state, "questions": QUESTIONS}).encode()
    for i in range(tentativas):
        req = urllib.request.Request(API, data=body, method="POST", headers={
            "Authorization": f"Bearer {key}", "Content-Type": "application/json"})
        try:
            with urllib.request.urlopen(req, timeout=30) as r:
                return json.load(r)
        except urllib.error.HTTPError as e:
            if e.code in (429, 529) and i < tentativas - 1:
                time.sleep(2 ** i)
                continue
            raise RuntimeError(f"HTTP {e.code}: {e.read()[:300]!r}")
    raise RuntimeError("sem resposta")


def decidir(h):
    d, a = h["det"], h.get("jev")
    if a is None:  # dry-run: so regex
        motivos = []
        if d["literais_novos"] and not d["cita_endereco"]:
            motivos.append(f"literal novo sem endereco: {', '.join(d['literais_novos'][:6])}")
        if d["chamadas_removidas"]:
            motivos.append(f"{d['chamadas_removidas']} chamada(s) removida(s)")
        if h["arquivo"].startswith("src/") and d["linhas"][0] >= 20:
            motivos.append("hunk grande em src/")
        return ("CONFERIR" if motivos else "LEVE"), motivos
    ans = a["answers"]
    tipo = ans["tipo"]["choice"]
    conf = ans["tipo"].get("confidence", 1.0)
    sim = lambda k: ans[k]["noul"] >= LIM_SIM
    risco = ans["risco"]["score"]
    motivos = []
    if sim("ordem_tick"): motivos.append("muda ordem dentro do tick")
    if sim("remove_comportamento") or d["chamadas_removidas"]:
        motivos.append("remove comportamento")
    if sim("fonte_fraca") or (d["literais_novos"] and not d["cita_endereco"]):
        motivos.append("valor/regra sem endereco no binario")
    if sim("enfraquece_teste"): motivos.append("enfraquece teste")
    if risco >= LIM_RISCO: motivos.append(f"risco {risco:.1f}/3")
    if conf < LIM_CONF_HUMANO:
        return "HUMANO", [f"Jev inseguro sobre o tipo ({tipo}, conf {conf:.2f})"] + motivos
    if tipo == "mecanica" and motivos:
        return "REVISAR-A", motivos
    if tipo == "teste" and sim("enfraquece_teste"):
        return "REVISAR-A", motivos
    if motivos:
        return "CONFERIR", motivos
    return "LEVE", []


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rev", help="intervalo git, ex. HEAD~1..HEAD (padrao: arvore de trabalho)")
    ap.add_argument("--repo", default=".")
    ap.add_argument("--out", help="grava o relatorio em markdown aqui (padrao: stdout)")
    ap.add_argument("--dry-run", action="store_true", help="nao chama a API")
    ap.add_argument("--stdin", action="store_true", help="le o diff da entrada padrao")
    ap.add_argument("--workers", type=int, default=8)
    args = ap.parse_args()

    if args.stdin:
        diff = sys.stdin.read()
    else:
        diff = run_git(["diff", "-U3", "--ignore-cr-at-eol"] + ([args.rev] if args.rev else []) + ["--"] + PATHS, args.repo)
    hunks = split_hunks(diff)
    if not hunks:
        print("diff vazio"); return
    for h in hunks:
        h["det"] = deterministico(h)

    key = os.environ.get("TYPESAFE_API_KEY")
    dry = args.dry_run or not key
    tokens = 0
    if not dry:
        def f(h):
            state = {"arquivo": h["arquivo"], "contexto": h["cabecalho"],
                     "diff": h["texto"][:MAX_HUNK_CHARS]}
            try:
                h["jev"] = jev(state, key)
            except Exception as e:
                h["erro"] = str(e)
        t0 = time.time()
        with ThreadPoolExecutor(args.workers) as ex:
            list(ex.map(f, hunks))
        dt = time.time() - t0
        tokens = sum(h["jev"]["usage"]["input_tokens"] for h in hunks if "jev" in h)

    ordem = {"REVISAR-A": 0, "HUMANO": 1, "CONFERIR": 2, "LEVE": 3, "ERRO": 4}
    for h in hunks:
        if "erro" in h:
            h["rota"], h["motivos"] = "ERRO", [h["erro"]]
        else:
            h["rota"], h["motivos"] = decidir(h)
    hunks.sort(key=lambda h: (ordem[h["rota"]], h["arquivo"]))

    L = ["# Triagem Jev do diff", ""]
    if dry:
        L.append("> **Dry-run** (sem `TYPESAFE_API_KEY` ou `--dry-run`): so as checagens por regex.")
    else:
        L.append(f"> {len(hunks)} hunks, {tokens} tokens de entrada "
                 f"(~US$ {tokens * 0.042 / 1e6:.5f}), {dt:.1f} s.")
    L += ["> Triagem NAO e' veredito. `REVISAR-A` = mandar ao revisor com o binario "
          "(REVIEW_PROMPT.md). `LEVE` = leitura rapida, nao 'aprovado'.", ""]
    cont = {r: sum(h["rota"] == r for h in hunks) for r in ordem}
    L.append(" | ".join(f"**{r}** {n}" for r, n in cont.items() if n))
    L.append("")
    L.append("| rota | arquivo | hunk | +/- | motivos | Jev |")
    L.append("|---|---|---|---|---|---|")
    for h in hunks:
        m = re.match(r"@@ -\d+(?:,\d+)? \+(\d+)", h["cabecalho"])
        linha = m.group(1) if m else "?"
        jv = ""
        if "jev" in h:
            a = h["jev"]["answers"]
            jv = (f"{a['tipo']['choice']} ({a['tipo'].get('confidence', 0):.2f}), "
                  f"risco {a['risco']['score']:.1f}")
        ctx = h["cabecalho"].split("@@")[-1].strip()[:40].replace("|", "\\|")
        L.append(f"| {h['rota']} | `{h['arquivo']}:{linha}` | {ctx} | "
                 f"+{h['det']['linhas'][0]}/-{h['det']['linhas'][1]} | "
                 f"{'; '.join(h['motivos']).replace('|', '/')} | {jv} |")
    txt = "\n".join(L) + "\n"
    if args.out:
        open(args.out, "w", encoding="utf-8").write(txt)
        print(f"relatorio em {args.out}")
    else:
        sys.stdout.write(txt)


if __name__ == "__main__":
    main()
