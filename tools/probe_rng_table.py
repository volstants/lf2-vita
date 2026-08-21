#!/usr/bin/env python3
"""Le' a tabela do RNG de um lf2.exe EM EXECUCAO (Windows).

Isto e' o unico jeito de confirmar empiricamente o achado A23: a tabela mora em
BSS, nao existe no arquivo, e a evidencia estatica diz que em single-player ela
permanece zerada. Evidencia estatica completa e' forte, mas leitura de memoria
e' Nivel A' — e o ORACULO.md inteiro existe para preferir a segunda.

  1. Abra o LF2 e entre numa partida single-player (bata em alguem).
  2. python tools/probe_rng_table.py

Saida esperada se A23 estiver certo: 3001 bytes zerados, e cursorA/cursorB
diferentes de zero (provando que o RNG rodou e mesmo assim a tabela ficou zero).
"""
import ctypes, ctypes.wintypes as w, sys

TABLE, CUR_B, CUR_A, N = 0x0044FF90, 0x00450BCC, 0x00450C34, 3001

k = ctypes.WinDLL('kernel32', use_last_error=True)
psapi = ctypes.WinDLL('psapi', use_last_error=True)

def find_pid(name='lf2.exe'):
    arr = (w.DWORD * 4096)(); got = w.DWORD()
    psapi.EnumProcesses(ctypes.byref(arr), ctypes.sizeof(arr), ctypes.byref(got))
    for pid in arr[:got.value // ctypes.sizeof(w.DWORD)]:
        h = k.OpenProcess(0x0410, False, pid)   # QUERY_INFO | VM_READ
        if not h:
            continue
        buf = ctypes.create_unicode_buffer(260)
        if psapi.GetModuleBaseNameW(h, None, buf, 260) and buf.value.lower() == name:
            k.CloseHandle(h)
            return pid
        k.CloseHandle(h)
    return None

def read(h, addr, size):
    buf = (ctypes.c_ubyte * size)(); n = ctypes.c_size_t()
    if not k.ReadProcessMemory(h, ctypes.c_void_p(addr), buf, size, ctypes.byref(n)):
        sys.exit(f"ReadProcessMemory falhou em 0x{addr:08x} (erro {ctypes.get_last_error()})")
    return bytes(buf[:n.value])

pid = find_pid()
if not pid:
    sys.exit("lf2.exe nao esta' rodando. Abra o jogo e entre numa partida.")
h = k.OpenProcess(0x0410, False, pid)
if not h:
    sys.exit("OpenProcess falhou. Rode o PowerShell como administrador.")

# O lf2.exe e' 32-bit sem ASLR relocavel na pratica; se a base mudar, isto
# aparece como leitura de lixo em vez de zeros — por isso os cursores servem
# de sanidade: fora de faixa significa que o endereco esta' errado.
a = int.from_bytes(read(h, CUR_A, 4), 'little', signed=True)
b = int.from_bytes(read(h, CUR_B, 4), 'little', signed=True)
t = read(h, TABLE, N)
k.CloseHandle(h)

print(f"pid {pid}")
print(f"cursorA (0x{CUR_A:08x}) = {a}   {'OK' if 0 <= a < 1234 else 'FORA DE FAIXA - endereco suspeito'}")
print(f"cursorB (0x{CUR_B:08x}) = {b}   {'OK' if 0 <= b < 3000 else 'FORA DE FAIXA - endereco suspeito'}")
nz = [(i, v) for i, v in enumerate(t) if v]
print(f"tabela  (0x{TABLE:08x}) = {N} bytes, {len(nz)} nao-zero")
if not nz:
    print("\n=> A23 CONFIRMADO: tabela zerada com o RNG ja' rodando.")
    print("   engineRand(n) == cursorA % n em single-player.")
else:
    print(f"\n=> A23 REFUTADO: ha' {len(nz)} bytes nao-zero. Primeiros: {nz[:8]}")
    print("   Existe um gerador que a analise estatica nao achou. NAO implemente")
    print("   o caminho simplificado; investigue quem escreveu esses bytes.")
