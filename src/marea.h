/* La marea de Constitución, sincronizada con el SHOA.
   La altura sale de 36 constituyentes armónicos con sus correcciones nodales (el ciclo de 18,6 años de la Luna), medidos
   en el mareógrafo del SHOA en Constitución y ajustados a su tabla oficial. Donde hay tabla oficial, la curva se corrige
   para pasar justo por sus pleamares y bajamares. La tabla viene incluida, se guarda en mareas_shoa.txt y la app la renueva
   sola desde shoa.cl cuando le falta el mes que viene. El SHOA da las horas en UTC-4 todo el año. */
#pragma once
#include <winhttp.h>
enum { NOD_UNO, NOD_M2, NOD_K1, NOD_O1, NOD_K2, NOD_J1, NOD_OO1, NOD_MF, NOD_MM, NOD_MSF, NOD_MO3, NOD_M3, NOD_MK3, NOD_M4, NOD_MK4, NOD_M6, NNOD };
#include "marea_ctes.h"
#define PI_D 3.14159265358979323846

typedef struct { double t; float h, dh; int pleamar; } extremo;   /* t: días desde J2000 (UTC); h sobre el NRS; dh = tabla - armónicos */
typedef struct { int n; extremo e[]; } tabla_marea;
static tabla_marea *volatile g_tmar;
static volatile LONG g_tmar_estado;   /* 0 tabla guardada al día, 1 bajando de shoa.cl, 2 recién bajada, 3 sin conexión */
static double g_marea_desfase;        /* solo para pruebas: corre el reloj de la marea */

static double marea_ahora(void) {   /* días desde J2000 (2000-01-01 12:00 UTC) según el reloj del sistema */
    FILETIME ft; GetSystemTimeAsFileTime(&ft); ULARGE_INTEGER u; u.LowPart = ft.dwLowDateTime; u.HighPart = ft.dwHighDateTime;
    return ((double)u.QuadPart - 125912016000000000.0) / 864000000000.0 + g_marea_desfase;
}

/* ---------------- los armónicos ---------------- */
static double marea_armonica(double d) {   /* altura sobre el NRS */
    double T = d / 36525.0, N = 125.04452 - 1934.136261 * T;
    double s = 218.3164477 + 481267.88123421 * T, h = 280.46646 + 36000.76983 * T, p = 83.3532465 + 4069.0137287 * T, p1 = 282.93768 + 1.71946 * T;
    double var[6] = { 360.0 * (d - floor(d)) + h - s, s, h, p, -N, p1 };   /* tiempo lunar, Luna, Sol, perigeo, nodo, perihelio */
    double n = N * (PI_D / 180), c1 = cos(n), c2 = cos(2 * n), c3 = cos(3 * n), s1 = sin(n), s2 = sin(2 * n), s3 = sin(3 * n);
    double f[NNOD], u[NNOD];
    f[NOD_UNO] = 1; u[NOD_UNO] = 0;
    f[NOD_M2] = 1.0004 - 0.0373 * c1 + 0.0002 * c2;                u[NOD_M2] = -2.14 * s1;
    f[NOD_K1] = 1.0060 + 0.1150 * c1 - 0.0088 * c2 + 0.0006 * c3;  u[NOD_K1] = -8.86 * s1 + 0.68 * s2 - 0.07 * s3;
    f[NOD_O1] = 1.0089 + 0.1871 * c1 - 0.0147 * c2 + 0.0014 * c3;  u[NOD_O1] = 10.80 * s1 - 1.34 * s2 + 0.19 * s3;
    f[NOD_K2] = 1.0241 + 0.2863 * c1 + 0.0083 * c2 - 0.0015 * c3;  u[NOD_K2] = -17.74 * s1 + 0.68 * s2 - 0.04 * s3;
    f[NOD_J1] = 1.0129 + 0.1676 * c1 - 0.0170 * c2 + 0.0016 * c3;  u[NOD_J1] = -12.94 * s1 + 1.34 * s2 - 0.19 * s3;
    f[NOD_OO1] = 1.1027 + 0.6504 * c1 + 0.0317 * c2 - 0.0014 * c3; u[NOD_OO1] = -36.68 * s1 + 4.02 * s2 - 0.57 * s3;
    f[NOD_MF] = 1.0429 + 0.4135 * c1 - 0.0040 * c2;                u[NOD_MF] = -23.74 * s1 + 2.68 * s2 - 0.38 * s3;
    f[NOD_MM] = 1.0000 - 0.1300 * c1 + 0.0013 * c2;                u[NOD_MM] = 0;
    f[NOD_MSF] = f[NOD_M2];                                        u[NOD_MSF] = -u[NOD_M2];
    f[NOD_MO3] = f[NOD_M2] * f[NOD_O1];                            u[NOD_MO3] = u[NOD_M2] + u[NOD_O1];
    f[NOD_M3] = pow(f[NOD_M2], 1.5);                               u[NOD_M3] = 1.5 * u[NOD_M2];
    f[NOD_MK3] = f[NOD_M2] * f[NOD_K1];                            u[NOD_MK3] = u[NOD_M2] + u[NOD_K1];
    f[NOD_M4] = f[NOD_M2] * f[NOD_M2];                             u[NOD_M4] = 2 * u[NOD_M2];
    f[NOD_MK4] = f[NOD_M2] * f[NOD_K2];                            u[NOD_MK4] = u[NOD_M2] + u[NOD_K2];
    f[NOD_M6] = f[NOD_M4] * f[NOD_M2];                             u[NOD_M6] = 3 * u[NOD_M2];
    double z = MAREA_Z0;
    for (int k = 0; k < MAREA_NC; k++) {
        double V = marea_c[k].fase;
        for (int i = 0; i < 6; i++) V += marea_c[k].d[i] * var[i];
        V = fmod(V + u[marea_c[k].nod] - marea_c[k].g, 360.0);
        z += f[marea_c[k].nod] * marea_c[k].H * cos(V * (PI_D / 180));
    }
    return z;
}

/* ---------------- la tabla oficial ---------------- */
static long dias_civiles(int y, int m, int d) {   /* días desde 1970-01-01 (Hinnant) */
    y -= m <= 2; long era = (y >= 0 ? y : y - 399) / 400; unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1, doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long)doe - 719468;
}
static void civil(long z, int *y, int *m, int *d) {
    z += 719468; long era = (z >= 0 ? z : z - 146096) / 146097; unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365, doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1); *m = (int)(mp < 10 ? mp + 3 : mp - 9); *y = (int)(yoe + era * 400 + (*m <= 2));
}
static int dig(const char *s, int n) { int v = 0; for (int i = 0; i < n; i++) { if (s[i] < '0' || s[i] > '9') return -1; v = v * 10 + s[i] - '0'; } return v; }
/* lee "dd/mm/aaaa hh:mm h.hh P" donde aparezca: sirve para mareas_shoa.txt y para la página del SHOA sin etiquetas */
static int leer_extremos(const char *s, extremo *out, int max) {
    int n = 0; long dia = -1;
    for (const char *p = s; *p && n < max; p++) {
        if (dig(p, 2) >= 1 && p[2] == '/' && dig(p + 3, 2) >= 1 && p[5] == '/' && dig(p + 6, 4) >= 2000) {
            int d = dig(p, 2), m = dig(p + 3, 2), y = dig(p + 6, 4);
            if (d <= 31 && m <= 12) dia = dias_civiles(y, m, d);
            p += 9; continue;
        }
        if (dia < 0 || dig(p, 2) < 0 || p[2] != ':' || dig(p + 3, 2) < 0 || (p > s && p[-1] >= '0' && p[-1] <= '9')) continue;
        int hh = dig(p, 2), mi = dig(p + 3, 2); const char *q = p + 5;
        while (*q == ' ') q++;
        int neg = *q == '-'; if (neg) q++;
        if (*q < '0' || *q > '9') continue;
        int ent = 0; while (*q >= '0' && *q <= '9') ent = ent * 10 + *q++ - '0';
        if (*q++ != '.' || dig(q, 2) < 0) continue;
        float h = ent + dig(q, 2) / 100.0f; q += 2; if (neg) h = -h;
        while (*q == ' ') q++;
        if ((*q != 'P' && *q != 'B') || (q[1] >= 'A' && q[1] <= 'z') || hh > 23 || mi > 59 || h < -2 || h > 5) continue;
        extremo e = { (double)dia - 10957.5 + (hh + 4) / 24.0 + mi / 1440.0, h, 0, *q == 'P' };
        out[n++] = e; p = q;
    }
    return n;
}
static int por_hora(const void *a, const void *b) { double x = ((const extremo *)a)->t, y = ((const extremo *)b)->t; return x < y ? -1 : x > y; }
/* junta la tabla vieja con la nueva (la nueva manda en su tramo), ordena y calcula la corrección de cada extremo */
static tabla_marea *tabla_juntar(const tabla_marea *vieja, const extremo *nueva, int nn) {
    int nv = vieja ? vieja->n : 0;
    tabla_marea *T = HeapAlloc(GetProcessHeap(), 0, sizeof(tabla_marea) + (size_t)(nv + nn) * sizeof(extremo)); if (!T) return 0;
    double t0 = 1e30, t1 = -1e30; for (int i = 0; i < nn; i++) { if (nueva[i].t < t0) t0 = nueva[i].t; if (nueva[i].t > t1) t1 = nueva[i].t; }
    int n = 0;
    for (int i = 0; i < nv; i++) if (vieja->e[i].t < t0 - 0.001 || vieja->e[i].t > t1 + 0.001) T->e[n++] = vieja->e[i];
    for (int i = 0; i < nn; i++) T->e[n++] = nueva[i];
    qsort(T->e, n, sizeof(extremo), por_hora);
    for (int i = 0; i < n; i++) T->e[i].dh = T->e[i].h - (float)marea_armonica(T->e[i].t);
    T->n = n; return T;
}
static void marea_ruta(wchar_t *r) { GetModuleFileNameW(0, r, MAX_PATH); wchar_t *p = r + lstrlenW(r); while (p > r && p[-1] != '\\') p--; lstrcpyW(p, L"mareas_shoa.txt"); }
static void marea_guardar(const tabla_marea *T) {   /* en el formato del SHOA, para que se pueda leer y comparar a mano */
    char *b = HeapAlloc(GetProcessHeap(), 0, (size_t)T->n * 32 + 16); if (!b) return; int k = 0;
    for (int i = 0; i < T->n; i++) {
        double t = T->e[i].t + 10957.5 - 4 / 24.0 + 0.5 / 1440; long dia = (long)floor(t); int y, m, d; civil(dia, &y, &m, &d);
        int mins = (int)((t - dia) * 1440), cm = (int)lroundf(T->e[i].h * 100), acm = cm < 0 ? -cm : cm;
        k += wsprintfA(b + k, "%02d/%02d/%04d %02d:%02d %s%d.%02d %c\r\n", d, m, y, mins / 60, mins % 60, cm < 0 ? "-" : "", acm / 100, acm % 100, T->e[i].pleamar ? 'P' : 'B');
    }
    wchar_t r[MAX_PATH]; marea_ruta(r); DWORD w;
    HANDLE f = CreateFileW(r, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0); if (f != INVALID_HANDLE_VALUE) { WriteFile(f, b, k, &w, 0); CloseHandle(f); }
    HeapFree(GetProcessHeap(), 0, b);
}

/* ---------------- la tabla al día desde shoa.cl ---------------- */
#define MAX_EXT 6000
static DWORD WINAPI marea_bajar(void *arg) {
    InterlockedExchange(&g_tmar_estado, 1);
    DWORD cap = 1 << 20, n = 0; int ok = 0; char *b = HeapAlloc(GetProcessHeap(), 0, cap);
    HINTERNET s = b ? WinHttpOpen(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0 Safari/537.36",
                                  WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0) : 0;
    HINTERNET c = 0, r = 0;
    if (s) { WinHttpSetTimeouts(s, 10000, 10000, 10000, 20000); c = WinHttpConnect(s, L"www.shoa.cl", INTERNET_DEFAULT_HTTPS_PORT, 0); }
    if (c) r = WinHttpOpenRequest(c, L"POST", L"/php/mareas.php", 0, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    static char cuerpo[] = "local=constitucion&idioma=es";
    if (r && WinHttpSendRequest(r, L"Content-Type: application/x-www-form-urlencoded\r\n", (DWORD)-1L, cuerpo, sizeof cuerpo - 1, sizeof cuerpo - 1, 0) && WinHttpReceiveResponse(r, 0)) {
        DWORD est = 0, tam = sizeof est;
        WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &est, &tam, WINHTTP_NO_HEADER_INDEX);
        for (DWORD a; est == 200 && WinHttpQueryDataAvailable(r, &a) && a > 0;) {
            DWORD l = 0; if (a > cap - 1 - n) a = cap - 1 - n; if (!a || !WinHttpReadData(r, b + n, a, &l) || !l) break; n += l;
        }
        ok = est == 200 && n > 1000;
    }
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    if (s) WinHttpCloseHandle(s);
    LONG fin = 3;
    if (ok) {
        b[n] = 0; char *w = b; int dentro = 0;   /* sin etiquetas: cada una se vuelve un espacio */
        for (char *p = b; *p; p++) { if (*p == '<') dentro = 1; else if (*p == '>') { dentro = 0; *w++ = ' '; } else if (!dentro) *w++ = *p == '\n' || *p == '\t' || *p == '\r' ? ' ' : *p; }
        *w = 0;
        for (char *p = b, *q = b; ; p++) { if (*p == ' ' && q > b && q[-1] == ' ') continue; *q++ = *p; if (!*p) break; }   /* espacios repetidos */
        extremo *e = HeapAlloc(GetProcessHeap(), 0, MAX_EXT * sizeof(extremo)); int ne = e ? leer_extremos(b, e, MAX_EXT) : 0;
        int alternan = ne >= 60; for (int i = 1; i < ne && alternan; i++) alternan = e[i].pleamar != e[i - 1].pleamar && e[i].t > e[i - 1].t;
        if (alternan) {
            tabla_marea *T = tabla_juntar(g_tmar, e, ne);
            if (T) { marea_guardar(T); InterlockedExchangePointer((void *volatile *)&g_tmar, T); fin = 2; }
        }
        if (e) HeapFree(GetProcessHeap(), 0, e);
    }
    if (b) HeapFree(GetProcessHeap(), 0, b);
    InterlockedExchange(&g_tmar_estado, fin);
    return 0;
}
static void marea_iniciar(int bajar) {
    extremo *e = HeapAlloc(GetProcessHeap(), 0, MAX_EXT * sizeof(extremo)); if (!e) return;
    tabla_marea *T = tabla_juntar(0, e, leer_extremos(marea_tabla_incluida, e, MAX_EXT));
    wchar_t r[MAX_PATH]; marea_ruta(r);
    HANDLE f = CreateFileW(r, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
    if (f != INVALID_HANDLE_VALUE) {   /* la tabla que quedó guardada de otras noches */
        DWORD tam = GetFileSize(f, 0), rd = 0; char *txt = tam < (8u << 20) ? HeapAlloc(GetProcessHeap(), 0, tam + 1) : 0;
        if (txt && ReadFile(f, txt, tam, &rd, 0)) {
            txt[rd] = 0; int n = leer_extremos(txt, e, MAX_EXT);
            if (n > 0) { tabla_marea *T2 = tabla_juntar(T, e, n); if (T2) T = T2; }
        }
        if (txt) HeapFree(GetProcessHeap(), 0, txt);
        CloseHandle(f);
    }
    HeapFree(GetProcessHeap(), 0, e);
    g_tmar = T;
    /* si falta la tabla del mes que viene, se pide al SHOA sin esperar */
    if (bajar && (!T || T->n < 2 || T->e[T->n - 1].t < marea_ahora() + 20)) CreateThread(0, 0, marea_bajar, 0, 0, 0);
}

/* ---------------- lo que usa la app ---------------- */
static float marea_nivel(double d) {   /* altura sobre el NRS: armónicos, corregidos para pasar por la tabla oficial */
    double z = marea_armonica(d);
    const tabla_marea *T = g_tmar;
    if (T && T->n >= 2) {
        const extremo *e = T->e; int n = T->n;
        if (d >= e[0].t && d < e[n - 1].t) {
            int lo = 0, hi = n - 1; while (hi - lo > 1) { int m = (lo + hi) >> 1; if (e[m].t <= d) lo = m; else hi = m; }
            if (e[hi].t - e[lo].t < 0.5) z += e[lo].dh + (e[hi].dh - e[lo].dh) * (d - e[lo].t) / (e[hi].t - e[lo].t);
        } else {   /* fuera de la tabla la corrección se apaga en 6 horas */
            double g = d < e[0].t ? e[0].t - d : d - e[n - 1].t; float dh = d < e[0].t ? e[0].dh : e[n - 1].dh;
            if (g < 0.25) z += dh * (1 - g / 0.25);
        }
    }
    return (float)z;
}
/* pleamares y bajamares entre d0 y d1: los de la tabla oficial si la cubre; si no, los de la curva */
static int marea_extremos(double d0, double d1, extremo *out, int max, int *oficial) {
    const tabla_marea *T = g_tmar; int k = 0;
    if (T && T->n >= 2 && d0 >= T->e[0].t - 0.3 && d1 <= T->e[T->n - 1].t + 0.3) {
        for (int i = 0; i < T->n && k < max; i++) if (T->e[i].t >= d0 && T->e[i].t <= d1) out[k++] = T->e[i];
        *oficial = 1; return k;
    }
    *oficial = 0;
    double paso = 4.0 / 1440; float h0 = marea_nivel(d0 - paso), h1 = marea_nivel(d0);
    for (double d = d0; d <= d1 && k < max; d += paso) {
        float h2 = marea_nivel(d + paso);
        if ((h1 - h0) * (h2 - h1) < 0 || (h1 != h0 && h2 == h1)) {   /* la pendiente cambia de signo: parábola por los tres puntos */
            float den = h0 - 2 * h1 + h2, x = den != 0 ? 0.5f * (h0 - h2) / den : 0;
            extremo e = { d + x * paso, h1 - 0.25f * (h0 - h2) * x, 0, h1 > h0 }; out[k++] = e;
        }
        h0 = h1; h1 = h2;
    }
    return k;
}
static void marea_hora(double d, char *s) {   /* hh:mm en la hora de este computador (con su horario de verano) */
    ULARGE_INTEGER u; u.QuadPart = (ULONGLONG)((d + 0.5 / 1440) * 864000000000.0 + 125912016000000000.0);
    FILETIME ft; ft.dwLowDateTime = u.LowPart; ft.dwHighDateTime = u.HighPart;
    SYSTEMTIME su, sl; FileTimeToSystemTime(&ft, &su); if (!SystemTimeToTzSpecificLocalTime(0, &su, &sl)) sl = su;
    wsprintfA(s, "%02d:%02d", sl.wHour, sl.wMinute);
}
static void marea_metros(float h, char *s) { int cm = (int)lroundf(h * 100), a = cm < 0 ? -cm : cm; wsprintfA(s, "%s%d,%02d m", cm < 0 ? "-" : "", a / 100, a % 100); }
