/* Piezas comunes del Pacífico de Pellines: azar, filtros, ruido del silicio y una piscina de hilos. */
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <math.h>

typedef unsigned int u32; typedef unsigned long long u64; typedef unsigned char u8;
#define FS 48000
#define PI 3.14159265f

static u32 xs(u32 *s) { u32 x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }
static float uni(u32 *s, float a, float b) { return a + (b - a) * (xs(s) / 4294967296.0f); }
static float gauss(u32 *s) { float u = uni(s, 1e-7f, 1), v = uni(s, 0, 2 * PI); return sqrtf(-2 * logf(u)) * cosf(v); }
static float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static float suave(float x) { x = clampf(x, 0, 1); return x * x * (3 - 2 * x); }
static float sin_rapido(float x) { float y = 1.2732395f * x - 0.4052847f * x * fabsf(x); return 0.225f * (y * fabsf(y) - y) + y; }
static inline float minmod(float a, float b) { return a * b <= 0 ? 0 : fabsf(a) < fabsf(b) ? a : b; }

typedef struct { float ic1, ic2, a1, a2, a3, k; } svf;
static void svf_fijar(svf *f, float fc, float q) {
    fc = clampf(fc, 15, 0.45f * FS);
    float g = tanf(PI * fc / FS); f->k = 1 / q; f->a1 = 1 / (1 + g * (g + f->k)); f->a2 = g * f->a1; f->a3 = g * f->a2;
}
static inline void svf_paso(svf *f, float v0, float *lp, float *bp, float *hp) {
    float v3 = v0 - f->ic2, v1 = f->a1 * f->ic1 + f->a2 * v3, v2 = f->ic2 + f->a2 * f->ic1 + f->a3 * v3;
    f->ic1 = 2 * v1 - f->ic1; f->ic2 = 2 * v2 - f->ic2; *lp = v2; *bp = v1; *hp = v0 - f->k * v1 - v2;
}
/* resonador de dos polos: un modo de vibración de una plancha, un vidrio o una burbuja */
typedef struct { float a1, a2, y1, y2, g; } modo;
static void modo_fijar(modo *m, float f, float tau, float g) {   /* g: amplitud inicial por golpe unitario */
    float w = 2 * PI * f / FS, r = expf(-1.0f / (tau * FS)); m->a1 = 2 * r * cosf(w); m->a2 = -r * r; m->g = g * sinf(w);
}
static inline float modo_paso(modo *m, float x) { float y = m->a1 * m->y1 + m->a2 * m->y2 + x * m->g; m->y2 = m->y1; m->y1 = y; return y; }

typedef struct { u32 s; float b0, b1, b2, br; } ruido;

/* el ruido nace del silicio: RDRAND entrega bits que el procesador saca del ruido térmico de un circuito propio */
static float g_hw_mbit; static int g_hw_modo; static short *HWP; static int hw_i = 1 << 30, hw_n = 16384; static volatile u64 g_hw_bits;
static inline int rd64(unsigned long long *v) { unsigned char ok; __asm__ volatile("rdrand %0; setc %1" : "=r"(*v), "=qm"(ok)); return ok; }
static void hw_rellenar(void) {
    unsigned long long v = 0; u32 s = 0x9E3779B9;
    for (int i = 0; i < hw_n; i += 4) {
        if (g_hw_modo == 1 || (i & 63) == 0) {
            int ok = 0; for (int k = 0; k < 10 && !ok; k++) ok = rd64(&v);
            if (!ok) v = ((u64)xs(&s) << 32) | xs(&s); else g_hw_bits += 64;
            s = (u32)v | 1;
        } else v = ((u64)xs(&s) << 32) | xs(&s);
        CopyMemory(HWP + i, &v, 8);
    }
    hw_i = 0;
}
static inline float ruido_blanco(ruido *r) {
    if (g_hw_modo) { if (hw_i >= hw_n) hw_rellenar(); return HWP[hw_i++] * (1.0f / 32768.0f); }
    return (int)xs(&r->s) * 4.656613e-10f;
}
static void hw_iniciar(void) {
    unsigned int a, b, c, d; __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1), "c"(0));
    if (!(c & (1u << 30))) { g_hw_modo = 0; return; }
    HWP = VirtualAlloc(0, (size_t)hw_n * 2, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    LARGE_INTEGER f, t0, t1; unsigned long long v; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&t0);
    for (int i = 0; i < 4096; i++) rd64(&v);
    QueryPerformanceCounter(&t1);
    double por_seg = 4096.0 * f.QuadPart / (double)(t1.QuadPart - t0.QuadPart + 1);
    g_hw_modo = por_seg > 1.5e6 ? 1 : 2;
}
static inline void ruido_paso(ruido *r, float *w, float *p, float *b) {
    float x = ruido_blanco(r);
    r->b0 = 0.99765f * r->b0 + x * 0.0990460f; r->b1 = 0.96300f * r->b1 + x * 0.2965164f; r->b2 = 0.57000f * r->b2 + x * 1.0526913f;
    *p = (r->b0 + r->b1 + r->b2 + x * 0.1848f) * 0.22f; r->br = (r->br + 0.02f * x) / 1.02f; *b = r->br * 3.5f; *w = x;
}

static void *reservar(size_t n) { return VirtualAlloc(0, n, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE); }
/* los números diminutos que dejan la espuma y los filtros al apagarse se redondean a cero: si no, el procesador
   los calcula por un camino lento, hasta cien veces más caro */
static inline void sin_desnormales(void) { unsigned int m; __asm__ volatile("stmxcsr %0" : "=m"(m)); m |= 0x8040; __asm__ volatile("ldmxcsr %0" : : "m"(m)); }

/* piscina de hilos: todos ejecutan la misma tarea con su número y esperan a los demás */
#define MAXHIL 32
typedef void (*tarea)(int id, int n);
typedef struct piscina piscina;
typedef struct { piscina *p; int id; } hilo_arg;
struct piscina { int n; HANDLE ini[MAXHIL], fin[MAXHIL]; volatile tarea fn; hilo_arg arg[MAXHIL]; };
static DWORD WINAPI piscina_hilo(void *q) {
    hilo_arg *a = q; sin_desnormales();
    for (;;) { WaitForSingleObject(a->p->ini[a->id], INFINITE); a->p->fn(a->id, a->p->n); SetEvent(a->p->fin[a->id]); }
    return 0;
}
static void piscina_crear(piscina *p, int n, int prioridad) {
    p->n = n < 1 ? 1 : n > MAXHIL ? MAXHIL : n;
    for (int i = 0; i < p->n; i++) {
        p->ini[i] = CreateEventW(0, FALSE, FALSE, 0); p->fin[i] = CreateEventW(0, FALSE, FALSE, 0); p->arg[i].p = p; p->arg[i].id = i;
        HANDLE h = CreateThread(0, 0, piscina_hilo, &p->arg[i], 0, 0); if (prioridad) SetThreadPriority(h, prioridad);
    }
}
static void piscina_correr(piscina *p, tarea fn) {
    p->fn = fn; for (int i = 0; i < p->n; i++) SetEvent(p->ini[i]);
    WaitForMultipleObjects(p->n, p->fin, TRUE, INFINITE);
}
