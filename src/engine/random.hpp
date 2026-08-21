// engine_random — a fonte de aleatoriedade do LF2, reconstruida do assembly.
//
// Evidencia: AUDITORIA_2026-08-20.md, achados A19-A24. Original em
// 0x00417170-0x004171bc (21 instrucoes, lidas por inteiro).
//
//   cursorA  0x00450c34   mod 1234
//   cursorB  0x00450bcc   mod 3000
//   tabela   0x0044ff90   3001 bytes  <- NAO existe no .exe, ver A23
//
// Tres coisas que NAO sao obvias:
//
//  1. O primeiro argumento do original e' MORTO. Depois do `PUSH ESI` ele mora
//     em [ESP+8], e a funcao nunca le' [ESP+8]. Os valores 0xec/0xee/0xf1 que
//     aparecem no pseudocodigo sao etiqueta de sitio de chamada, ignorada.
//     Por isso a assinatura aqui tem UM parametro, nao dois.   (A19)
//
//  2. A tabela mora em BSS e e' GERADA no startup por `FUN_00422ac0`
//     (0x00422ac0, 18 instrucoes):  tabela[i] = rand() % 255 + 1,  i em
//     [0,3000), depois tabela[3000] = 0. Os valores vao de 1 a 255 e nunca
//     zero, porque o buffer e' terminado em nulo e trafega inteiro num
//     `send()` de 3001 bytes no netplay.                        (A25)
//
//  3. `rand()` e' semeado com `srand(timeGetTime())` em 0x0043cf40. Logo a
//     tabela e' DIFERENTE A CADA EXECUCAO, e a sequencia do engine tambem.
//     O original nao e' reproduzivel entre execucoes — nem para ele mesmo.
//     Para traco diferencial (ORACULO.md), capture a tabela do processo vivo
//     com `tools/probe_rng_table.py` e injete com `loadTable()`.  (A26)
//
//  O laco de copia do netplay conta ate' 0xbb9 = 3001 (0x0043e3f0), 3000 bytes
//  uteis + terminador. O buffer aqui tem 3001 pelo mesmo motivo.
#pragma once
#include <cstdint>
#include <cstddef>

namespace lf2 {

class EngineRandom {
public:
    static constexpr int  kModA       = 1234;   // 0x4d2
    static constexpr int  kModB       = 3000;   // 0xbb8
    static constexpr std::size_t kTableBytes = 3001;  // 0xbb9, ver nota 3

    // Traducao instrucao a instrucao de 0x00417170.
    int next(int n) {
        if (n < 1) return 0;                       // 00417175 TEST/JG -> XOR EAX,EAX
        cursorA_ = (cursorA_ + 1) % kModA;         // 0041717d..0041718b
        cursorB_ = (cursorB_ + 1) % kModB;         // 0041718d..0041719e
        // 004171a7 MOVZX (byte, zero-extend) + ADD + IDIV. A soma vale no
        // maximo 255 + 1233 = 1488, sempre positiva, entao o resto com sinal
        // do IDIV original e o resto sem sinal coincidem.
        const int v = static_cast<int>(table_[cursorB_]) + cursorA_;
        return v % n;
    }

    // 0x00422ac0 — o gerador do startup, instrucao a instrucao.
    // `rnd` recebe a fonte (no original, o `rand()` da libc semeado por
    // timeGetTime). Chamar UMA vez no boot, antes de qualquer sorteio.
    template <typename RandFn>
    void fillFromRand(RandFn&& rnd) {
        for (int i = 0; i < kModB; ++i) {                    // 00422ae0 CMP 0xbb8
            const int r = static_cast<int>(rnd()) % 255;     // 00422ad8 IDIV 0xff
            table_[i] = static_cast<std::uint8_t>(r + 1);    // 00422add ADD DL,1
        }
        table_[kModB] = 0;                                   // 00422aef terminador
    }

    // Injeta uma tabela pronta. Dois usos: (a) netplay, onde o host manda os
    // 3001 bytes e o cliente recebe (send 0x0040304a / recv 0x00428679);
    // (b) traco diferencial, injetando a tabela capturada do lf2.exe vivo —
    // sem isso os dois lados sorteiam diferente e o differ so' produz ruido.
    void loadTable(const std::uint8_t* src) {
        for (std::size_t i = 0; i < kTableBytes; ++i) table_[i] = src[i];
    }

    // 0x0043e3cb restaura o cursorB junto com a tabela, a partir do mesmo blob.
    void setCursors(int a, int b) { cursorA_ = a; cursorB_ = b; }
    int  cursorA() const { return cursorA_; }
    int  tableByte(std::size_t i) const { return table_[i]; }
    int  cursorB() const { return cursorB_; }

    // Canario do traco diferencial (ORACULO.md secao 2): cursor divergente
    // ANTES de campo de fisica divergente = sitio de decisao faltando no porte,
    // nao formula errada.
    void reset() { cursorA_ = 0; cursorB_ = 0; }

private:
    int cursorA_ = 0;   // BSS = 0 no original
    int cursorB_ = 0;
    std::uint8_t table_[kTableBytes] = {};   // BSS = 0 no original
};

// Instancia global: o original tem UMA, em enderecos fixos. Um RNG por objeto
// dessincronizaria tudo — os 264 sitios compartilham os mesmos dois cursores.
inline EngineRandom& engineRandom() {
    static EngineRandom g;
    return g;
}

inline int engineRand(int n) { return engineRandom().next(n); }

} // namespace lf2
