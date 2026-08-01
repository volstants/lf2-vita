#pragma once
// ─────────────────────────────────────────────────────────────────────────────
//  log.hpp — diagnóstico de falha, sem exceções e sem SDL
//
//  Motivo de existir: até aqui toda falha de inicialização virava silêncio.
//  `loadTex` devolvia nullptr, `drawSprite` fazia `if (!t) return;` e o
//  resultado no device era um sprite invisível — sem pista de qual asset faltou.
//  `SDL_Init`, `SDL_CreateWindow` e `SDL_CreateRenderer` não tinham o retorno
//  verificado, e `SDL_GetError()` não era chamado em lugar nenhum do projeto.
//
//  Este header NÃO inclui SDL de propósito: `dat.hpp` é puro e roda nos testes
//  de host, e precisa poder registrar "arquivo não abriu" do mesmo jeito. Quem
//  tem SDL passa `SDL_GetError()` como argumento string.
//
//  Destino do log:
//    Vita  → ux0:data/lf2vita.log   (sobrevive ao fechamento do app)
//    host  → stderr                 (harness e testes)
// ─────────────────────────────────────────────────────────────────────────────
#include <cstdio>
#include <cstdarg>

namespace lf2 {

// Aberto uma vez em main(); se nunca for aberto, tudo cai em stderr, que é o
// comportamento certo para o host e um fallback aceitável no device.
inline std::FILE*& logFile() { static std::FILE* f = nullptr; return f; }

inline void logOpen(const char* path) {
    if (logFile()) return;
    logFile() = std::fopen(path, "w");     // "w": um log por sessão, não cresce sem fim
}

inline void logClose() {
    if (!logFile()) return;
    std::fclose(logFile());
    logFile() = nullptr;
}

inline void logLine(const char* level, const char* fmt, ...) {
    std::FILE* out = logFile() ? logFile() : stderr;
    std::fprintf(out, "[%s] ", level);
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(out, fmt, ap);
    va_end(ap);
    std::fputc('\n', out);
    std::fflush(out);      // sem isto um crash logo depois leva o log junto
}

} // namespace lf2

// Falha recuperável: registra e segue. Use quando o jogo consegue continuar
// degradado (um sprite que não carregou, um .dat opcional ausente).
#define LF2_WARN(cond, ...)                                                    \
    do { if (!(cond)) {                                                        \
        ::lf2::logLine("AVISO", "%s:%d", __FILE__, __LINE__);                  \
        ::lf2::logLine("AVISO", __VA_ARGS__);                                  \
    } } while (0)

// Falha fatal: registra e devolve `ret`. NÃO aborta — abortar na Vita fecha o
// app sem deixar rastro na tela, e o log já é o rastro. O chamador decide o que
// fazer com o retorno.
#define LF2_CHECK(cond, ret, ...)                                              \
    do { if (!(cond)) {                                                        \
        ::lf2::logLine("ERRO", "%s:%d", __FILE__, __LINE__);                   \
        ::lf2::logLine("ERRO", __VA_ARGS__);                                   \
        return ret;                                                            \
    } } while (0)
