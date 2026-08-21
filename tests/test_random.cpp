// test_random — engine_random contra o assembly de 0x00417170.
//
//   g++ -std=c++17 -Wall -Wextra -I src tests/test_random.cpp -o bin/test_random
#include "engine/random.hpp"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

static int g_fail = 0, g_ok = 0;
#define CHECK(cond) do { \
    if (cond) { ++g_ok; } \
    else { ++g_fail; std::printf("FALHOU %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

int main() {
    using lf2::EngineRandom;

    // --- guarda de 00417175: n < 1 retorna 0 E NAO avanca os cursores -------
    {
        EngineRandom r;
        CHECK(r.next(0) == 0);
        CHECK(r.next(-7) == 0);
        CHECK(r.cursorA() == 0);   // o RET precoce e' antes de tocar cursor
        CHECK(r.cursorB() == 0);
    }

    // --- gerador 0x00422ac0: faixa 1..255, nunca zero, terminador zero ----
    {
        EngineRandom r;
        unsigned seed = 12345;
        r.fillFromRand([&] { seed = seed * 1103515245u + 12345u; return (seed >> 16) & 0x7fff; });
        int zeros = 0;
        for (int i = 0; i < EngineRandom::kModB; ++i) {
            const int v = r.tableByte(i);
            CHECK(v >= 1 && v <= 255);
            if (v == 0) ++zeros;
        }
        CHECK(zeros == 0);                                    // nenhum zero interior
        CHECK(r.tableByte(EngineRandom::kModB) == 0);         // terminador
    }

    // --- tabela zerada: next(n) == cursorA % n (so' antes de fillFromRand) --
    {
        EngineRandom r;
        for (int i = 1; i <= 20; ++i) {
            const int got = r.next(6);
            CHECK(got == i % 6);           // cursorA vale i na i-esima chamada
            CHECK(r.cursorA() == i);
            CHECK(r.cursorB() == i);
        }
    }

    // --- envoltura dos cursores: 1234 e 3000, independentes ----------------
    {
        EngineRandom r;
        for (int i = 0; i < 1234; ++i) r.next(2);
        CHECK(r.cursorA() == 0);           // 1234 chamadas -> volta a zero
        CHECK(r.cursorB() == 1234);
        for (int i = 0; i < 3000 - 1234; ++i) r.next(2);
        CHECK(r.cursorB() == 0);
        CHECK(r.cursorA() == (3000 % 1234));
    }

    // --- faixa: sempre [0, n) ---------------------------------------------
    {
        EngineRandom r;
        std::uint8_t t[EngineRandom::kTableBytes];
        for (std::size_t i = 0; i < EngineRandom::kTableBytes; ++i)
            t[i] = static_cast<std::uint8_t>((i * 37) & 0xff);
        r.loadTable(t);
        for (int i = 0; i < 20000; ++i) {
            for (int n : {1, 2, 6, 16, 300}) {
                const int v = r.next(n);
                CHECK(v >= 0 && v < n);
            }
        }
    }

    // --- soma nunca estoura: max 255 + 1233 = 1488 -------------------------
    {
        EngineRandom r;
        std::uint8_t t[EngineRandom::kTableBytes];
        for (auto& b : t) b = 255;
        r.loadTable(t);
        r.setCursors(1233, 0);
        const int v = r.next(10000);
        CHECK(v == (255 + ((1233 + 1) % 1234)) % 10000);   // cursorA envolve p/ 0
        CHECK(v == 255);
    }

    std::printf("%d CHECKs ok, %d falhas\n", g_ok, g_fail);
    return g_fail ? 1 : 0;
}
