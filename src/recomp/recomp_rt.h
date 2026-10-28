/*
 * recomp_rt.h — runtime contract of the statically recompiled lf2.exe.
 *
 * The generated translation units (build/recomp/lf2_gen_*.c) include only
 * this header. Guest state is a flat 32-bit address space (M) holding the PE
 * image at its preferred base 0x400000, the guest stack and the guest heap,
 * plus the register file R. Nothing here depends on the host OS.
 */
#ifndef LF2_RECOMP_RT_H
#define LF2_RECOMP_RT_H

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi;
    double   st[8];      /* x87 stack, 53-bit precision control (Windows default) */
    int      top;        /* index of ST(0) in st[] */
    uint16_t fsw_cc;     /* C0/C1/C2/C3 bits as in the status word */
    uint16_t fcw;        /* control word reported by fnstcw (0x027F) */
    uint32_t fs_base;    /* guest address of the TIB (fs:[0] = SEH chain) */
    uint32_t eflags_extra;
} rc_cpu;

extern rc_cpu R;
extern uint8_t* M;

/* ── guest memory ──────────────────────────────────────────────────────── */
static inline uint32_t RD32(uint32_t a) { uint32_t v; memcpy(&v, M + a, 4); return v; }
static inline uint32_t RD16(uint32_t a) { uint16_t v; memcpy(&v, M + a, 2); return v; }
static inline uint32_t RD8(uint32_t a)  { return M[a]; }
static inline uint64_t RD64(uint32_t a) { uint64_t v; memcpy(&v, M + a, 8); return v; }
#ifdef RC_WATCH
/* debug builds: report writes that touch [rc_watch_lo, rc_watch_hi) */
extern uint32_t rc_watch_lo, rc_watch_hi;
void rc_watch_hit(uint32_t a, uint32_t n, const void* v);
#define RC_W(a, n, p) do { if ((a) < rc_watch_hi && (a) + (n) > rc_watch_lo) rc_watch_hit((a), (n), (p)); } while (0)
#else
#define RC_W(a, n, p) do { } while (0)
#endif
static inline void WR32(uint32_t a, uint32_t v) { RC_W(a, 4, &v); memcpy(M + a, &v, 4); }
static inline void WR16(uint32_t a, uint32_t v) { uint16_t w = (uint16_t)v; RC_W(a, 2, &w); memcpy(M + a, &w, 2); }
static inline void WR8(uint32_t a, uint32_t v)  { RC_W(a, 1, &v); M[a] = (uint8_t)v; }
static inline void WR64(uint32_t a, uint64_t v) { RC_W(a, 8, &v); memcpy(M + a, &v, 8); }
static inline float  RDF32(uint32_t a) { float f; memcpy(&f, M + a, 4); return f; }
static inline double RDF64(uint32_t a) { double d; memcpy(&d, M + a, 8); return d; }
static inline void WRF32(uint32_t a, float f)  { RC_W(a, 4, &f); memcpy(M + a, &f, 4); }
static inline void WRF64(uint32_t a, double d) { RC_W(a, 8, &d); memcpy(M + a, &d, 8); }

/* ── lazy flags ────────────────────────────────────────────────────────── */
enum { FO_ADD = 0, FO_SUB = 1, FO_LOGIC = 2, FO_X = 3 };

static inline uint32_t rc_mask(int sz) { return sz == 32 ? 0xffffffffu : sz == 16 ? 0xffffu : 0xffu; }
static inline uint32_t rc_sign(int sz) { return sz == 32 ? 0x80000000u : sz == 16 ? 0x8000u : 0x80u; }

static inline int rc_zf(int op, uint32_t r, int sz) { (void)op; return (r & rc_mask(sz)) == 0; }
static inline int rc_sf(uint32_t r, int sz) { return (r & rc_sign(sz)) != 0; }
static inline int rc_cf(int op, uint32_t a, uint32_t b, uint32_t r, int sz, uint32_t cf) {
    uint32_t m = rc_mask(sz);
    switch (op) {
    case FO_ADD: return (r & m) < (a & m);
    case FO_SUB: return (a & m) < (b & m);
    case FO_LOGIC: return 0;
    default: return cf != 0;
    }
}
static inline int rc_of(int op, uint32_t a, uint32_t b, uint32_t r, int sz, uint32_t of) {
    uint32_t s = rc_sign(sz);
    switch (op) {
    case FO_ADD: return (((a ^ r) & (b ^ r)) & s) != 0;
    case FO_SUB: return (((a ^ b) & (a ^ r)) & s) != 0;
    case FO_LOGIC: return 0;
    default: return of != 0;
    }
}
static inline int rc_pf(uint32_t r) { return !__builtin_parity(r & 0xffu); }

#define FL_ZF rc_zf(F_op, F_r, F_sz)
#define FL_SF rc_sf(F_r, F_sz)
#define FL_CF rc_cf(F_op, F_a, F_b, F_r, F_sz, F_cf)
#define FL_OF rc_of(F_op, F_a, F_b, F_r, F_sz, F_of)
#define FL_PF rc_pf(F_r)

uint32_t rc_eflags(int op, uint32_t a, uint32_t b, uint32_t r, int sz, uint32_t cf, uint32_t of);

/* ── x87 ───────────────────────────────────────────────────────────────── */
#define ST(i) (R.st[(R.top + (i)) & 7])
static inline void FPUSH(double v) { R.top = (R.top - 1) & 7; R.st[R.top] = v; }
static inline void FPOP(void) { R.top = (R.top + 1) & 7; }
static inline void rc_fcom(double a, double b) {
    /* C3 C2 C0: 000 a>b, 001 a<b, 100 a==b, 111 unordered */
    uint16_t cc;
    if (a != a || b != b) cc = 0x4500;
    else if (a > b) cc = 0x0000;
    else if (a < b) cc = 0x0100;
    else cc = 0x4000;
    R.fsw_cc = cc;
}
static inline uint32_t rc_fsw(void) { return (uint32_t)R.fsw_cc | ((uint32_t)(R.top & 7) << 11); }
int32_t rc_fist32(double v);   /* current rounding mode: round to nearest even */
int64_t rc_fist64(double v);

/* ── control transfer ──────────────────────────────────────────────────── */
typedef void (*rc_fn)(void);
typedef struct { uint32_t addr; rc_fn fn; } rc_fn_entry;
extern const rc_fn_entry rc_fn_table[];
extern const unsigned rc_fn_count;
/* instruction trace (generated with LF2_RC_TRACE=1) */
extern int rc_tracing;
void rc_trace_flush(void);
void rc_trace(uint32_t pc, uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t ebx,
              uint32_t esp, uint32_t ebp, uint32_t esi, uint32_t edi);

void rc_call(uint32_t target);           /* indirect call/jmp: recompiled or native */
void rc_trap(uint32_t where, const char* why) __attribute__((noreturn));
void rc_cpuid(uint32_t* a, uint32_t* b, uint32_t* c, uint32_t* d);
void rc_stos32(uint32_t dst, uint32_t v, uint32_t count);

/* CRT helpers implemented natively (see NATIVE in tools/recomp/emit.py) */
void n_ftol2_sse(void);

#ifdef __cplusplus
}
#endif
#endif
