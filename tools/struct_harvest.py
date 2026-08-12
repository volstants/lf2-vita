#!/usr/bin/env python3
"""Inventario de campos de struct a partir dos deslocamentos usados no `.text`.

Uso:  python3 tools/struct_harvest.py reference/decomp/lf2.exe
      python3 tools/struct_harvest.py reference/decomp/lf2.exe 0x42e100 0x430200

Problema que isto resolve
-------------------------
Ate' agora os campos do `object_t` foram descobertos de forma REATIVA: um bug
aparece, vamos ao assembly, achamos `+0xB8`. Foi assim com `fall`, `bdefend` e
`shaking`, e por isso os tres passaram sessoes inteiras sem existir no porte.

Este script inverte a ordem. Ele varre um intervalo do `.text`, colhe todo
operando de memoria da forma `disp(%reg)` com `reg` != `esp`/`ebp`, e monta um
inventario: deslocamento, quantas leituras, quantas escritas, largura de acesso
provavel, registradores-base observados e um exemplo de instrucao.

O resultado nao e' um mapa de struct. E' uma LISTA DE CANDIDATOS ordenada, que
diz onde vale gastar tempo de leitura.

A ideia veio do `struct_recovery.rs` do `rdecomp`. A ferramenta em si nao foi
adotada (ver `BINARY_NOTES.md`); a ideia deu 90 linhas em cima do `objdump` que
ja' era a nossa camada de Nivel A.

LIMITE IMPORTANTE — leia antes de usar o resultado
--------------------------------------------------
**O script nao sabe para qual struct o registrador-base aponta.** No bloco de
aplicacao de acerto, `%edx` aponta ora para o `object_t`, ora para o `itr`
(passo 0x50), ora para o `frame`. Entao `0x1c` aparece na lista tanto podendo
ser `object_t+0x1C` quanto `itr->fall` — e neste caso e' `itr->fall`.

Consequencia pratica: **todo item da saida e' Nivel D ate' alguem abrir o
disassembly e confirmar quem e' o base.** O script economiza a busca, nao a
prova. Usar a saida como se fosse mapa de campos seria repetir exatamente o
vicio que `AUDITORIA_SUPERFICIE.md` documenta.

COLISAO CONHECIDA — object_t x struct do .dat de personagem (2026-08-12, A19)
---------------------------------------------------------------------------
Os dois guardam `double` na mesma faixa de offset. A rotina de dump em
0x0040d185-0x0040d22a imprime o struct do .dat com o nome literal de cada campo:

    +0x20 running_speed        +0x38 heavy_walking_speedz
    +0x28 running_speedz       +0x40 heavy_running_speed
    +0x30 heavy_walking_speed  +0x48 heavy_running_speedz

No object_t esses mesmos offsets sao:

    +0x28/+0x30/+0x38  acumulador de empurrao x/y/z  (A16)
    +0x40/+0x48/+0x50  velocidade efetiva x/y/z

Entao `fldl 0x30(%ecx)` pode ser qualquer um dos dois. As leituras em 0x00413838
e 0x004138c6 sao do .dat; as de 0x0042f1cd e 0x0042f212 sao do objeto. O script
nao distingue, e nunca vai distinguir.

O QUE FECHOU O CASO EM A16, E QUE ESTE SCRIPT NAO FAZ
-----------------------------------------------------
A varredura ampla mostrou +0x30 como um candidato qualquer no meio da lista. O
que provou a semantica foi o INVENTARIO EXAUSTIVO de um unico offset: as 10
instrucoes do .text inteiro que tocam 0x30(%reg) com base != esp/ebp, escritas
e leituras, todas mapeadas. A pergunta que rendeu foi "quem LE este campo?" —
o bloco de tombo escrevia num campo que nada lia, e foi isso que obrigou a
procurar o passo de commit.

Modo sugerido e nao implementado: `--offset 0x30` sobre o .text inteiro.
"""
import re, subprocess, sys, collections

REG_BYTE = {'al', 'bl', 'cl', 'dl', 'ah', 'bh', 'ch', 'dh'}
REG_WORD = {'ax', 'bx', 'cx', 'dx', 'si', 'di', 'sp', 'bp'}

# Intervalos ja' mapeados, usados como default. Nomes em ROTINAS sao Nivel A.
ROTINAS = [
    (0x42e100, 0x430200, 'FUN_0042e100  aplicacao de acerto'),
    (0x40d940, 0x40e900, 'update por tick / laco de decaimento'),
    (0x417400, 0x417600, 'FUN_00417400  does_attack_success'),
]

# Campos ja' provados. Ficam de fora da lista de candidatos.
CONHECIDOS = {
    0x10: 'x', 0x14: 'y (negativo = no ar)', 0x18: 'z', 0x20: 'attacks',
    0x28: 'x_velocity', 0x40: 'y_velocity', 0x48: 'vy', 0x50: 'z_velocity',
    0x58: 'x_position', 0x60: 'y_position', 0x68: 'z_position',
    0x70: 'frame_id1', 0x78: 'frame_id3', 0x7c: 'frame_id4',
    0x80: 'facing', 0x88: 'frame_wait',
    0xb0: 'fall', 0xb4: 'shaking', 0xb8: 'bdefend',
    0xea: '(byte; NAO e o cronometro de queimadura)',
    0xec: 'arest', 0xf0: 'vrest_of_objects[400]',
    0x2fc: 'hp', 0x31c: 'drink_hp', 0x368: 'file',
    0x6f4: 'file->id', 0x6f8: 'file->type', 0x7a4: 'file->frame[0]',
    0x7ac: 'frame->state',
}


def largura(mn, ops):
    """Largura provavel do acesso, em bytes. 0 = nao determinada."""
    if mn.startswith(('fldl', 'fstpl', 'fstl', 'fcompl', 'fcoml',
                      'faddl', 'fsubl', 'fmull', 'fdivl', 'fsubrl', 'fdivrl')):
        return 8
    if mn.startswith(('flds', 'fstps', 'fsts', 'fadds', 'fsubs', 'fmuls',
                      'fdivs', 'fcomps')):
        return 4
    if mn.startswith(('fildll', 'fistpll')):
        return 8
    if mn.startswith(('fildl', 'fistpl', 'fisttpl')):
        return 4
    if mn.startswith(('movsd', 'movq', 'movlpd', 'movhpd')):
        return 8
    if mn.startswith(('movss', 'movd')):
        return 4
    if mn.startswith(('movzbl', 'movsbl')):
        return 1
    if mn.startswith(('movzwl', 'movswl')):
        return 2
    for r in re.findall(r'%([a-z]{2,3})\b', ops):
        if r in REG_BYTE:
            return 1
        if r in REG_WORD:
            return 2
        if r.startswith('e'):
            return 4
    if mn.endswith('b'):
        return 1
    if mn.endswith('w'):
        return 2
    if mn.endswith('l'):
        return 4
    return 0


# Mnemonicos que leem a memoria mesmo quando ela e' o ultimo operando.
SO_LEEM = ('cmp', 'test', 'fld', 'fcom', 'fadd', 'fsub', 'fmul', 'fdiv',
           'push', 'fild', 'lea')


def colher(exe, lo, hi, acc):
    out = subprocess.run(
        ['objdump', '-d', f'--start-address={hex(lo)}',
         f'--stop-address={hex(hi)}', exe],
        capture_output=True, text=True).stdout
    for l in out.splitlines():
        m = re.match(r'\s*([0-9a-f]+):\t[0-9a-f ]+\t(\S+)\s*(.*)', l)
        if not m:
            continue
        addr, mn, ops = int(m.group(1), 16), m.group(2), m.group(3)
        for mm in re.finditer(
                r'(-?0x[0-9a-f]+)\(%(e[a-z]{2})(?:,%e[a-z]{2},\d)?\)', ops):
            disp, base = int(mm.group(1), 16), mm.group(2)
            if base in ('esp', 'ebp'):      # pilha, nao struct
                continue
            if not 0 <= disp <= 0x2000:
                continue
            c = acc[disp]
            alvo = ops.rsplit(',', 1)[-1].strip()
            escrita = mm.group(0) in alvo and not mn.startswith(SO_LEEM)
            c['w' if escrita else 'r'] += 1
            c['larg'][largura(mn, ops)] += 1
            c['base'][base] += 1
            if c['ex'] is None:
                c['ex'] = f'{addr:#08x}  {mn} {ops}'


def novo():
    return {'r': 0, 'w': 0, 'larg': collections.Counter(),
            'base': collections.Counter(), 'ex': None}


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    exe = sys.argv[1]
    if len(sys.argv) >= 4:
        faixas = [(int(sys.argv[2], 16), int(sys.argv[3], 16), 'intervalo dado')]
    else:
        faixas = ROTINAS

    acc = collections.defaultdict(novo)
    for lo, hi, nome in faixas:
        print(f'varrendo {lo:#x}-{hi:#x}  {nome}')
        colher(exe, lo, hi, acc)

    cand = [k for k in sorted(acc) if k not in CONHECIDOS]
    print(f'\ndeslocamentos distintos: {len(acc)}   '
          f'ja provados: {len(acc) - len(cand)}   candidatos: {len(cand)}\n')

    # Ordena por volume de acesso: onde o engine mais toca e' onde mais importa.
    cand.sort(key=lambda k: -(acc[k]['r'] + acc[k]['w']))

    print(f"{'off':>7} {'R':>4} {'W':>4} {'larg':>4}  {'base':<16} exemplo")
    print('-' * 96)
    for k in cand:
        v = acc[k]
        w = v['larg'].most_common(1)[0][0]
        bases = ','.join(b for b, _ in v['base'].most_common(3))
        print(f'{k:#7x} {v["r"]:4d} {v["w"]:4d} {w:4d}  {bases:<16} {v["ex"]}')

    print('\nCada linha e um CANDIDATO de Nivel D. O registrador-base pode')
    print('apontar para object_t, itr, bdy ou frame — o script nao distingue.')
    print('Confirmar no disassembly antes de escrever qualquer coisa no porte.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
