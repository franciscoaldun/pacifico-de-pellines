/* Pacífico de Pellines 3: la vista literal desde la cabaña de Pellines (Pacífico de Pellines 2 quedó tal cual). El sonido, la lluvia, la física de las
   olas y la distancia a la playa son los mismos de «Pacífico de Pellines», que quedó tal cual; aquí solo cambia lo que
   se ve: la ventana de la cabaña, la espuma, el detalle y la calidad, con el procesador y la gráfica a fondo.
   Un hilo resuelve las ecuaciones del oleaje sobre 800 m de playa al ritmo del reloj del audio; el sonido sale
   de la energía que pierde cada ola al reventar y la imagen es una vista de esa misma agua. La marea, la
   marejada, el viento y la lluvia cambian durante la noche. Windows solo pone ventana, WASAPI y reloj. */
#include "comun.h"
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>
#include <dwmapi.h>
#include <mmsystem.h>
#include "mar.h"
#include "sonido.h"
#include "astro.h"
#include "vista.h"
#include "marea.h"
#include "gpu.h"
#include "fuente.h"

/* los letreros de la Play, con la misma letra de los textos de la app, para la pantalla de la tele */
static void atlas_tele(void) {
    static u8 px[512 * 96]; static const char *l[4] = { "Browser", "System Configuration", "SONY COMPUTER ENTERTAINMENT", "PlayStation 2" };
    for (int f = 0; f < 4; f++) {
        int x = 0;
        for (const char *s = l[f]; *s && x + FUENTE_ANCHO <= 512; s++, x += FUENTE_ANCHO) {
            int g = 0; for (int i = 0; i < FUENTE_N; i++) if (fuente_cp[i] == (u8)*s) { g = i; break; }
            for (int yy = 0; yy < FUENTE_ALTO; yy++) for (int xx = 0; xx < FUENTE_ANCHO; xx++) px[(f * 24 + yy) * 512 + x + xx] = fuente_alfa[g][yy * FUENTE_ANCHO + xx];
        }
    }
    gpu_atlas(px);
}

/* ---------------- la noche: marea, marejada y viento ---------------- */
static struct { float ou_sw, sw_base, vang, vang_obj, z_desde, ajuste; } AMB;
static void ambiente_iniciar(void) {
    AMB.sw_base = 1; AMB.vang = AMB.vang_obj = 0; AMB.ajuste = 1;
}
static float marea_objetivo(void) {   /* el nivel del agua sobre el nivel medio: la marea del SHOA a esta hora y la baja presión de un frente */
    return marea_nivel(marea_ahora()) - (float)MAREA_Z0 + 0.10f * clampf(S.R / 6, 0, 1);
}
static void ambiente(void) {   /* se llama antes de cada paso del mar */
    /* la marea de Constitución según el SHOA; si el mar viene de otra noche, primero se pone al día */
    float z = marea_objetivo();
    M.zeta0 = AMB.ajuste < 1 ? AMB.z_desde + (z - AMB.z_desde) * suave(AMB.ajuste) : z;
    /* la marejada crece y baja durante la noche */
    AMB.ou_sw += -AMB.ou_sw * DTM / 2400 + 0.12f * sqrtf(2 * DTM / 2400) * gauss(&M.rng);
    M.a_sw = clampf(AMB.sw_base * (1 + AMB.ou_sw), 0.55f, fminf(1.4f, 3.4f / M.hs_sw));
    /* el viento: del norte cuando llueve (los frentes del invierno), del suroeste cuando no */
    AMB.vang_obj = S.R > 0.3f ? 1 : 0; AMB.vang += (AMB.vang_obj - AMB.vang) * DTM / 300;
    float vxn = 0.55f + (-0.85f - 0.55f) * AMB.vang, vzn = -0.8f + (-0.45f + 0.8f) * AMB.vang;
    M.vx = S.U * vxn; M.vz = S.U * vzn;
    M.a_ws = clampf((S.U / 7) * (S.U / 7), 0.08f, 1.5f);
}

/* ---------------- el mar sigue entre una noche y otra ---------------- */
static void ruta_estado(wchar_t *r) { GetModuleFileNameW(0, r, MAX_PATH); wchar_t *p = r + lstrlenW(r); while (p > r && p[-1] != '\\') p--; lstrcpyW(p, L"estado_mar.bin"); }
#define MAGIA 0x4D415231u
static void guardar_mar(void) {
    wchar_t r[MAX_PATH]; ruta_estado(r);
    HANDLE f = CreateFileW(r, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0); if (f == INVALID_HANDLE_VALUE) return;
    DWORD w; u32 cab[4] = { MAGIA, NX, NZ, (u32)(DXM * 1000) };
    WriteFile(f, cab, 16, &w, 0); WriteFile(f, g_fase, sizeof g_fase, &w, 0); WriteFile(f, &M.t, 8, &w, 0);
    float *cs[] = { M.eta, M.u, M.v, M.W, M.rod, M.wd, M.seco, M.pend };
    for (int k = 0; k < 8; k++) WriteFile(f, cs[k], NXP * NZP * 4, &w, 0);
    LONG np = 0; WriteFile(f, &np, 4, &w, 0);   /* ya no hay partículas de espuma: el lugar queda por compatibilidad */
    WriteFile(f, &M.zeta0, 4, &w, 0);   /* la marea con que quedó, para ponerla al día la próxima vez */
    float *ce[] = { M.R, M.du, M.dv };   /* la espuma que flotaba y el dibujo que llevaba */
    for (int k = 0; k < 3; k++) WriteFile(f, ce[k], NXP * NZP * 4, &w, 0);
    CloseHandle(f);
}
static float nivel_afuera(void) {   /* el nivel medio lejos de la rompiente, para estados guardados sin la marea anotada */
    double s = 0; int n = 0;
    for (int j = NZ - 24; j < NZ; j++) for (int i = 0; i < NX; i++) { s += M.eta[IX(j, i)]; n++; }
    return (float)(s / n);
}
static int cargar_mar(void) {
    wchar_t r[MAX_PATH]; ruta_estado(r);
    HANDLE f = CreateFileW(r, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0); if (f == INVALID_HANDLE_VALUE) return 0;
    DWORD rd; u32 cab[4]; int ok = ReadFile(f, cab, 16, &rd, 0) && rd == 16 && cab[0] == MAGIA && cab[1] == NX && cab[2] == NZ && cab[3] == (u32)(DXM * 1000);
    if (ok) {
        double t; ReadFile(f, g_fase, sizeof g_fase, &rd, 0); ReadFile(f, &t, 8, &rd, 0);
        float *cs[] = { M.eta, M.u, M.v, M.W, M.rod, M.wd, M.seco, M.pend };
        for (int k = 0; k < 8 && ok; k++) ok = ReadFile(f, cs[k], NXP * NZP * 4, &rd, 0) && rd == (DWORD)(NXP * NZP * 4);
        LONG np = 0; if (ok) ok = ReadFile(f, &np, 4, &rd, 0) && np >= 0 && np <= MAXESP;
        if (ok) { SetFilePointer(f, np * (LONG)sizeof(espuma), 0, FILE_CURRENT); M.t = t; }   /* las partículas de las versiones anteriores se saltan */
        if (ok) for (int j = -2; j < NZ + 2; j++) for (int i = -2; i < NX + 2; i++) {   /* el mismo fondo de la noche anterior */
            int jj = j < 0 ? 0 : j >= NZ ? NZ - 1 : j, ii = (i + NX) % NX; M.hb[IX(j, i)] = -fondo((ii + 0.5f - NX / 2) * DXM, ZPRIM + (jj + 0.5f) * DXM);
        }
        if (ok) {
            float z; if (!(ReadFile(f, &z, 4, &rd, 0) && rd == 4 && z > -3 && z < 3)) z = nivel_afuera();
            AMB.z_desde = z; AMB.ajuste = 0;
            M.rampa = 1;   /* la marejada sigue entrando por el borde, como cuando se formó */
            float *ce[] = { M.R, M.du, M.dv };   /* si el estado es de antes, la espuma que flotaba empieza de cero */
            for (int k = 0; k < 3; k++) if (!(ReadFile(f, ce[k], NXP * NZP * 4, &rd, 0) && rd == (DWORD)(NXP * NZP * 4))) { ZeroMemory(M.R, NXP * NZP * 4); ZeroMemory(M.du, NXP * NZP * 4); ZeroMemory(M.dv, NXP * NZP * 4); break; }
        }
    }
    CloseHandle(f);
    return ok;
}

/* ---------------- el hilo del mar: va un poco adelante del oído ---------------- */
static volatile LONG g_mar_salir, g_mar_vivo; static volatile LONG g_mar_atras; static volatile float g_mar_adelanto_min = 9;
static DWORD WINAPI hilo_mar(void *p) {
    sin_desnormales();
    while (!g_mar_salir) {
        double obj = S.t0 + (double)S.muestra / FS + LEAD, ad = M.t - (obj - LEAD);
        if (ad < g_mar_adelanto_min) g_mar_adelanto_min = (float)ad; if (ad < 0) g_mar_atras++;
        if (M.t < obj) { ambiente(); mar_paso(); }
        else Sleep(1);
        if (g_pedido) { instantanea(); InterlockedExchange(&g_pedido, 0); }
    }
    InterlockedExchange(&g_mar_vivo, 0);
    return 0;
}

/* ---------------- texto en pantalla ---------------- */
static int g_ovl_hay, g_ovl_prev;   /* si en este cuadro (y en el anterior) se dibujó algo encima */
static inline void pintar(int px, int py, float al, int r, int g, int b) {   /* un color encima: en el cuadro (GDI) o en la capa de textos (gráfica) */
    if (al > 1) al = 1;
    if (g_gpu) {
        u32 c = OVL[py * VW + px]; float k = 1 - al;
        u32 R = (u32)((c & 255) * k + r * al), Gc = (u32)(((c >> 8) & 255) * k + g * al), B = (u32)(((c >> 16) & 255) * k + b * al), A = (u32)((c >> 24) * k + 255 * al);
        OVL[py * VW + px] = R | Gc << 8 | B << 16 | A << 24; g_ovl_hay = 1;
    } else {
        u32 c = PIX[py * VW + px]; int r0 = (c >> 16) & 255, g0 = (c >> 8) & 255, b0 = c & 255;
        PIX[py * VW + px] = ((r0 + (int)((r - r0) * al)) << 16) | ((g0 + (int)((g - g0) * al)) << 8) | (b0 + (int)((b - b0) * al));
    }
}
static void texto(const char *s, int x, int y, float a) {
    while (*s) {
        u32 cp = (u8)*s++;
        if (cp >= 0xE0) { cp = ((cp & 15) << 12) | ((s[0] & 63) << 6) | (s[1] & 63); s += 2; } else if (cp >= 0xC0) { cp = ((cp & 31) << 6) | (s[0] & 63); s += 1; }
        int g = 0; for (int i = 0; i < FUENTE_N; i++) if (fuente_cp[i] == cp) { g = i; break; }
        for (int yy = 0; yy < FUENTE_ALTO; yy++) for (int xx = 0; xx < FUENTE_ANCHO; xx++) {
            int px = x + xx, py = y + yy; if (px < 0 || py < 0 || px >= VW || py >= VH) continue;
            float al = fuente_alfa[g][yy * FUENTE_ANCHO + xx] / 255.0f * a; if (al <= 0) continue;
            pintar(px, py, al, 190, 205, 220);
        }
        x += FUENTE_ANCHO;
    }
}
static char g_info[7][180]; static char g_aviso[64]; static double g_aviso_t = -10;

/* ---------------- el panel de la marea (tecla M): el día de hoy según el SHOA ---------------- */
static int g_panel;
static void mezclar(int x, int y, float a, int r, int g, int b) {
    if (x < 0 || y < 0 || x >= VW || y >= VH || a <= 0) return;
    pintar(x, y, a, r, g, b);
}
static void linea_marea(char *s, double ahora) {   /* "1,12 m y bajando · bajamar a las 19:19 (0,46 m)" */
    extremo e[4]; int of, n = marea_extremos(ahora, ahora + 0.6, e, 4, &of);
    char m[16], h[8], m2[16]; float z = marea_nivel(ahora); marea_metros(z, m);
    if (n > 0) { marea_hora(e[0].t, h); marea_metros(e[0].h, m2); wsprintfA(s, "%s y %s · %s a las %s (%s)", m, e[0].pleamar ? "subiendo" : "bajando", e[0].pleamar ? "pleamar" : "bajamar", h, m2); }
    else wsprintfA(s, "%s", m);
}
static void panel_marea(float alfa) {
    const int W = 600, H = 110, x0 = (int)(0.028f * VW) + 14, y0 = (int)(0.035f * VH) + 84;
    const float TOPE = 2.4f;   /* la escala: de 0 a 2,4 m sobre el nivel de reducción */
    double ahora = marea_ahora(), d0 = ahora - 6 / 24.0, d1 = ahora + 18 / 24.0;
    for (int y = y0 - 54; y < y0 + H + 82; y++) for (int x = x0 - 14; x < x0 + W + 14; x++) mezclar(x, y, 0.42f * alfa, 0, 0, 0);   /* sombra para que se lea */
    extremo e[12]; int of, n = marea_extremos(d0, d1, e, 12, &of);
    char s[180], h[8], m[16];
    texto(of ? "marea en Constitución · tabla oficial del SHOA" : "marea en Constitución · constantes armónicas del SHOA", x0, y0 - 46, alfa);
    /* la curva de 6 horas atrás a 18 adelante */
    float yp = 0;
    for (int x = 0; x < W; x++) {
        double d = d0 + (d1 - d0) * x / (W - 1); float y = y0 + H - marea_nivel(d) / TOPE * H, a = alfa * (d < ahora ? 0.4f : 0.85f);
        if (x > 0) { int ya = (int)floorf(fminf(y, yp)), yb = (int)ceilf(fmaxf(y, yp)); for (int yy = ya; yy <= yb; yy++) mezclar(x0 + x, yy, a, 190, 205, 220); }
        yp = y;
    }
    for (int x = 0; x < W; x += 4) mezclar(x0 + x, y0 + H - (int)(MAREA_Z0 / TOPE * H), alfa * 0.25f, 190, 205, 220);   /* el nivel medio, punteado */
    for (int i = 0; i < n; i++) {   /* cada pleamar y bajamar con su hora en tu reloj */
        int xe = (int)((e[i].t - d0) / (d1 - d0) * (W - 1)), ye = (int)(y0 + H - e[i].h / TOPE * H);
        marea_hora(e[i].t, h); marea_metros(e[i].h, m); wsprintfA(s, "%s %s", h, m);
        int tx = x0 + xe - 6 * FUENTE_ANCHO, ancho = lstrlenA(s) * FUENTE_ANCHO;
        if (tx < x0 - 8) tx = x0 - 8; if (tx > x0 + W + 8 - ancho) tx = x0 + W + 8 - ancho;
        texto(s, tx, e[i].pleamar ? ye - 24 : ye + 6, alfa * 0.85f);
    }
    {   /* ahora */
        int xa = (int)((ahora - d0) / (d1 - d0) * (W - 1)); float ya = y0 + H - marea_nivel(ahora) / TOPE * H;
        for (int yy = y0 - 6; yy < y0 + H + 6; yy += 3) mezclar(x0 + xa, yy, alfa * 0.35f, 190, 205, 220);
        for (int dy = -4; dy <= 4; dy++) for (int dx = -4; dx <= 4; dx++) { float r = sqrtf((float)(dx * dx + dy * dy)); mezclar(x0 + xa + dx, (int)ya + dy, alfa * clampf(4.5f - r, 0, 1), 240, 244, 248); }
    }
    char l[140]; linea_marea(l, ahora); wsprintfA(s, "ahora %s", l); texto(s, x0, y0 + H + 30, alfa);
    float hs = M.hs_sw * M.a_sw; int dec = (int)(hs * 10 + 0.5f);
    wsprintfA(s, "marejada del suroeste %d,%d m · viento %d km/h%s", dec / 10, dec % 10, (int)(S.U * 3.6f + 0.5f),
              g_tmar_estado == 1 ? " · pidiendo la tabla al SHOA…" : g_tmar_estado == 2 ? " · tabla recién bajada de shoa.cl" : "");
    texto(s, x0, y0 + H + 54, alfa * 0.75f);
}

/* ---------------- plataforma Windows ---------------- */
static volatile LONG g_salir, g_audio_fin, g_tecla; static volatile float g_vol_db = -6, g_brillo = 0.85f, g_fundido_salida = 4;
static HWND g_hwnd; static char g_modo[96] = "iniciando audio"; static int g_marco = 1;
static const GUID C_ENUM = { 0xBCDE0395, 0xE52F, 0x467C, { 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E } };
static const GUID I_ENUM = { 0xA95664D2, 0x9614, 0x4F35, { 0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6 } };
static const GUID I_CLIENT = { 0x1CB9AD4C, 0xDBFA, 0x4C32, { 0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2 } };
static const GUID I_RENDER = { 0xF294ACFC, 0x3146, 0x4483, { 0xA7, 0xBF, 0xAD, 0xDC, 0xA7, 0xC2, 0x60, 0xE2 } };

static void aplicar_tecla(void) {
    LONG t = InterlockedExchange(&g_tecla, 0);
    if (t == 'V') S.ventana = !S.ventana;
    else if (t == 'R') S.lluvia_modo = (S.lluvia_modo + 1) % 3;
    else if (t == 'P') S.tv_obj = S.tv_obj > 0.5f ? 0 : 1;
}
static int sesion_audio(float *tmp) {
    IMMDeviceEnumerator *en = 0; IMMDevice *dev = 0; IAudioClient *cl = 0; IAudioRenderClient *rc = 0;
    HANDLE ev = CreateEventW(0, FALSE, FALSE, 0); int ret = 1;
    WAVEFORMATEX f = { WAVE_FORMAT_IEEE_FLOAT, 2, FS, FS * 8, 8, 32, 0 };
    if (FAILED(CoCreateInstance(&C_ENUM, 0, CLSCTX_ALL, &I_ENUM, (void **)&en))) goto fin;
    if (FAILED(IMMDeviceEnumerator_GetDefaultAudioEndpoint(en, eRender, eConsole, &dev))) goto fin;
    if (FAILED(IMMDevice_Activate(dev, &I_CLIENT, CLSCTX_ALL, 0, (void **)&cl))) goto fin;
    if (FAILED(IAudioClient_Initialize(cl, AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK | 0x80000000 | 0x08000000, 400000, 0, &f, 0))) goto fin;
    UINT32 nbuf = 0; IAudioClient_GetBufferSize(cl, &nbuf); IAudioClient_SetEventHandle(cl, ev);
    if (FAILED(IAudioClient_GetService(cl, &I_RENDER, (void **)&rc))) goto fin;
    wsprintfA(g_modo, "salida: WASAPI, 48 kHz flotante, búfer de %u muestras", nbuf);
    IAudioClient_Start(cl);
    int apagando = 0;
    for (;;) {
        if (WaitForSingleObject(ev, 2000) != WAIT_OBJECT_0) break;
        UINT32 pad = 0; if (FAILED(IAudioClient_GetCurrentPadding(cl, &pad))) break;
        UINT32 n = nbuf - pad; if (!n) continue;
        BYTE *d; if (FAILED(IAudioRenderClient_GetBuffer(rc, n, &d))) break;
        S.vol_obj = powf(10, g_vol_db / 20); aplicar_tecla();
        if (g_salir && !apagando) { apagando = 1; S.fundido_paso = -1.0f / (g_fundido_salida * FS); }
        sint_generar(tmp, n); CopyMemory(d, tmp, n * 8); IAudioRenderClient_ReleaseBuffer(rc, n, 0);
        if (apagando && S.fundido <= 0) { Sleep(150); ret = 0; break; }
    }
    IAudioClient_Stop(cl);
fin:
    if (rc) IAudioRenderClient_Release(rc); if (cl) IAudioClient_Release(cl); if (dev) IMMDevice_Release(dev); if (en) IMMDeviceEnumerator_Release(en);
    CloseHandle(ev); return ret;
}
static DWORD WINAPI hilo_audio(void *p) {
    CoInitializeEx(0, COINIT_MULTITHREADED); sin_desnormales();
    DWORD idx = 0; HANDLE h = AvSetMmThreadCharacteristicsW(L"Pro Audio", &idx);
    float *tmp = VirtualAlloc(0, FS * 8 * 2, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    while (sesion_audio(tmp) && !g_salir) { lstrcpyA(g_modo, "audio: reconectando…"); Sleep(1000); }
    if (h) AvRevertMmThreadCharacteristics(h);
    InterlockedExchange(&g_audio_fin, 1); return 0;
}

static void aviso(const char *s, double t) { lstrcpynA(g_aviso, s, 63); g_aviso_t = t; }
static double g_t0; static LARGE_INTEGER g_qf;
static double ahora(void) { LARGE_INTEGER c; QueryPerformanceCounter(&c); return (double)c.QuadPart / g_qf.QuadPart - g_t0; }

static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    char b[64]; static const char *ll[3] = { "lluvia: va y viene", "lluvia: apagada", "lluvia: siempre" };
    switch (m) {
    case WM_CLOSE: InterlockedExchange(&g_salir, 1); aviso("apagando…", ahora()); return 0;
    case WM_SETCURSOR: SetCursor(0); return 1;
    case WM_ERASEBKGND: return 1;
    case WM_SIZE: InterlockedExchange(&g_gpu_tam, 1); return 0;
    case WM_KEYDOWN:
        if (w == VK_ESCAPE) { InterlockedExchange(&g_salir, 1); aviso("apagando…", ahora()); }
        else if (w == VK_UP || w == VK_DOWN) { float v = g_vol_db + (w == VK_UP ? 2 : -2); g_vol_db = v > 0 ? 0 : v < -36 ? -36 : v; wsprintfA(b, "volumen %d dB", (int)g_vol_db); aviso(b, ahora()); }
        else if (w == VK_LEFT || w == VK_RIGHT) { float v = g_brillo + (w == VK_RIGHT ? 0.1f : -0.1f); g_brillo = v > 1.3f ? 1.3f : v < 0.1f ? 0.1f : v; wsprintfA(b, "brillo %d %%", (int)(g_brillo * 100 + 0.5f)); aviso(b, ahora()); }
        else if (w == 'V') { InterlockedExchange(&g_tecla, 'V'); aviso(S.ventana ? "ventana cerrada" : "ventana entreabierta", ahora()); }
        else if (w == 'R') { InterlockedExchange(&g_tecla, 'R'); aviso(ll[(S.lluvia_modo + 1) % 3], ahora()); }
        else if (w == 'P') { InterlockedExchange(&g_tecla, 'P'); aviso(S.tv_obj > 0.5f ? "la play se apaga" : "la play se enciende", ahora()); }
        else if (w == 'F') { g_marco = !g_marco; aviso(g_marco ? "marco de la ventana" : "sin marco", ahora()); }
        else if (w == 'M') g_panel = !g_panel;
        else if (w == 'G') { g_via_postal = !g_via_postal; if (g_via_postal) g_luna_postal = 0; aviso(g_via_postal ? "la Vía Láctea en la ventana, en una noche sin luna" : "el cielo de verdad", ahora()); }
        else if (w == 'L') { g_luna_postal = !g_luna_postal; if (g_luna_postal) g_via_postal = 0; aviso(g_luna_postal ? "la luna en la ventana, con su fase de hoy" : "la luna de verdad: donde está ahora", ahora()); }
        return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
static void cpu_marca(char *s) {
    unsigned int r[12];
    for (int i = 0; i < 3; i++) __asm__ volatile("cpuid" : "=a"(r[4 * i]), "=b"(r[4 * i + 1]), "=c"(r[4 * i + 2]), "=d"(r[4 * i + 3]) : "a"(0x80000002 + i), "c"(0));
    CopyMemory(s, r, 48); s[48] = 0; char *p = s; while (*p == ' ') p++; if (p != s) lstrcpyA(s, p);
}
static void guardar_bmp_de(const wchar_t *nombre, const u32 *px, int w, int h) {
    DWORD wr; HANDLE fh = CreateFileW(nombre, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    BITMAPFILEHEADER bf = { 0x4D42, 54 + w * h * 4, 0, 0, 54 }; BITMAPINFOHEADER bi = { 40, w, -h, 1, 32, BI_RGB };
    WriteFile(fh, &bf, 14, &wr, 0); WriteFile(fh, &bi, 40, &wr, 0); WriteFile(fh, px, w * h * 4, &wr, 0); CloseHandle(fh);
}
static void guardar_bmp(const wchar_t *nombre) { guardar_bmp_de(nombre, PIX, VW, VH); }

/* el mar se forma: 90 s de oleaje entrando desde cero, tan rápido como dé el procesador */
static void formar_mar(void (*progreso)(float)) {
    double fin = M.t + 90; M.rampa = 0;
    while (M.t < fin) { M.rampa = suave((float)(M.t / 25)); ambiente(); mar_paso(); if (progreso && (M.pasos & 31) == 0) progreso((float)(1 - (fin - M.t) / 90)); }
    M.rampa = 1;
}
/* el mar guardado tenía la marea de otra hora: sube o baja hasta la de ahora antes de mostrarse */
static void ajustar_marea(void (*progreso)(float)) {
    float dz = marea_objetivo() - AMB.z_desde;
    if (fabsf(dz) < 0.02f) { AMB.ajuste = 1; return; }
    double dur = 15 + 40 * fabsf(dz), t0 = M.t;
    while (M.t < t0 + dur + 8) {
        AMB.ajuste = clampf((float)((M.t - t0) / dur), 0, 1); ambiente(); mar_paso();
        if (progreso && (M.pasos & 31) == 0) progreso((float)((M.t - t0) / (dur + 8)));
    }
    AMB.ajuste = 1;
}
static HDC g_dc; static BITMAPINFO g_bmi; static const char *g_prog_txt = "el mar se está formando…";
static void mostrar_progreso(float f) {
    MSG msg; while (PeekMessageW(&msg, 0, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    ZeroMemory(PIX, (size_t)VW * VH * 4);
    char s[96]; wsprintfA(s, "%s %d %%", g_prog_txt, (int)(f * 100)); texto(s, VW / 2 - 10 * lstrlenA(s) / 2 + 20, VH / 2 - 10, 0.8f);
    RECT cr; GetClientRect(g_hwnd, &cr); StretchDIBits(g_dc, 0, 0, cr.right, cr.bottom, 0, 0, VW, VH, PIX, &g_bmi, DIB_RGB_COLORS, SRCCOPY);
}

int WINAPI WinMain(HINSTANCE hi, HINSTANCE hp, LPSTR cmd, int show) {
    QueryPerformanceFrequency(&g_qf); g_t0 = ahora(); timeBeginPeriod(1); sin_desnormales();
    int ventana = 0; double limite = 8 * 3600, prueba = 0;
    for (char *c = cmd; *c; c++) {
        if (c[0] == '-' && c[1] == '-' && c[2] == 'v') ventana = 1;
        if (c[0] == '-' && c[1] == '-' && c[2] == 'm' && c[3] == 'u') g_vol_db = -60;   /* para probar sin despertar a nadie */
        if (c[0] == '-' && c[1] == '-' && c[2] == 'm' && c[3] == 'a') g_panel = 1;      /* abre con el panel de la marea */
        if (c[0] == '-' && c[1] == '-' && c[2] == 's') { char *q = c; while (*q && *q != ' ') q++; while (*q == ' ') q++; limite = 0; while (*q >= '0' && *q <= '9') limite = limite * 10 + (*q++ - '0'); }
        if (c[0] == '-' && c[1] == '-' && c[2] == 'p') { char *q = c; while (*q && *q != ' ') q++; while (*q == ' ') q++; while (*q >= '0' && *q <= '9') prueba = prueba * 10 + (*q++ - '0'); }
    }
    if (prueba > 0 && getenv("DESFASE_H")) g_marea_desfase = atof(getenv("DESFASE_H")) / 24;   /* para probar otra hora de la marea */
    SYSTEMTIME st; GetLocalTime(&st); float hora = st.wHour + st.wMinute / 60.0f;
    LARGE_INTEGER tsc; QueryPerformanceCounter(&tsc);
    u32 semilla = (u32)__builtin_ia32_rdtsc() ^ (u32)tsc.QuadPart ^ (st.wMilliseconds * 2654435761u);
    SYSTEM_INFO si; GetSystemInfo(&si); int nh = (int)si.dwNumberOfProcessors; if (nh < 1) nh = 1; if (nh > MAXHIL) nh = MAXHIL;
    static piscina pm; piscina_crear(&pm, nh, THREAD_PRIORITY_ABOVE_NORMAL); piscina_crear(&g_pv, getenv("HV") ? atoi(getenv("HV")) : nh, 0);
    hw_iniciar();
    marea_iniciar(1);
    mar_iniciar(&pm, semilla);
    u32 r2 = semilla ^ 0x51ED270B; r2 |= 1;
    /* la marejada de esta noche, del clima de olas frente a Constitución (Atlas de Oleaje de Chile, nodo 9): en aguas profundas la
       mitad de las noches de invierno pasa de 2,4 m y una de cada diez de 3,1 m, con 13 s y del suroeste. Al borde de la grilla, en
       7,6 m de fondo, llega un 5 % más baja por la refracción; sobre 3,3 m ya reventaría antes de entrar. Encima, el mar de viento. */
    float hs = clampf(0.95f * 2.4f * expf(0.23f * gauss(&r2)), 1.4f, 3.3f), tp = clampf(13.2f + 1.3f * gauss(&r2), 10.5f, 16.5f), dir = uni(&r2, 0.18f, 0.40f);
    if (prueba > 0 && getenv("HS")) hs = (float)atof(getenv("HS"));   /* para medir con una marejada dada */
    sint_iniciar(semilla, hora, reservar((size_t)TVN * 16 + (size_t)TVN * 16));
    mar_oleaje(hs, tp, dir, 0.55f, uni(&r2, 5.0f, 7.0f), S.R > 0.3f ? -0.55f : 0.35f);
    ambiente_iniciar();
    int cargado = prueba > 0 && getenv("NUEVO") ? 0 : cargar_mar();
    if (!cargado) mar_nivel_inicial(marea_objetivo());
    char marca[64]; cpu_marca(marca);
    const char *momento = hora < 6 ? "madrugada" : hora < 12 ? "mañana" : hora < 20 ? "tarde" : "noche";
    lstrcpyA(g_info[0], "pacífico de pellines 3");
    wsprintfA(g_info[1], "%s · %u hilos", marca, (unsigned)GetActiveProcessorCount(0xFFFF));
    lstrcpyA(g_info[2], "desde la cabaña sobre la laja, a 15 m sobre el mar y 110 m de la orilla, mirando al noroeste: el oleaje se resuelve en vivo");
    { int d = (int)(hs * 10 + 0.5f), t10 = (int)(tp * 10 + 0.5f);
      wsprintfA(g_info[3], "marejada del suroeste de %d,%d m y %d,%d s · semilla 0x%08X · %02d:%02d · %s", d / 10, d % 10, t10 / 10, t10 % 10, semilla, st.wHour, st.wMinute, momento); }
    { char l[140]; linea_marea(l, marea_ahora()); wsprintfA(g_info[4], "marea en Constitución según el SHOA: %s", l); }
    lstrcpyA(g_info[6], "↑↓ volumen · ←→ brillo · V ventana · R lluvia · P play · F marco · M marea · L luna · G vía láctea · Esc salir");

    if (prueba > 0) {   /* sin ventana: forma el mar, graba el sonido y guarda cuadros para revisar */
        int seg = (int)prueba; float *buf = reservar(FS * 8);
        vista_buffers(1440, 900); CU.mvis = 1; CU.marco = 1;
        if (getenv("LLUVIA")) { S.lluvia_modo = 2; S.R = S.Robj = 4; S.Rflujo = 4; }
        if (getenv("SOLO")) { const char *o = getenv("SOLO"); g_solo_mar = o[0] == 'm'; g_solo_viento = o[0] == 'v'; g_solo_lluvia = o[0] == 'l'; S.tv_obj = 0; S.tv_on = 0; }
        if (getenv("SECO")) { S.lluvia_modo = 1; S.R = S.Robj = 0; S.Rflujo = 0; S.lloviendo = 0; }
        if (getenv("PANEL")) g_panel = 1;
        if (getenv("SSH")) g_ssh = atoi(getenv("SSH")); if (getenv("SSV")) g_ssv = (float)atof(getenv("SSV"));   /* para medir cuánto cuesta cada muestra */
        /* con GPU=1 la gráfica arma el cuadro final a 1920 x 1200 sin ventana, y se guarda eso */
        u32 *gsal = 0; if (getenv("GPU") && gpu_iniciar(0, VW, VH, 1920, 1200)) { gsal = reservar((size_t)1920 * 1200 * 4); atlas_tele(); }
        if (getenv("TELE_T")) g_tele_t = (float)atof(getenv("TELE_T"));   /* para ver la tele en otro momento de su encendido */
        if (getenv("LUNA_POSTAL")) g_luna_postal = 1;
        if (getenv("VIA_POSTAL")) g_via_postal = 1;
        double tf0 = ahora(); if (!cargado) formar_mar(0); else ajustar_marea(0); double tforma = ahora() - tf0;
        S.t0 = M.t - LEAD;
        HANDLE fh = CreateFileW(L"prueba_pacifico.wav", GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0); DWORD wr;
        u32 datos = seg * FS * 4; u32 cab[11] = { 0x46464952, 36 + datos, 0x45564157, 0x20746D66, 16, 0x00020001, FS, FS * 4, 0x00100004, 0x61746164, datos };
        WriteFile(fh, cab, 44, &wr, 0);
        short *pcm = (short *)buf; double ta = 0, td = 0, tm = 0; int cuadros = 0; double tprev = M.t;
        static char dbg[32768]; int ndbg = 0;
        for (int s = 0; s < seg * 10; s++) {
            double t0 = ahora();
            while (M.t < S.t0 + (double)(S.muestra + FS / 10) / FS + LEAD) { ambiente(); mar_paso(); }
            tm += ahora() - t0; t0 = ahora();
            sint_generar(buf, FS / 10); ta += ahora() - t0;
            for (int i = 0; i < FS / 10 * 2; i++) { float x = buf[i]; pcm[i] = (short)(x > 1 ? 32767 : x < -1 ? -32767 : x * 32767); }
            WriteFile(fh, pcm, FS / 10 * 4, &wr, 0);
            instantanea();
            if (s % 10 == 0) astro_calcular((marea_ahora() + 10957.5) * 86400);   /* la luna y el sol de este momento */
            g_tele_t += 0.1f; g_tele_on = S.tv_on;
            float tvv = S.tv_rms * 12 * S.tv_on + 0.35f * S.tv_on;
            t0 = ahora(); if (g_gpu) gpu_abrir();
            dibujar(IT, (float)(IT - tprev), 0.85f, 0, S.fundido, tvv, clampf(S.R / 6, 0, 1), S.U / 10);
            if (g_gpu) { gpu_cerrar(); if (g_ovl_prev) ZeroMemory(OVL, (size_t)VW * VH * 4); g_ovl_hay = 0; }
            if (g_panel) panel_marea(1);
            if (g_gpu) { g_ovl_prev = g_ovl_hay; gpu_mostrar(g_ovl_hay); }
            td += ahora() - t0; cuadros++; tprev = IT;
            if (s % 10 == 0 && ndbg < 32500) ndbg += wsprintfA(dbg + ndbg, "t=%d mar_rms=%d/10000 rms=%d/10000 pot=%d R=%d U=%d\r\n", s / 10, (int)(S.mar_rms * 10000), (int)(S.rms * 10000), (int)S.debug_pot, (int)(S.R * 10), (int)(S.U * 10));
            if (s == 50 || s == 120 || s == 200 || s == seg * 10 - 1) {
                wchar_t nm[40]; wsprintfW(nm, L"pacifico_%03d.bmp", s);
                if (gsal && g_gpu) { gpu_leer(gsal); guardar_bmp_de(nm, gsal, 1920, 1200); } else guardar_bmp(nm);
            }
        }
        CloseHandle(fh);
        {   /* la marea, aparte: cómo quedó el nivel del agua */
            char mm[400]; int k = wsprintfA(mm, "nivel %d mm, objetivo %d mm, venía de %d mm; ajuste en %d ms; tabla %d extremos, estado %d\r\n", (int)(M.zeta0 * 1000),
                                            (int)(marea_objetivo() * 1000), (int)(AMB.z_desde * 1000), (int)(tforma * 1000), g_tmar ? g_tmar->n : 0, (int)g_tmar_estado);
            k += wsprintfA(mm + k, "luna: altura %d, a la derecha de la vista %d grados, fase %d %%, luz %d milésimas; sol: altura %d; adaptación del ojo %d %%\r\n",
                           (int)ASTRO.alt_luna, (int)(atan2f(ASTRO.luna[0], ASTRO.luna[2]) * 57.3f), (int)(ASTRO.fase * 100), (int)(ASTRO.luz * 1000), (int)ASTRO.alt_sol, (int)(CU.adapt * 100));
            fh = CreateFileW(L"prueba_pacifico_marea.txt", GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0); WriteFile(fh, mm, k, &wr, 0); WriteFile(fh, dbg, ndbg, &wr, 0); CloseHandle(fh);
        }
        char m[1100]; wsprintfA(m, "forma %d ms; mar %d ms; audio %d s en %d ms; dibujo %d cuadros, %d us por cuadro con %d hilos (prep %d, espuma %d, cielo %d, mar %d, detalles %d, final %d us); rdrand modo %d\r\n%s", (int)(tforma * 1000), (int)(tm * 1000), seg, (int)(ta * 1000), cuadros, (int)(td * 1e6 / cuadros), nh, (int)(g_tv[0] * 1e6 / cuadros), (int)(g_tv[1] * 1e6 / cuadros), (int)(g_tv[2] * 1e6 / cuadros), (int)(g_tv[3] * 1e6 / cuadros), (int)(g_tv[4] * 1e6 / cuadros), (int)(g_tv[5] * 1e6 / cuadros), g_hw_modo, dbg);
        fh = CreateFileW(L"prueba_pacifico.txt", GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0); WriteFile(fh, m, lstrlenA(m), &wr, 0); CloseHandle(fh);
        if (getenv("GUARDAR")) guardar_mar();
        return 0;
    }

    HMONITOR mon = MonitorFromPoint((POINT){ 0, 0 }, MONITOR_DEFAULTTOPRIMARY); MONITORINFO mi = { sizeof mi }; GetMonitorInfoW(mon, &mi); RECT r = mi.rcMonitor;
    int W = r.right - r.left, H = r.bottom - r.top; if (ventana) { W = 1280; H = 800; }
    /* enchufado, a la resolución de la pantalla: cada píxel calculado. A batería (donde el procesador anda fresco a propósito),
       a tres cuartos, y la gráfica la lleva a la pantalla */
    SYSTEM_POWER_STATUS sps; int enchufado = GetSystemPowerStatus(&sps) && sps.ACLineStatus == 1;
    if (enchufado) vista_buffers(W & ~3, H & ~3); else vista_buffers((W * 3 / 4) & ~3, (H * 3 / 4) & ~3);
    CU.mvis = 0.6f;
    WNDCLASSEXW wc = { sizeof wc, CS_OWNDC, proc, 0, 0, hi, LoadIconW(hi, MAKEINTRESOURCEW(1)), 0, (HBRUSH)GetStockObject(BLACK_BRUSH), 0, L"pacifico3", 0 };
    RegisterClassExW(&wc);
    g_hwnd = CreateWindowExW(0, L"pacifico3", L"Pacífico de Pellines 3", ventana ? WS_OVERLAPPEDWINDOW | WS_VISIBLE : WS_POPUP | WS_VISIBLE,
                             ventana ? CW_USEDEFAULT : r.left, ventana ? CW_USEDEFAULT : r.top, W, H, 0, 0, hi, 0);
    SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
    g_bmi = (BITMAPINFO){ { sizeof(BITMAPINFOHEADER), VW, -VH, 1, 32, BI_RGB } };
    g_dc = GetDC(g_hwnd); SetStretchBltMode(g_dc, HALFTONE); SetBrushOrgEx(g_dc, 0, 0, 0);
    if (!cargado) formar_mar(mostrar_progreso);
    else { g_prog_txt = "la marea se pone al día con el SHOA…"; ajustar_marea(mostrar_progreso); }
    S.t0 = M.t - LEAD;
    { RECT cr; GetClientRect(g_hwnd, &cr); if (!getenv("SIN_GPU") && gpu_iniciar(g_hwnd, VW, VH, cr.right, cr.bottom)) atlas_tele(); }   /* desde aquí muestra la gráfica, si puede */
    g_mar_vivo = 1; CreateThread(0, 0, hilo_mar, 0, 0, 0);
    CreateThread(0, 0, hilo_audio, 0, 0, 0);
    MSG msg; int vivo = 1; double tprev = M.t, tinicio = ahora(); int cuadros = 0; double tc = 0; float fps = 0; double ttot0 = ahora(); long ctot = 0;
    while (vivo) {
        while (PeekMessageW(&msg, 0, 0, 0, PM_REMOVE)) { if (msg.message == WM_QUIT) vivo = 0; TranslateMessage(&msg); DispatchMessageW(&msg); }
        double t = ahora() - tinicio;
        if (t > limite && !g_salir) { g_fundido_salida = limite >= 600 ? 60 : 2; InterlockedExchange(&g_salir, 1); }
        if (g_audio_fin) {
            InterlockedExchange(&g_mar_salir, 1); while (g_mar_vivo) Sleep(5);
            guardar_mar(); DestroyWindow(g_hwnd); g_audio_fin = 0;
        }
        { static u64 bits0; static double tb; if (t - tb >= 1) { g_hw_mbit = (float)((g_hw_bits - bits0) / 1e6 / (t - tb)); bits0 = g_hw_bits; tb = t; }
          const char *gr = g_gpu ? " · la gráfica hace el cielo y el resplandor" : "";
          if (g_hw_modo) wsprintfA(g_info[5], "%s · ruido térmico del silicio: %d,%d Mbit/s · %d cuadros/s%s", g_modo, (int)g_hw_mbit, (int)(g_hw_mbit * 10) % 10, (int)(fps + 0.5f), gr);
          else wsprintfA(g_info[5], "%s · %d cuadros/s%s", g_modo, (int)(fps + 0.5f), gr); }
        /* pide una copia del mar y espera a que el hilo del mar la deje lista */
        if (g_mar_vivo) { InterlockedExchange(&g_pedido, 1); double tw = ahora(); while (g_pedido && ahora() - tw < 0.05) Sleep(0); }
        { static double tastro = -10; if (t - tastro > 5) { tastro = t; astro_calcular((marea_ahora() + 10957.5) * 86400); } }   /* la luna y el sol de verdad, cada 5 s */
        {   /* la tele: cada vez que se enciende (al abrir la app o con la P) parte la secuencia de la Play sin disco */
            static float obj_prev = 1; static double tprev_tv; if (S.tv_obj > 0.5f && obj_prev <= 0.5f) g_tele_t = 0;
            obj_prev = S.tv_obj; g_tele_t += (float)(t - tprev_tv); tprev_tv = t; g_tele_on = S.tv_on;
        }
        float dt = clampf((float)(IT - tprev), 0, 0.25f); tprev = IT;
        CU.marco = g_marco;
        float tvv = S.tv_rms * 12 * S.tv_on + 0.35f * S.tv_on;
        int gpu = g_gpu; if (gpu) gpu_abrir();
        double td0 = ahora();
        dibujar(IT, dt, g_brillo, (float)(t / 28800.0), S.fundido, tvv, clampf(S.R / 6, 0, 1), S.U / 10);
        {   /* la calidad se ajusta sola para que el cuadro tome unos 45 ms: el procesador siempre lleno y la imagen fluida
               (con marejada grande o a batería baja el número de muestras; con margen, lo sube) */
            static const struct { int h; float v; } CAL[5] = { { 1, 0.65f }, { 1, 0.45f }, { 1, 0.33f }, { 2, 0.45f }, { 2, 0.3f } };
            static int cal = 2; static float tc_ema = 0.045f; static double tcal;
            tc_ema += ((float)(ahora() - td0) - tc_ema) * 0.08f;
            if (t - tcal > (t < 20 ? 1 : 2)) { tcal = t; if (tc_ema > 0.055f && cal > 0) cal--; else if (tc_ema < 0.036f && cal < 4) cal++; }
            g_ssh = CAL[cal].h; g_ssv = CAL[cal].v;
        }
        if (gpu) { gpu_cerrar(); if (g_ovl_prev) ZeroMemory(OVL, (size_t)VW * VH * 4); g_ovl_hay = 0; }
        int x0 = (int)(0.028f * VW) + 14;
        float ai = t < 12 ? 1 : t < 16 ? (float)(16 - t) / 4 : 0;
        if (ai > 0) for (int l = 0; l < 7; l++) texto(g_info[l], x0, (int)(0.915f * VH) - 16 - (7 - l) * 22, ai * (l == 0 ? 1 : 0.75f));
        if (g_panel) panel_marea(1);
        double ta2 = ahora(); if (ta2 - g_aviso_t < 2.5) texto(g_aviso, x0, (int)(0.035f * VH) + 12, (float)(ta2 - g_aviso_t < 2 ? 1 : (2.5 - (ta2 - g_aviso_t)) * 2));
        if (gpu) {
            g_ovl_prev = g_ovl_hay; gpu_mostrar(g_ovl_hay);   /* la gráfica pone el cielo y la ventana, escala, resplandece y espera la pantalla */
            /* la gráfica, también llena: si el cuadro completo se alarga más allá de ~55 ms, menos pasos en las nubes; si sobra, más */
            static double tprevc; static float tf_ema = 0.05f, tpasos; double ahc = ahora();
            if (tprevc > 0) tf_ema += ((float)(ahc - tprevc) - tf_ema) * 0.05f; tprevc = ahc;
            if (t - tpasos > 1.5) { tpasos = (float)t; if (tf_ema > 0.058f && G.pasos > 24) G.pasos -= 8; else if (tf_ema < 0.047f && G.pasos < 96) G.pasos += 8; }
        }
        else {
            RECT cr; GetClientRect(g_hwnd, &cr);
            StretchDIBits(g_dc, 0, 0, cr.right, cr.bottom, 0, 0, VW, VH, PIX, &g_bmi, DIB_RGB_COLORS, SRCCOPY);
            DwmFlush();
        }
        cuadros++; ctot++; if (t - tc >= 1) { fps = (float)(cuadros / (t - tc)); cuadros = 0; tc = t; }
    }
    {   /* un registro corto para revisar cómo anduvo */
        char m[300]; DWORD wr; double dur = ahora() - ttot0;
        int hs10 = (int)(M.hs_sw * M.a_sw * 10 + 0.5f);
        wsprintfA(m, "%d s, %d cuadros (%d por segundo); el mar quedó atrás del oído %d veces; adelanto mínimo %d ms; marejada %d,%d m; %s\r\n",
                  (int)dur, (int)ctot, (int)(ctot / (dur > 0 ? dur : 1)), (int)g_mar_atras, (int)(g_mar_adelanto_min * 1000), hs10 / 10, hs10 % 10,
                  g_gpu ? "mostró la gráfica (Direct3D 11)" : "mostró GDI");
        wchar_t r[MAX_PATH]; ruta_estado(r); lstrcpyW(r + lstrlenW(r) - 14, L"registro.txt");
        HANDLE fh = CreateFileW(r, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0); if (fh != INVALID_HANDLE_VALUE) { WriteFile(fh, m, lstrlenA(m), &wr, 0); CloseHandle(fh); }
    }
    SetThreadExecutionState(ES_CONTINUOUS);
    timeEndPeriod(1);
    ExitProcess(0);
}
