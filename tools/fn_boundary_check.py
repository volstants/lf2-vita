#!/usr/bin/env python3
"""Valida as fronteiras de funcao que o ghidra_export_c.py produziu.

Uso:  python3 tools/fn_boundary_check.py reference/decomp/lf2.exe \\
                                          reference/decomp/lf2_decomp.c

Problema que isto resolve
-------------------------
O modo "orphan code" do `ghidra_export_c.py` promove a FUNCAO qualquer endereco
que pareca codigo alcancavel. Ja custou duas categorias de lixo no `lf2_decomp.c`:

1. **Stubs `int3`** — 1573 deles, ja conhecidos.
2. **Padding de hot-patch do MSVC** — descoberto em 2026-08-12 em `0x0040d6e7`:

       40d6e5:  jmp  0x40d6f0            ; o fluxo real salta POR CIMA
       40d6e7:  lea  0x0(%esp),%esp      ; 7 bytes de NOP
       40d6ee:  mov  %edi,%edi           ; 2 bytes de NOP
       40d6f0:  <codigo real>

O sintoma no decomp e' uma cascata de `unaff_EBP`/`unaff_ESI`/`in_stack_0000...`
logo no inicio da funcao — que e' facil de confundir com ruido de FPO. Nao e':
e' o Ghidra cravando o boundary no lugar errado, e ler o pseudocodigo dali leva
a conclusoes sobre codigo que nunca executa como entrada.

A ideia veio de ferramentas que detectam fronteira por prologo (Capstone). Aqui
usamos `objdump`, que ja esta instalado e ja e' a nossa camada de Nivel A — sem
dependencia nova, sem chave de API.

Criterios de suspeita
---------------------
- primeira instrucao e' `int3`                        → stub
- primeiras instrucoes sao NOP de alinhamento         → padding
  (`lea 0x0(%esp),%esp`, `mov %edi,%edi`, `nop`, `xchg %ax,%ax`)
- existe um `jmp` imediatamente antes que salta POR CIMA do endereco → padding
- primeira instrucao nao e' um prologo plausivel      → aviso (nao veredito)

Prologos plausiveis em MSVC x86: `push %ebp`, `mov %edi,%edi` (hot-patch NO
inicio de funcao real — por isso o teste do `jmp` importa), `sub $N,%esp`,
`push %esi/%edi/%ebx`, `mov %ecx,...` (thiscall), `xor`, `cmp`, `test`, `lea`.
FPO deixa muita funcao real sem `push %ebp`, entao "prologo atipico" e' AVISO,
nunca veredito — o veredito so' sai de `int3`, padding ou salto por cima.
"""
import re, subprocess, sys, collections

NOP_PADDING = (
    "lea    0x0(%esp),%esp",
    "lea    0x0(%edi),%edi",
    "lea    0x0(%esi),%esi",
    "mov    %edi,%edi",
    "nop",
    "xchg   %ax,%ax",
    "data16",
)
PROLOGO_PLAUSIVEL = ("push", "sub", "mov", "xor", "cmp", "test", "lea", "and", "or", "call", "jmp")

def disasm(exe, start, stop):
    """Desassembla [start, stop). Devolve [(addr, bytes_hex, texto)]."""
    out = subprocess.run(
        ["objdump", "-d", f"--start-address={hex(start)}", f"--stop-address={hex(stop)}", exe],
        capture_output=True, text=True).stdout
    linhas = []
    for l in out.splitlines():
        m = re.match(r"\s*([0-9a-f]+):\t([0-9a-f ]+)\t(.*)", l)
        if m:
            linhas.append((int(m.group(1), 16), m.group(2).strip(), m.group(3).strip()))
    return linhas

def main():
    if len(sys.argv) < 3:
        print(__doc__); return 1
    exe, decomp = sys.argv[1], sys.argv[2]

    addrs = sorted({int(m, 16) for m in
                    re.findall(r"^/\* ---- FUN_([0-9a-f]{8}) @", open(decomp, errors="replace").read(),
                               re.M)})
    if not addrs:
        print("nenhum FUN_ encontrado no decomp"); return 1
    print(f"{len(addrs)} funcoes declaradas no decomp\n")

    veredito = collections.Counter()
    suspeitos = []

    # Uma unica passada de disassembly linear e' muito mais rapida que N chamadas.
    # Desassembla a secao inteira uma vez e indexa por endereco.
    print("desassemblando .text uma vez…", flush=True)
    todas = disasm(exe, addrs[0] - 16, addrs[-1] + 64)
    por_addr = {a: (b, t) for a, b, t in todas}
    ordem    = [a for a, _, _ in todas]
    idx      = {a: i for i, a in enumerate(ordem)}
    print(f"{len(todas)} instrucoes indexadas\n")

    for a in addrs:
        if a not in idx:
            veredito["fora do intervalo"] += 1; continue
        i = idx[a]
        txt0 = por_addr[a][1]
        seq  = [por_addr[ordem[j]][1] for j in range(i, min(i + 3, len(ordem)))]

        motivo = None
        if txt0.startswith("int3"):
            motivo = "stub int3"
        elif any(txt0.startswith(p) for p in NOP_PADDING):
            motivo = "padding de alinhamento"
        else:
            # jmp imediatamente antes saltando POR CIMA deste endereco?
            for j in range(max(0, i - 3), i):
                pa, (_, pt) = ordem[j], por_addr[ordem[j]]
                m = re.match(r"jmp\s+0x([0-9a-f]+)", pt)
                if m and int(m.group(1), 16) > a:
                    motivo = f"salto por cima: {pa:#x} jmp {m.group(1)}"
                    break

        if motivo:
            veredito["LIXO"] += 1
            suspeitos.append((a, motivo, seq[0]))
        elif not any(txt0.startswith(p) for p in PROLOGO_PLAUSIVEL):
            veredito["prologo atipico (aviso)"] += 1
            suspeitos.append((a, "AVISO prologo atipico", seq[0]))
        else:
            veredito["ok"] += 1

    print("=== veredito ===")
    for k, v in veredito.most_common():
        print(f"  {k:<28} {v}")

    lixo = [s for s in suspeitos if not s[1].startswith("AVISO")]
    print(f"\n=== primeiros 25 de {len(lixo)} enderecos que NAO sao entrada de funcao ===")
    for a, motivo, txt in lixo[:25]:
        print(f"  FUN_{a:08x}  {motivo:<34} | {txt}")

    avisos = [s for s in suspeitos if s[1].startswith("AVISO")]
    if avisos:
        print(f"\n=== primeiros 10 de {len(avisos)} avisos (podem ser reais, FPO) ===")
        for a, _, txt in avisos[:10]:
            print(f"  FUN_{a:08x}  | {txt}")

    print("\nUse esta lista para NAO gastar tempo lendo pseudocodigo desses enderecos.")
    return 0

if __name__ == "__main__":
    sys.exit(main())
