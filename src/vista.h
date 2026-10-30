/* La vista por la ventana, píxel a píxel, desde la cabaña de Pellines sobre la laja: 15 m sobre el mar y 110 m de la orilla.
   Cada columna de la pantalla es un rayo que baja por la arena, cruza la playa y recorre el mar simulado hasta el
   horizonte: una ola alta tapa lo que está detrás, como pasa mirando de verdad. El color sale de la física: el agua
   refleja el cielo y la luna según su inclinación real, la arena queda oscura y brillante donde el agua la mojó, el
   agua blanca aparece donde un frente reventó y la espuma que queda flotando se va con la corriente, con su dibujo
   pegado al agua. Más allá de la grilla sigue la misma marejada como suma de ondas hasta el horizonte. */

#include <immintrin.h>
/* la cabaña real, en Pellines, medida en la imagen satelital de 2026 (barra de 20 m = 101 px) y en el modelo de
   terreno Copernicus GLO-30: la laja está a unos 15 m sobre el mar; a 38 m del techo empieza la arena seca, a 103 m la mojada y a
   124 m la espuma. Los ojos, solo para la imagen: sentado junto a la ventana, 1,5 m sobre la laja. El sonido sigue con su propia
   geometría de mar.h, intacta (la orilla a 110 m también la confirma el satélite) */
#define OJO_VISTA 16.5f
/* la costa corre a 24,4° (su perpendicular hacia el mar, a 294,4°) y la pared de la ventana mira a 311°: la vista está girada
   16,6° hacia el norte respecto de la grilla del mar, así que la orilla queda más cerca a la izquierda y se aleja a la derecha */
#define GIRO_VISTA (16.6f * 3.14159265f / 180)
static float g_cg, g_sg;   /* coseno y seno del giro */
static inline void a_camara(float X, float Z, float *xc, float *zc) { *xc = X * g_cg - Z * g_sg; *zc = X * g_sg + Z * g_cg; }   /* de la grilla a la vista */
static inline void a_grilla(float xc, float zc, float *X, float *Z) { *X = xc * g_cg + zc * g_sg; *Z = zc * g_cg - xc * g_sg; }   /* de la vista a la grilla */
static int VW, VH, YH; static float FOC;
/* con la gráfica, el cielo lo hace ella con nubes de verdad: aquí solo se marca qué píxeles son cielo abierto */
static int g_nubes_gpu; static float *SKY; static u8 *CIELO; static volatile float g_mvis_gpu = -1;
static float *FB, *FB2, *DENS, *HAZE, *TVM; static u8 *MARCO; static u32 *PIX;
static float NUBE[256 * 256], GRANO[256 * 256]; static u32 vr = 0x9E3779B9;
typedef struct { float x, y, b, w, f; } estrella; static estrella E[500];
typedef struct { float x, y, r, vy, vida; int baja; } gota; static gota GO[420]; static int NGO;
typedef struct { float X, Y, Z, vx, vy, vz, vida, b; } rocio; static rocio RO[3000]; static int NRO; static LONG g_est_visto;
/* barcos de pesca en la noche: lanchas a pocos kilómetros y algún barco grande más afuera, solo sus luces y su reflejo */
typedef struct { float X, Z, v, h, fase; int tipo; } barco; static barco BAR[4]; static int NBAR;
/* la instantánea del mar que se dibuja: la copia el hilo del mar cuando la pido */
static float *IE, *IW, *IR, *IS, *IU, *IV, *IRE, *IDU, *IDV, *SUP;
static double IT; static float IZ0; static volatile LONG g_pedido;
static int LEJ[6];
/* la vista reparte su trabajo con su propio contador: el hilo del mar corre al mismo tiempo con el suyo */
static volatile LONG g_col;
static inline int tomar_v(int total) { LONG k = InterlockedIncrement(&g_col) - 1; return k < total ? (int)k : -1; }
#define POR_FILAS_V for (int _b; (_b = tomar_v((NZ + 3) / 4)) >= 0;) for (int j = _b * 4, _jf = j + 4 > NZ ? NZ : j + 4; j < _jf; j++)
static struct {
    double t; float dt, brillo, dis, mvis, mx, my, mr, mdx, mdy, mdz, nx, nz, cov, tv, lluvia, viento, wox, woz, rug; int marco;
    float mluz, adapt;   /* cuánta luz da la luna de verdad (fase, altura, aire) y cuánto se acostumbra el ojo a la oscuridad */
    float sol[3], disco[3], altsol;   /* el sol con que se dibuja la fase y el brillo del disco */
} CU = { .adapt = 1 };
static int g_luna_postal;   /* tecla L: la luna en la ventana, con la fase de hoy, aunque de verdad esté en otra parte del cielo */
/* tecla G: el centro de la Vía Láctea en la ventana, saliendo sobre el mar en una noche sin luna ni nubes. Por esta ventana, que mira
   al noroeste, de verdad solo pasa la parte norte de la galaxia (el Águila, el Cisne), débil y pegada al horizonte: el centro, en
   Sagitario, se pone por el suroeste. g_via es el giro de la vista a coordenadas galácticas para esa postal */
static int g_via_postal; static float g_via[9];
static void via_postal_preparar(void) {   /* el centro 12° a la derecha y 6° sobre el horizonte; la banda sube 25° hacia la izquierda */
    float az = 12 * PI / 180, al = 6 * PI / 180, inc = 25 * PI / 180, t[3];
    float d0[3] = { sinf(az) * cosf(al), sinf(al), cosf(az) * cosf(al) }, iz[3] = { -cosf(az), 0, sinf(az) }, ar[3] = { -sinf(al) * sinf(az), cosf(al), -sinf(al) * cosf(az) };
    for (int i = 0; i < 3; i++) t[i] = iz[i] * cosf(inc) + ar[i] * sinf(inc);
    float z[3] = { d0[1] * t[2] - d0[2] * t[1], d0[2] * t[0] - d0[0] * t[2], d0[0] * t[1] - d0[1] * t[0] };
    for (int i = 0; i < 3; i++) { g_via[i] = d0[i]; g_via[3 + i] = t[i]; g_via[6 + i] = z[i]; }
}
static float LUNA[3] = { 1.0f, 0.96f, 0.86f };   /* el tono de la luz de la luna: se pone naranja cerca del horizonte */
static void textura_bytes(void);

static inline float tex(const float *T, float u, float v) {
    int iu = (int)u, iv = (int)v; if (u < iu) iu--; if (v < iv) iv--;
    float fu = u - iu, fv = v - iv; int x0 = iu & 255, y0 = iv & 255, x1 = (x0 + 1) & 255, y1 = (y0 + 1) & 255;
    float a = T[y0 * 256 + x0], b = T[y0 * 256 + x1], c = T[y1 * 256 + x0], d = T[y1 * 256 + x1];
    return a + (b - a) * fu + (c - a + (a - b - c + d) * fu) * fv;
}
static void gen_fbm(float *dst, int octavas) {
    static float base[64 * 64];
    for (int i = 0; i < 64 * 64; i++) base[i] = uni(&vr, 0, 1);
    for (int y = 0; y < 256; y++) for (int x = 0; x < 256; x++) {
        float v = 0, amp = 0.5f, tot = 0;
        for (int o = 0; o < octavas; o++) {
            int esc = 64 >> o; if (esc < 2) break;
            float fx = (float)x / esc, fy = (float)y / esc; int ix = (int)fx, iy = (int)fy; float tx = suave(fx - ix), ty = suave(fy - iy); int m = 256 / esc;
            float a = base[(iy % m) * 64 + ix % m], b = base[(iy % m) * 64 + (ix + 1) % m], c = base[((iy + 1) % m) * 64 + ix % m], d = base[((iy + 1) % m) * 64 + (ix + 1) % m];
            v += amp * (a + (b - a) * tx + (c - a + (a - b - c + d) * tx) * ty); tot += amp; amp *= 0.5f;
        }
        dst[y * 256 + x] = v / tot;
    }
}

static void instantanea(void) {   /* la llama el hilo del mar entre dos pasos */
    size_t n = (size_t)NXP * NZP * 4;
    CopyMemory(IE, M.eta, n); CopyMemory(IW, M.W, n); CopyMemory(IR, M.rod, n); CopyMemory(IS, M.seco, n); CopyMemory(IU, M.u, n); CopyMemory(IV, M.v, n);
    CopyMemory(IRE, M.R, n); CopyMemory(IDU, M.du, n); CopyMemory(IDV, M.dv, n);
    IT = M.t; IZ0 = M.zeta0;
}

static void escena_iniciar(void) {
    gen_fbm(NUBE, 6); gen_fbm(GRANO, 5); textura_bytes();
    for (int i = 0; i < 500; i++) { E[i].x = uni(&vr, 0, 1); E[i].y = powf(uni(&vr, 0, 1), 1.4f); E[i].b = powf(uni(&vr, 0, 1), 6) * 0.8f + 0.02f; E[i].w = uni(&vr, 0.5f, 3); E[i].f = uni(&vr, 0, 6.28f); }
    NBAR = 3 + (uni(&vr, 0, 1) < 0.5f);
    for (int i = 0; i < NBAR; i++) {
        barco *b = &BAR[i]; b->tipo = i == 0;
        b->Z = b->tipo ? uni(&vr, 6000, 9000) : uni(&vr, 1300, 5000); b->X = uni(&vr, -0.45f, 0.45f) * b->Z;
        b->v = b->tipo ? uni(&vr, 3, 5) * (uni(&vr, 0, 1) < 0.5f ? -1 : 1) : uni(&vr, -0.8f, 0.8f); b->h = b->tipo ? 16 : uni(&vr, 4, 6); b->fase = uni(&vr, 0, 6.28f);
    }
    for (int y = 0; y < VH; y++) for (int x = 0; x < VW; x++) {
        int i = y * VW + x; float fx = (float)x / VW, fy = (float)y / VH;
        float lado = clampf(1 - fminf(fx, 1 - fx) / 0.2f, 0, 1), abajo = clampf((fy - 0.72f) / 0.2f, 0, 1), arriba = clampf(1 - fy / 0.12f, 0, 1);
        HAZE[i] = (lado * lado * 0.6f + abajo * abajo * 0.8f + arriba * 0.3f) * (0.5f + 0.9f * tex(GRANO, x * 0.6f, y * 0.6f));
        float dx = (fx - 0.23f) / 0.10f, dy = (fy - 0.66f) / 0.075f, q = fmaxf(fabsf(dx), fabsf(dy));
        TVM[i] = clampf(1.15f - q, 0, 1) * clampf(1.15f - q, 0, 1) + 0.25f / (1 + dx * dx * 0.1f + dy * dy * 0.1f);
        int bw = (int)(0.028f * VW), bt = (int)(0.035f * VH), sill = (int)(0.915f * VH), mull = (int)(0.009f * VW);
        u8 m = 0;
        if (x < bw || x >= VW - bw || y < bt || y >= sill || abs(x - VW / 2) < mull) m = 1;
        if (m && (x == bw || x == VW - bw - 1 || y == bt || abs(abs(x - VW / 2) - mull) <= 1)) m = 2;
        if (y >= sill && y < sill + 5) m = 3;
        MARCO[i] = m;
    }
    /* para el mar lejano, las componentes más grandes de la marejada */
    for (int k = 0; k < 6; k++) {
        int mejor = 0; float am = -1;
        for (int c = 0; c < M.nc; c++) { int usado = 0; for (int q = 0; q < k; q++) usado |= LEJ[q] == c; if (!usado && M.c[c].a > am) { am = M.c[c].a; mejor = c; } }
        LEJ[k] = mejor;
    }
}

static void fila_cielo(int y) {
    float e = (float)(YH - y) / VH + 0.006f, dist = 0.32f / e;
    float f = (float)y / YH, f2 = f * f;
    float zen[3] = { 0.003f, 0.004f, 0.009f }, hor[3] = { 0.012f, 0.015f, 0.024f };
    float hz = clampf(1 - e * 7, 0, 1);
    for (int x = 0; x < VW; x++) {
        int i = y * VW + x;
        float cx = ((float)x - VW * 0.5f) / (VW * 0.5f) * dist * 0.8f + CU.nx, cz = dist + CU.nz;
        float d = 0.65f * tex(NUBE, cx * 22, cz * 22) + 0.35f * tex(NUBE, cx * 57 + 91, cz * 30 + 17);
        float dens = suave((d - (1 - CU.cov)) / 0.22f); dens += (0.85f - dens) * hz * 0.6f;
        float dx = x - CU.mx, dy = y - CU.my, d2 = dx * dx + dy * dy, mr2 = CU.mr * CU.mr, halo = mr2 * 7 / (d2 + mr2 * 7), cerca = mr2 * 1.5f / (d2 + mr2 * 1.5f);
        float r = zen[0] + (hor[0] - zen[0]) * f2 + halo * 0.025f * LUNA[0], g = zen[1] + (hor[1] - zen[1]) * f2 + halo * 0.025f * LUNA[1], b = zen[2] + (hor[2] - zen[2]) * f2 + halo * 0.025f * LUNA[2];
        float borde = dens * (1 - dens) * 4, nl = 0.004f + halo * (0.05f + 0.30f * borde) + cerca * 0.25f * borde;
        float cr = 0.005f + nl * LUNA[0] * 0.8f, cg = 0.006f + nl * LUNA[1] * 0.8f, cb = 0.009f + nl * LUNA[2] * 0.85f;
        r = r * (1 - dens) + cr * dens; g = g * (1 - dens) + cg * dens; b = b * (1 - dens) + cb * dens;
        if (d2 < mr2 * 1.3f) { float ed = clampf((CU.mr - sqrtf(d2)) / 1.2f, 0, 1) * 1.5f * (1 - dens * 0.93f), limbo = 0.75f + 0.25f * sqrtf(clampf(1 - d2 / mr2, 0, 1)); r += ed * limbo * LUNA[0]; g += ed * limbo * LUNA[1]; b += ed * limbo * LUNA[2]; }
        DENS[i] = dens;
        FB[3 * i] = r * CU.dis; FB[3 * i + 1] = g * CU.dis; FB[3 * i + 2] = b * CU.dis;
        SKY[3 * i] = FB[3 * i]; SKY[3 * i + 1] = FB[3 * i + 1]; SKY[3 * i + 2] = FB[3 * i + 2]; CIELO[i] = y < YH;
    }
}

/* ---------------- el mar simulado ---------------- */
/* todo lo que el sombreado necesita de una celda, junto en 48 bytes: una lectura bilineal trae 4 bloques y no 36 líneas de caché.
   du y dv dicen de dónde viene el agua de la celda: el dibujo de la espuma se lee ahí y así viaja con ella */
typedef struct { float gx, gz, dep, w, rod, moj, hum, uc, vc, du, dv, resto; } celda;
static celda *CV;
/* la textura de ruido en bytes: valor, sus dos derivadas y un segundo ruido, 256 KB que caben en la caché */
static u32 TX[256 * 256];
static inline void tx4(float u, float v, float *o) {
    int iu = (int)u, iv = (int)v; if (u < iu) iu--; if (v < iv) iv--;
    float fu = u - iu, fv = v - iv; int x0 = iu & 255, y0 = iv & 255, x1 = (x0 + 1) & 255, y1 = (y0 + 1) & 255;
    u32 a = TX[y0 * 256 + x0], b = TX[y0 * 256 + x1], c = TX[y1 * 256 + x0], d = TX[y1 * 256 + x1];
    float wa = (1 - fu) * (1 - fv), wb = fu * (1 - fv), wc = (1 - fu) * fv, wd = fu * fv;
    for (int k = 0; k < 4; k++, a >>= 8, b >>= 8, c >>= 8, d >>= 8) o[k] = ((a & 255) * wa + (b & 255) * wb + (c & 255) * wc + (d & 255) * wd) * (1.0f / 255);
}
static inline float tx1(float u, float v) {   /* solo el valor */
    int iu = (int)u, iv = (int)v; if (u < iu) iu--; if (v < iv) iv--;
    float fu = u - iu, fv = v - iv; int x0 = iu & 255, y0 = iv & 255, x1 = (x0 + 1) & 255, y1 = (y0 + 1) & 255;
    float a = TX[y0 * 256 + x0] & 255, b = TX[y0 * 256 + x1] & 255, c = TX[y1 * 256 + x0] & 255, d = TX[y1 * 256 + x1] & 255;
    return (a + (b - a) * fu + (c - a + (a - b - c + d) * fu) * fv) * (1.0f / 255);
}
static void textura_bytes(void) {
    static float otro[256 * 256]; u32 r0 = vr; vr ^= 0xA5A5A5A5; gen_fbm(otro, 4); vr = r0 ^ 0x5A5A5A5A;
    for (int y = 0; y < 256; y++) for (int x = 0; x < 256; x++) {
        int i = y * 256 + x;
        float dx = GRANO[y * 256 + ((x + 1) & 255)] - GRANO[y * 256 + ((x + 255) & 255)], dy = GRANO[((y + 1) & 255) * 256 + x] - GRANO[((y + 255) & 255) * 256 + x];
        int R = (int)(GRANO[i] * 255 + 0.5f), G = (int)(clampf(dx * 4, -1, 1) * 127 + 128), B = (int)(clampf(dy * 4, -1, 1) * 127 + 128), A = (int)(otro[i] * 255 + 0.5f);
        TX[i] = (u32)R | (u32)G << 8 | (u32)B << 16 | (u32)A << 24;
    }
}

typedef struct { int c; float a, b, cc, d; } pesos;   /* bilineal sobre la grilla de celdas */
static inline void pesar(pesos *p, float X, float Z) {
    float x = X * (1 / DXM) + NX / 2 - 0.5f, z = (Z - ZPRIM) * (1 / DXM) - 0.5f;
    int i = (int)floorf(x), j = (int)floorf(z); float fx = x - i, fz = z - j;
    if (j < 0) { j = 0; fz = 0; } if (j > NZ - 2) { j = NZ - 2; fz = 1; }
    i %= NX; if (i < 0) i += NX;
    p->c = IX(j, i); p->a = (1 - fx) * (1 - fz); p->b = fx * (1 - fz); p->cc = (1 - fx) * fz; p->d = fx * fz;
}
static inline float L(const float *A, const pesos *p) { const float *q = A + p->c; return q[0] * p->a + q[1] * p->b + q[NXP] * p->cc + q[NXP + 1] * p->d; }
static inline void LC(celda *o, const pesos *p) {
    const float *a = (const float *)(CV + p->c), *b = (const float *)(CV + p->c + 1), *c = (const float *)(CV + p->c + NXP), *d = (const float *)(CV + p->c + NXP + 1);
    float *r = (float *)o; for (int k = 0; k < 12; k++) r[k] = a[k] * p->a + b[k] * p->b + c[k] * p->cc + d[k] * p->d;
}

static void fase_prep(int id, int n) {
    POR_FILAS_V {
        for (int i = 0; i < NX; i++) { int c = IX(j, i); float D = M.hb[c] + IE[c]; SUP[c] = D > 0.01f ? IE[c] : -M.hb[c]; }
        fila_periodica(SUP + IX(j, 0));
    }
}
static void fase_grad(int id, int n) {
    POR_FILAS_V {
        for (int i = 0; i < NX; i++) {
            int c = IX(j, i); celda *v = CV + c; float D = M.hb[c] + IE[c];
            if (D < 0.005f) {   /* en la arena seca: cuánto le falta al agua vecina para llegar, con signo negativo */
                float s = 0, n = 0; int vc[4] = { c - 1, c + 1, c - NXP, c + NXP };
                for (int q = 0; q < 4; q++) { float d = M.hb[vc[q]] + IE[vc[q]]; if (d > 0.005f) { s += IE[vc[q]]; n++; } }
                D = n > 0 ? s / n + M.hb[c] : -0.3f;
            }
            v->gx = (SUP[c + 1] - SUP[c - 1]) * (0.5f / DXM); v->gz = (SUP[c + NXP] - SUP[c - NXP]) * (0.5f / DXM); v->dep = D;
            v->w = IW[c]; v->rod = IR[c]; float sec = IS[c]; v->moj = sec < 200 ? expf(-sec / 20) : 0; v->hum = sec < 4000 ? expf(-sec / 400) : 0;
            v->uc = 0.5f * (IU[c - 1] + IU[c]); v->vc = 0.5f * (IV[c - NXP] + IV[c]);
            v->du = IDU[c] * DXM; v->dv = IDV[c] * DXM; v->resto = IRE[c];
        }
        celda *f = CV + IX(j, 0); f[-2] = f[NX - 2]; f[-1] = f[NX - 1]; f[NX] = f[0]; f[NX + 1] = f[1];
    }
}

static inline void cielo_reflejado(float ry, float *r, float *g, float *b) {
    float ty = clampf(ry * 3, 0, 1);
    float hor0 = 0.012f + 0.010f * CU.mvis, hor1 = 0.016f + 0.013f * CU.mvis, hor2 = 0.026f + 0.022f * CU.mvis;
    *r = hor0 + (0.004f - hor0) * ty; *g = hor1 + (0.005f - hor1) * ty; *b = hor2 + (0.010f - hor2) * ty;
}

/* la arena no es pareja: manchones más claros y más oscuros, franjas a lo largo de la playa que deja el viento (así se ve en la
   imagen satelital) y las huellas de una camioneta que pasó por la playa */
static inline float arena_alb(float X, float Z) {
    float a = 0.84f + 0.36f * (tex(NUBE, X * 0.045f + 5, Z * 0.11f + 31) - 0.5f) + 0.07f * sinf(Z * 0.83f + 2.5f * tex(NUBE, X * 0.02f, Z * 0.05f + 9));
    float zt = 60 + 4 * sinf(X * 0.021f + 1.3f) + 1.5f * sinf(X * 0.067f + 0.4f), dt = fabsf(fabsf(Z - zt) - 0.8f);
    return a * (1 - 0.3f * expf(-dt * dt * 40) * (0.6f + 0.4f * tx1(X * 2.1f, Z * 2.1f)));
}
static void sombrear(float X, float Z, float Hs, const pesos *p, float *out) {
    celda q; LC(&q, p);
    float luz = 0.12f + 0.88f * CU.mvis, D = q.dep, agua = 1;
    if (D < 0.08f) { float borde = tx1(X * 2.7f + 31, Z * 2.7f + 77) - 0.5f; agua = suave((D + borde * 0.05f + 0.005f) / 0.06f); }
    /* el picado del viento: olas de uno a tres metros que la grilla no alcanza a resolver */
    float det = CU.rug * agua / (1 + Z * Z * 0.00003f), mx = 0, mz = 0;
    if (D < 0.25f) det *= clampf(D / 0.25f, 0.15f, 1);   /* la lámina que sube por la arena es lisa: refleja como espejo */
    if (det > 0.002f) {
        float t1[4], t2[4];
        tx4(X * 2.2f + CU.wox, Z * 2.2f + CU.woz, t1); tx4(X * 5.5f - CU.wox * 1.7f + 40, Z * 5.5f + CU.woz * 1.3f, t2);
        mx = det * 0.5f * ((t1[1] - 0.5f) + 0.55f * (t2[1] - 0.5f)); mz = det * 0.5f * ((t1[2] - 0.5f) + 0.55f * (t2[2] - 0.5f));
    }
    float nx = -(q.gx + mx), ny = 1, nz = -(q.gz + mz), in = 1 / sqrtf(nx * nx + ny * ny + nz * nz); nx *= in; ny *= in; nz *= in;
    float vx = X, vy = Hs - OJO_VISTA, vz = Z, iv = 1 / sqrtf(vx * vx + vy * vy + vz * vz); vx *= iv; vy *= iv; vz *= iv;
    float vn = vx * nx + vy * ny + vz * nz, rx = vx - 2 * vn * nx, ry = vy - 2 * vn * ny, rz = vz - 2 * vn * nz;
    float sp = rx * CU.mdx + ry * CU.mdy + rz * CU.mdz; sp = sp > 0 ? sp : 0; float s8 = sp * sp; s8 *= s8; s8 *= s8; float s128 = s8 * s8; s128 *= s128; s128 *= s128; s128 *= s128;
    float fr = clampf(1 + vn, 0, 1), f5 = fr * fr; f5 = 0.02f + 0.98f * f5 * f5 * fr;
    float cr, cg, cb; cielo_reflejado(ry, &cr, &cg, &cb);
    float spec = (s128 * 2.0f + s8 * 0.04f) * luz;
    float r = 0.002f + f5 * cr + spec * LUNA[0], g = 0.005f + f5 * cg + spec * LUNA[1], b = 0.010f + f5 * cb + spec * LUNA[2];
    float ndl = clampf(nx * CU.mdx + ny * CU.mdy + nz * CU.mdz, 0, 1);
    if (agua < 0.999f) {   /* la arena oscura del Maule: seca, húmeda y recién mojada, que brilla con la luna */
        float gr = 0.8f + 0.4f * tx1(X * 3.1f, Z * 3.1f);
        /* la misma luz que la arena seca de más acá (así no hay costura en la berma), más oscura donde está húmeda */
        float sa = 0.024f * gr * arena_alb(X, Z) * (0.55f + 0.45f * CU.mvis) * (0.30f + 2.6f * ndl * CU.mvis + 0.45f * (1 - CU.mvis)) * (1 - 0.45f * q.hum);
        float sr = sa * 1.05f + q.moj * (0.6f * f5 * cr + spec * 0.9f * LUNA[0]), sg = sa * 0.97f + q.moj * (0.6f * f5 * cg + spec * 0.9f * LUNA[1]), sb = sa * 0.88f + q.moj * (0.6f * f5 * cb + spec * 0.9f * LUNA[2]);   /* la arena recién mojada brilla como espejo */
        r = sr + (r - sr) * agua; g = sg + (g - sg) * agua; b = sb + (b - sb) * agua;
    }
    /* la espuma: el agua blanca de una ola que reventó y lo que queda flotando cuando se apaga. Su dibujo está pegado al agua
       (se lee donde esa agua estaba): viaja con ella, se estira con la corriente y solo cambia cuando otra ola revienta encima */
    float Wv = q.w, fp = q.resto;
    /* la orilla: el agua que sube por la arena empuja una línea de espuma, y la que se retira deja burbujas que se apagan solas */
    float vel = sqrtf(q.uc * q.uc + q.vc * q.vc), orilla = D > 0.003f && D < 0.3f ? clampf((0.3f - D) / 0.22f, 0, 1) * clampf(vel / 1.1f, 0, 1) : 0;
    float burbujas = q.moj * (1 - agua);
    if (Wv > 0.004f || fp > 0.01f || orilla > 0.02f || burbujas > 0.05f) {
        float fresca = clampf(q.rod / T_RODILLO, 0, 1), ax = X + q.du, az = Z + q.dv, o1[4], o2[4];
        tx4(ax * 0.9f + 13, az * 0.9f, o1); tx4(ax * 3.3f + 71, az * 3.3f, o2);
        float ta = 0.5f + (o1[0] - 0.5f) * 1.25f, tb = 0.5f + (o2[0] - 0.5f) * 1.25f, tl = 0.5f + (o2[3] - 0.5f) * 1.25f;
        float hilo = 1 - fabsf(2 * tb - 1); hilo *= hilo;
        /* el encaje más fino, que solo se alcanza a ver en la espuma de más cerca */
        float cerca = clampf((300 - Z) / 160, 0, 1), fino = 0.5f;
        if (cerca > 0) { float o3 = tx1(ax * 9.5f + 5, az * 9.5f + 9); fino = 1 - fabsf(2 * o3 - 1); fino *= fino; }
        /* cuánta agua blanca hay decide qué fracción se ve blanca: tupida en el frente, con hoyos y jirones atrás */
        float cobertura = clampf(Wv, 0, 1.3f) / 1.3f, m = cobertura * 1.2f - 0.48f + (ta - 0.5f) * 1.25f + (hilo - 0.35f) * 0.65f + fresca * 0.3f + (fino - 0.45f) * 0.3f * cerca;
        float cw = suave(m / 0.14f) * clampf(Wv * 5, 0, 1);
        /* la que quedó flotando: encaje fino del mismo dibujo, que se adelgaza a medida que se deshace */
        float enc = 1 - fabsf(2 * tl - 1); enc *= enc; enc = 0.5f * enc + 0.3f * hilo + 0.2f * fino;
        float cf = suave((fp * 1.5f + (enc - 0.45f) * 1.0f - 0.36f) / 0.12f) * clampf(fp * 3.5f, 0, 1) * 0.85f * agua;
        float cs = suave((orilla * 1.3f + (fino - 0.5f) * 0.6f + (hilo - 0.4f) * 0.4f - 0.55f) / 0.12f) * orilla;
        /* el frente del agua que sube: una cinta blanca y tupida, más gruesa mientras más rápido sube */
        float frente = q.vc < -0.2f && D > 0.003f && D < 0.14f ? clampf((-q.vc - 0.2f) / 1.0f, 0, 1) * clampf((0.14f - D) / 0.1f, 0, 1) : 0;
        cs = fmaxf(cs, suave((frente * 1.6f + (fino - 0.5f) * 0.5f + (hilo - 0.4f) * 0.3f - 0.45f) / 0.1f) * frente);
        float cb = suave((fino - 0.74f) / 0.07f) * burbujas * 0.55f * cerca;
        float cov = fmaxf(fmaxf(cw, cf), fmaxf(cs, cb));
        if (cov > 0.003f) {
            float base = (0.12f + 0.85f * CU.mvis) * 0.8f + 0.035f;
            float grumo = clampf(0.55f + 0.7f * (ta - 0.5f) + 0.5f * (hilo - 0.4f), 0, 1);
            float fl;
            if (cw >= cf && cw >= cs && cw >= cb) fl = base * (0.28f + 0.55f * ndl + 0.4f * fresca) * (0.55f + 0.8f * grumo);   /* el agua blanca de la ola */
            else if (cs >= cf && cs >= cb) fl = base * (0.3f + 0.45f * ndl) * (0.7f + 0.4f * fino);                               /* la línea que sube por la arena */
            else if (cf >= cb) fl = base * (0.2f + 0.35f * ndl) * (0.6f + 0.6f * enc);                                            /* el encaje que flota */
            else fl = base * (0.16f + 0.3f * ndl);                                                                                /* las burbujas en la arena */
            r += (fl * 0.93f - r) * cov; g += (fl * 1.0f - g) * cov; b += (fl * 1.1f - b) * cov;
        }
    }
    /* la niebla y la lluvia se comen lo lejano */
    float nb = clampf(Z / 1600 + CU.lluvia * Z / 1200, 0, 0.85f), h0 = 0.012f + 0.010f * CU.mvis, h1 = 0.016f + 0.013f * CU.mvis, h2 = 0.026f + 0.022f * CU.mvis;
    out[0] = (r + (h0 - r) * nb) * CU.dis; out[1] = (g + (h1 - g) * nb) * CU.dis; out[2] = (b + (h2 - b) * nb) * CU.dis;
}

/* más allá de la grilla: la misma marejada como suma de ondas, hasta el horizonte */
static void sombrear_lejos(float X, float Z, float *out) {
    float t = (float)CU.t, hx = 0, hz = 0;
    for (int k = 0; k < 6; k++) {
        const componente *c = &M.c[LEJ[k]]; float zb = ZPRIM + (NZ - 0.5f) * DXM;
        float ph = c->kx * X + c->kz * (Z - zb) - c->w * t + c->ph, a = c->a * (c->viento ? M.a_ws : M.a_sw) * sinf(ph);
        hx -= a * c->kx; hz -= a * c->kz;
    }
    float det = CU.rug * 0.5f / (1 + Z * Z * 0.00003f), o[4]; tx4(X * 1.1f + CU.wox * 0.5f, Z * 1.1f + CU.woz * 0.5f, o);
    hx += det * 0.5f * (o[1] - 0.5f); hz += det * 0.5f * (o[2] - 0.5f);
    float nx = -hx, ny = 1, nz = -hz, in = 1 / sqrtf(nx * nx + ny * ny + nz * nz); nx *= in; ny *= in; nz *= in;
    float vx = X, vy = -OJO_VISTA, vz = Z, iv = 1 / sqrtf(vx * vx + vy * vy + vz * vz); vx *= iv; vy *= iv; vz *= iv;
    float vn = vx * nx + vy * ny + vz * nz, rx = vx - 2 * vn * nx, ry = vy - 2 * vn * ny, rz = vz - 2 * vn * nz;
    float sp = rx * CU.mdx + ry * CU.mdy + rz * CU.mdz; sp = sp > 0 ? sp : 0; float s8 = sp * sp; s8 *= s8; s8 *= s8; float s128 = s8 * s8; s128 *= s128; s128 *= s128; s128 *= s128;
    float fr = clampf(1 + vn, 0, 1), f5 = fr * fr; f5 = 0.02f + 0.98f * f5 * f5 * fr;
    float cr, cg, cb; cielo_reflejado(ry, &cr, &cg, &cb); float luz = 0.12f + 0.88f * CU.mvis, spec = (s128 * 2.0f + s8 * 0.04f) * luz;
    float r = 0.002f + f5 * cr + spec * LUNA[0], g = 0.005f + f5 * cg + spec * LUNA[1], b = 0.010f + f5 * cb + spec * LUNA[2];
    float nb = clampf(Z / 1600 + CU.lluvia * Z / 1200, 0, 0.92f), h0 = 0.012f + 0.010f * CU.mvis, h1 = 0.016f + 0.013f * CU.mvis, h2 = 0.026f + 0.022f * CU.mvis;
    out[0] = (r + (h0 - r) * nb) * CU.dis; out[1] = (g + (h1 - g) * nb) * CU.dis; out[2] = (b + (h2 - b) * nb) * CU.dis;
}

/* ---------------- la laja, el barranco y la playa ---------------- */
/* la cabaña está sobre una laja a 15 m: afuera de la ventana, un corredor angosto y el barranco casi vertical; al pie, la arena
   que el viento amontona contra la roca baja en rampa hasta la playa de arena oscura, ancha y casi plana, que llega a la berma, donde
   empieza la grilla del mar. Fran recuerda "todo lo demás es arena": la imagen satelital de 2026 muestra matorral al pie del
   barranco (creció después), aquí quedan solo unas matas sueltas. En metros de la grilla: Z perpendicular a la costa */
#define DUNA_DX 0.25f
#define DUNA_X0 (-56.0f)   /* con la vista girada al norte, a la derecha se ve mucha más playa que a la izquierda */
#define DUNA_NX 768        /* X de -56 a 136 m */
#define DUNA_NZ 340        /* Z de 0 a 85 m */
#define LAJA 15.0f
static float *DUH;    /* por punto: altura y sus dos pendientes */
static inline float matorral(float X, float Z) {   /* matas sueltas cerca del barranco: 1 en una mata, 0 en la arena */
    if (Z > 36) return 0;
    float m = tex(NUBE, X * 0.42f + 50, Z * 0.42f + 13) * 0.7f + tex(GRANO, X * 1.2f, Z * 1.2f + 7) * 0.3f;
    return suave((m - 0.6f) / 0.08f) * (1 - suave((Z - 26) / 9));
}
static float duna_f(float X, float Z) {
    float w = 2 * PI / LARGO;   /* la berma, igual que fondo() en el borde de la grilla */
    float berma = 3.14f + 0.05f * sinf(w * 29 * X + g_fase[3]) * sinf(0.13f * ZPRIM + g_fase[4]) + 0.04f * sinf(w * 57 * X + g_fase[5] + 0.21f * ZPRIM);
    float borde = 2.8f + 0.5f * sinf(0.23f * X + g_fase[5]) + 0.25f * sinf(0.61f * X + g_fase[6]), pie = 7.0f + 0.8f * sinf(0.17f * X + g_fase[4]);
    if (Z < borde) return LAJA;                                                             /* la laja y el corredor */
    if (Z < borde + 0.8f) return LAJA + (pie - LAJA) * suave((Z - borde) / 0.8f);           /* la pared del barranco */
    /* la rampa de arena contra el barranco y después la playa casi plana, a 3,5 m, que baja apenas hasta la berma */
    float z0 = borde + 0.8f, zr = 21 + 3 * sinf(0.05f * X + g_fase[7]), s = clampf((Z - z0) / (zr - z0), 0, 1);
    float t = clampf((Z - zr) / (ZPRIM - zr), 0, 1), h = Z < zr ? 3.5f + (pie - 3.5f) * (1 - s) * (1 - s) : 3.5f + (berma - 3.5f) * t;
    /* la arena no es lisa: ondulaciones suaves del viento y montoncitos sueltos */
    float irr = 0.10f * sinf(0.09f * X + 0.6f * sinf(0.05f * Z) + g_fase[6]) * sinf(0.11f * Z) + 0.28f * (tex(GRANO, X * 0.25f + 40, Z * 0.25f + 90) - 0.5f)
              + 0.22f * suave((tex(NUBE, X * 0.35f + 11, Z * 0.35f + 29) - 0.55f) / 0.15f);
    h += irr * suave((Z - z0) / 3) * (1 - suave((Z - ZPRIM + 4) / 4));
    return h + matorral(X, Z) * (0.5f + 0.8f * tex(NUBE, X * 0.12f + 70, Z * 0.12f));
}
/* las sombras de la luna sobre la arena: se recalculan solo cuando la luna se movió */
static float *DUS; static float g_mds[3] = { 9, 9, 9 };
static void fila_sombra(int j) {
    for (int i = 0; i < DUNA_NX; i++) {
        float X = DUNA_X0 + i * DUNA_DX, Z = j * DUNA_DX, luz = 1, s = 0.5f, hp = DUH[3 * (j * DUNA_NX + i)] + 0.05f;
        for (int k = 0; k < 22 && luz > 0.01f; k++) {
            float px = X + CU.mdx * s, pz = Z + CU.mdz * s; if (pz >= ZPRIM || pz < 0) break;
            float x = clampf((px - DUNA_X0) / DUNA_DX, 0, DUNA_NX - 1.001f), z = pz / DUNA_DX; int a = (int)x, b = (int)z; float fx = x - a, fz = z - b;
            const float *p = DUH + 3 * (b * DUNA_NX + a), *q = p + 3 * DUNA_NX;
            float hk = (p[0] + (p[3] - p[0]) * fx) * (1 - fz) + (q[0] + (q[3] - q[0]) * fx) * fz;
            luz = fminf(luz, clampf(0.5f + (hp + CU.mdy * s - hk) * 4 / s, 0, 1)); s *= 1.25f;
        }
        DUS[j * DUNA_NX + i] = luz;
    }
}
static void duna_preparar(void) {
    DUH = reservar((size_t)DUNA_NX * DUNA_NZ * 3 * 4); DUS = reservar((size_t)DUNA_NX * DUNA_NZ * 4);
    for (int j = 0; j < DUNA_NZ; j++) for (int i = 0; i < DUNA_NX; i++) {
        float X = DUNA_X0 + i * DUNA_DX, Z = j * DUNA_DX, e = 0.2f, *d = DUH + 3 * (j * DUNA_NX + i);
        d[0] = duna_f(X, Z); d[1] = (duna_f(X + e, Z) - duna_f(X - e, Z)) / (2 * e); d[2] = (duna_f(X, Z + e) - duna_f(X, Z - e)) / (2 * e);
    }
}
static inline void duna(float X, float Z, float *o) {   /* altura y pendientes, bilineal en la tabla */
    float x = clampf((X - DUNA_X0) / DUNA_DX, 0, DUNA_NX - 1.001f), z = clampf(Z / DUNA_DX, 0, DUNA_NZ - 1.001f);
    int i = (int)x, j = (int)z; float fx = x - i, fz = z - j; const float *a = DUH + 3 * (j * DUNA_NX + i), *c = a + 3 * DUNA_NX;
    for (int k = 0; k < 3; k++) o[k] = (a[k] + (a[k + 3] - a[k]) * fx) * (1 - fz) + (c[k] + (c[k + 3] - c[k]) * fx) * fz;
}
/* ---------------- la fogata en la arena seca, abajo a la derecha ---------------- */
static struct { float X, Z, H, brillo; } FUEGO;
static float g_llamarada;   /* el fuego se aviva cuando lo atizan */
static float g_atizo;       /* las chispas que saltan con el golpe del palo */
typedef struct { float X, Y, Z, vx, vy, vida, v0; } chispa; static chispa CH[200]; static int NCH;
static void sombrear_duna(float X, float Z, const float *dh, float *out) {   /* arena seca y oscura, con las ondas chicas del viento */
    float o[4], o2[4]; tx4(X * 7.0f + 17, Z * 3.0f + 5, o); tx4(X * 21.0f + 3, Z * 9.0f + 61, o2);
    float rz = 0.5f + 0.5f / (1 + Z * 0.05f);   /* las ondas chicas se ven de cerca */
    float nx = -(dh[1] + (o[1] - 0.5f) * 0.5f + (o2[1] - 0.5f) * 0.4f * rz), ny = 1, nz = -(dh[2] + (o[2] - 0.5f) * 0.5f + (o2[2] - 0.5f) * 0.4f * rz);
    float in = 1 / sqrtf(nx * nx + ny * ny + nz * nz); nx *= in; ny *= in; nz *= in;
    float ndl = clampf(nx * CU.mdx + ny * CU.mdy + nz * CU.mdz, 0, 1), gr = 0.75f + 0.5f * tx1(X * 0.9f + 11, Z * 0.9f) * (0.8f + 0.4f * tx1(X * 3.1f, Z * 3.1f));
    float x = clampf((X - DUNA_X0) / DUNA_DX, 0, DUNA_NX - 1.001f), z = clampf(Z / DUNA_DX, 0, DUNA_NZ - 1.001f); int a = (int)x, b = (int)z; float fx = x - a, fz = z - b;
    const float *p = DUS + b * DUNA_NX + a, *q = p + DUNA_NX; float luz = (p[0] + (p[1] - p[0]) * fx) * (1 - fz) + (q[0] + (q[1] - q[0]) * fx) * fz;
    /* la luna baja raspa la arena: cada loma tiene su cara clara, su cara oscura y su sombra larga */
    float sa = 0.024f * gr * arena_alb(X, Z) * (0.55f + 0.45f * CU.mvis) * (0.30f + 2.6f * ndl * luz * CU.mvis + 0.45f * (1 - CU.mvis));
    float ar = sa * 1.05f, ag = sa * 0.97f, ab = sa * 0.88f;
    {   /* con la luna baja de frente, la arena seca brilla apenas en el camino hacia ella (los granos y las ondas chicas la reflejan) */
        float vx = X, vy = dh[0] - OJO_VISTA, vz = Z, iv = 1 / sqrtf(vx * vx + vy * vy + vz * vz); vx *= iv; vy *= iv; vz *= iv;
        float vn = vx * nx + vy * ny + vz * nz, sp = fmaxf(0, (vx - 2 * vn * nx) * CU.mdx + (vy - 2 * vn * ny) * CU.mdy + (vz - 2 * vn * nz) * CU.mdz);
        float s4 = sp * sp; s4 *= s4; float bl = (s4 * s4 * 0.35f + s4 * 0.08f) * CU.mvis * luz * gr * 0.02f;
        ar += bl * LUNA[0]; ag += bl * LUNA[1]; ab += bl * LUNA[2];
    }
    float veg = matorral(X, Z);
    if (veg > 0.01f) {   /* el matorral: matas oscuras, verde gris, con lo de arriba plateado por la luna y huecos negros entre las hojas */
        float hoja = tx1(X * 3.7f + 9, Z * 3.7f + 27), hueco = 0.3f + 0.7f * hoja * hoja;
        float sv = 0.0085f * hueco * (0.45f + 0.55f * CU.mvis) * (0.12f + 2.4f * ndl * luz * CU.mvis + 0.25f * (1 - CU.mvis));
        ar += (sv * 0.78f - ar) * veg; ag += (sv * 0.95f - ag) * veg; ab += (sv * 0.80f - ab) * veg;
    }
    {   /* la fogata alumbra la arena de alrededor, con su parpadeo */
        float fx = FUEGO.X - X, fz = FUEGO.Z - Z, d2 = fx * fx + fz * fz;
        if (d2 < 600) {
            float fy = FUEGO.H + 0.5f - dh[0], dd = d2 + fy * fy, id = 1 / sqrtf(dd), nf = fmaxf(0, (nx * fx + ny * fy + nz * fz) * id);
            float I = 1.1f * FUEGO.brillo * nf / (dd + 0.6f) * gr; ar += I; ag += I * 0.48f; ab += I * 0.16f;
        }
    }
    float nb = clampf(Z / 1600 + CU.lluvia * Z / 1200, 0, 0.85f), h0 = 0.012f + 0.010f * CU.mvis, h1 = 0.016f + 0.013f * CU.mvis, h2 = 0.026f + 0.022f * CU.mvis;
    out[0] = (ar + (h0 - ar) * nb) * CU.dis; out[1] = (ag + (h1 - ag) * nb) * CU.dis; out[2] = (ab + (h2 - ab) * nb) * CU.dis;
}

/* cada muestra del rayo cubre un tramo de la pantalla: lo reparto entre las filas que toca, para que nada parpadee ni se escalone */
#define MAXVH 2400
static inline void repartir(float (*acc)[4], float a, float b, const float *c, float w) {
    for (int r = (int)a; r < b && r < VH; r++) {
        float o = (fminf(b, r + 1.0f) - fmaxf(a, (float)r)) * w; if (o <= 0) continue;
        acc[r][0] += c[0] * o; acc[r][1] += c[1] * o; acc[r][2] += c[2] * o; acc[r][3] += o;
    }
}
static int g_ssh = 2;   /* rayos por columna */
static float g_ssv = 0.3f;   /* filas que avanza cada muestra del mar: menos es más fino */
static void columna(int x) {
    float acc[MAXVH][4];
    for (int y = YH; y < VH; y++) acc[y][0] = acc[y][1] = acc[y][2] = acc[y][3] = 0;
    const float ZF = ZPRIM + (NZ - 3) * DXM, wr = 1.0f / g_ssh;
    for (int sub = 0; sub < g_ssh; sub++) {
        u32 hs = (u32)(x * 8 + sub) * 2654435761u; hs ^= hs >> 15; float jit = (hs & 1023) / 1024.0f;
        float dx = (x + (sub + 0.25f + 0.5f * jit) / g_ssh - VW * 0.5f) / FOC, gx = dx * g_cg + g_sg, gz = g_cg - dx * g_sg;   /* el rayo en la grilla, por metro de profundidad */
        /* Z es la profundidad en la vista; parte donde el borde de abajo de la pantalla toca lo más alto pasado el barranco (las matas, ~6,5 m) */
        float Z = 0.8f * FOC * (OJO_VISTA - 6.5f) / (VH - YH), ytop = (float)VH, Zp = Z, Hp = 6, yp = 1e9f;
        while (Z * gz < ZF && ytop > YH) {
            float X = gx * Z, Zg = gz * Z, Hs, dh[3]; pesos p; int arena = Zg < ZPRIM;
            if (arena) { duna(X, Zg, dh); Hs = dh[0]; }
            else {
                pesar(&p, X, Zg); Hs = L(SUP, &p);
                float wv = L(IW, &p);
                if (wv > 0.03f) {   /* el agua blanca recién reventada tiene cuerpo: un rodillo de espuma con el borde de arriba irregular */
                    /* el grosor cambia en metros, no de un rayo al otro: así el borde de arriba queda irregular pero sin púas */
                    float fr = clampf(L(IR, &p) / T_RODILLO, 0, 1), n = tx1((X + L(IDU, &p) * DXM) * 0.45f + 5, (Zg + L(IDV, &p) * DXM) * 0.45f);
                    Hs += clampf(wv, 0, 1.2f) * (0.15f + 0.75f * fr) * (0.35f + 0.8f * n);
                }
            }
            float ys = YH + FOC * (OJO_VISTA - Hs) / Z;
            if (ys < ytop) {
                float lo = fmaxf(ys, (float)YH), c[3];
                if (ytop - lo <= 1.25f) { if (arena) sombrear_duna(X, Zg, dh, c); else sombrear(X, Zg, Hs, &p, c); repartir(acc, lo, ytop, c, wr); }
                else for (float b = ytop; b > lo;) {   /* una cara empinada o la arena de cerca: cada tramo con su propio punto */
                    float a = fmaxf(b - 1, lo), ym = 0.5f * (a + b), w = yp > ys + 1e-3f ? clampf((yp - ym) / (yp - ys), 0, 1) : 1;
                    float Zk = Zp + (Z - Zp) * w, Hk = Hp + (Hs - Hp) * w, Xk = gx * Zk, Zkg = gz * Zk;
                    if (Zkg < ZPRIM) { float dk[3]; duna(Xk, Zkg, dk); sombrear_duna(Xk, Zkg, dk, c); } else { pesos pk; pesar(&pk, Xk, Zkg); sombrear(Xk, Zkg, Hk, &pk, c); }
                    repartir(acc, a, b, c, wr); b = a;
                }
                ytop = lo;
            }
            Zp = Z; Hp = Hs; yp = ys;
            float dz = (arena ? 0.9f : g_ssv) * (0.8f + 0.4f * jit) * Z * Z / (FOC * fmaxf(OJO_VISTA - Hs, 0.5f)); Z += dz < 0.02f ? 0.02f : dz;
        }
        for (int r = YH; r < ytop && r < VH; r++) {   /* lo que queda hasta el horizonte: el mar más allá de la grilla */
            float a = (float)r, b = fminf(r + 1.0f, ytop), ym = 0.5f * (a + b), c[3];
            float Zl = ym - YH > 0.01f ? FOC * OJO_VISTA / (ym - YH) : 1e6f; if (Zl * gz < ZF) Zl = ZF / gz;
            sombrear_lejos(gx * Zl, gz * Zl, c); repartir(acc, a, b, c, wr);
        }
    }
    for (int y = YH; y < VH; y++) {
        float *d = FB + 3 * (y * VW + x), k = acc[y][3] > 1e-6f ? 1 / acc[y][3] : 0;
        d[0] = acc[y][0] * k; d[1] = acc[y][1] * k; d[2] = acc[y][2] * k;
    }
}

/* si la gráfica muestra el cuadro, fila_final deja la luz lineal en su textura (media precisión, con F16C) y ella hace el resto */
static u8 *g_hdr; static int g_hdr_paso;
static inline void gpu_pixel(int x, int y, float r, float g, float b, float a) {
    __m128i h = _mm_cvtps_ph(_mm_set_ps(a, b, g, r), 0);
    _mm_storel_epi64((__m128i *)(g_hdr + (size_t)y * g_hdr_paso + (size_t)x * 8), h);
}
static inline u32 empaca(float r, float g, float b, float brillo, int x, int y) {
    static const float bayer[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
    float d = (bayer[(y & 3) * 4 + (x & 3)] / 16.0f - 0.5f) / 255.0f, c[3] = { r, g, b }; u32 px = 0;
    for (int ch = 0; ch < 3; ch++) { float v = c[ch] < 0 ? 0 : c[ch]; v = v / (1 + v); v = sqrtf(v * brillo) + d; int q = (int)(v * 255); px |= (u32)(q < 0 ? 0 : q > 255 ? 255 : q) << (16 - 8 * ch); }
    return px;
}

static void fila_final(int y) {
    float amb = 0.010f + 0.018f * CU.mvis + 0.03f * CU.tv;
    for (int x = 0; x < VW; x++) {
        int i = y * VW + x; float r = FB[3 * i], g = FB[3 * i + 1], b = FB[3 * i + 2], a = 0;
        if (g_hdr && g_nubes_gpu && CIELO[i]) {   /* cielo abierto: queda lo que se le sumó encima y la gráfica pone el cielo (y encima, su ventana) */
            r -= SKY[3 * i]; g -= SKY[3 * i + 1]; b -= SKY[3 * i + 2]; a = 1;
        }
        float h = HAZE[i] * (0.6f + 0.6f * CU.lluvia) * CU.dis * 0.3f; r += h * amb * 0.8f; g += h * amb * 0.9f; b += h * amb * 1.1f;
        float tv = g_hdr ? 0 : TVM[i] * CU.tv * 0.11f * CU.dis; r += tv * 0.35f; g += tv * 0.42f; b += tv * 1.0f;   /* con la gráfica, el reflejo de la tele lo hace ella */
        if (TVM[i] > 0.25f && !g_hdr) {   /* en el reflejo de la tele se alcanzan a ver las olas de luz del menú de la Play */
            float bd = 0.5f + 0.5f * sinf(y * 0.21f + (float)CU.t * 0.9f + 2.2f * sinf(x * 0.018f + (float)CU.t * 0.35f));
            bd *= bd; bd = bd * bd * bd;
            float im = clampf(TVM[i] - 0.25f, 0, 1) * bd * CU.tv * 0.16f * CU.dis; r += im * 0.5f; g += im * 0.75f; b += im * 1.1f;
        }
        if (CU.marco && MARCO[i] && !g_hdr) {   /* el marco plano de antes, solo sin gráfica: con ella, la ventana la dibuja la gráfica */
            float lx = 1 - (float)x / VW, ly = (float)y / VH, tvl = CU.tv * 0.08f * lx * (0.4f + 0.6f * ly);
            r = 0.008f + tvl * 0.35f; g = 0.006f + tvl * 0.4f; b = 0.005f + tvl * 1.0f;
            if (MARCO[i] == 2) { float rim = CU.mvis * 0.025f + 0.004f; r += rim; g += rim * 1.02f; b += rim * 1.08f; }
            if (MARCO[i] == 3) { r += tvl * 0.6f; g += tvl * 0.7f; b += tvl * 1.6f; }
            r *= CU.dis; g *= CU.dis; b *= CU.dis;
        }
        if (g_hdr) gpu_pixel(x, y, r, g, b, a); else PIX[i] = empaca(r, g, b, CU.brillo, x, y);
    }
}

static piscina g_pv; static volatile int g_fase_v;
static void fase_vista(int id, int n) {
    if (g_fase_v == 0) { for (int y; (y = tomar_v(YH + 1)) >= 0;) fila_cielo(y); }
    else if (g_fase_v == 1) { for (int b; (b = tomar_v((VW + 15) / 16)) >= 0;) for (int x = b * 16; x < b * 16 + 16 && x < VW; x++) columna(x); }
    else if (g_fase_v == 2) { for (int y; (y = tomar_v(VH)) >= 0;) fila_final(y); }
    else { for (int j; (j = tomar_v(DUNA_NZ)) >= 0;) fila_sombra(j); }
}
static void lanzar(int fase) { g_fase_v = fase; g_col = 0; piscina_correr(&g_pv, fase_vista); }

static void preparar_cuadro(double t, float dt, float brillo, float deriva, float maestro, float tv, float lluvia, float viento) {
    CU.t = t; CU.dt = dt; CU.brillo = brillo; CU.lluvia = lluvia; CU.viento = viento;
    float mae = clampf(maestro, 0, 1); CU.dis = mae * (0.3f + 0.7f * mae);
    CU.tv += (tv - CU.tv) * 0.25f;
    CU.mluz = 1;
    if (ASTRO.ok) {   /* la luna de verdad: donde está esta noche sobre Pellines, del tamaño que se ve (algo agrandada) y con la luz que da */
        float l[3] = { ASTRO.luna[0], ASTRO.luna[1], ASTRO.luna[2] }, s[3] = { ASTRO.sol[0], ASTRO.sol[1], ASTRO.sol[2] };
        CU.mluz = ASTRO.luz; CU.altsol = ASTRO.alt_sol;
        for (int c = 0; c < 3; c++) { LUNA[c] = ASTRO.color[c]; CU.disco[c] = ASTRO.disco[c]; }
        if (g_luna_postal) {   /* la luna en la ventana: se gira todo el cielo, primero en acimut y después en altura, con la misma fase */
            float az_t = atan2f(0.16f, 1), alt_t = atanf(0.105f / sqrtf(1 + 0.16f * 0.16f));
            float az_r = atan2f(l[0], l[2]), alt_r = asinf(fmaxf(-1, fminf(1, l[1]))), fi = az_t - az_r, cf = cosf(fi), sf = sinf(fi);
            float s1[3] = { s[0] * cf + s[2] * sf, s[1], -s[0] * sf + s[2] * cf };   /* el giro en acimut */
            float k[3] = { -cosf(az_t), 0, sinf(az_t) }, th = alt_t - alt_r, ct = cosf(th), st = sinf(th);   /* y en altura, sobre el eje horizontal */
            float kv = k[0] * s1[0] + k[2] * s1[2], kx[3] = { k[1] * s1[2] - k[2] * s1[1], k[2] * s1[0] - k[0] * s1[2], k[0] * s1[1] - k[1] * s1[0] };
            for (int c = 0; c < 3; c++) s[c] = s1[c] * ct + kx[c] * st + k[c] * kv * (1 - ct);
            l[0] = cosf(alt_t) * sinf(az_t); l[1] = sinf(alt_t); l[2] = cosf(alt_t) * cosf(az_t);
            CU.mluz = ASTRO.brillo_fase * 0.6f; CU.altsol = -30;   /* la luz de esa fase, como a media altura */
            static const float base[3] = { 1.0f, 0.95f, 0.84f }; for (int c = 0; c < 3; c++) { LUNA[c] = base[c]; CU.disco[c] = base[c] * 0.75f; }
        }
        for (int c = 0; c < 3; c++) CU.sol[c] = s[c];
        CU.mdx = l[0]; CU.mdy = l[1]; CU.mdz = l[2]; CU.mr = FOC * 0.0059f;
        if (l[2] > 0.05f) { CU.mx = VW * 0.5f + FOC * l[0] / l[2]; CU.my = YH - FOC * l[1] / l[2]; } else CU.mx = CU.my = -10000;   /* detrás de la cabaña */
    } else {
        CU.mx = VW * (0.66f + 0.08f * deriva); CU.my = VH * (0.25f + 0.03f * deriva); CU.mr = VH * 0.026f;
        float lx = (CU.mx - VW * 0.5f) / FOC, ly = (YH - CU.my) / FOC, il = 1 / sqrtf(lx * lx + ly * ly + 1);
        CU.mdx = lx * il; CU.mdy = ly * il; CU.mdz = il;
    }
    { float x = CU.mdx, z = CU.mdz; a_grilla(x, z, &CU.mdx, &CU.mdz); }   /* de aquí en adelante la luz de la luna va en metros de la grilla: alumbra el agua y la arena */
    if (g_via_postal) { CU.mluz = 0; CU.altsol = -40; CU.mx = CU.my = -10000; }   /* la postal de la Vía Láctea: noche sin luna */
    CU.nx += dt * 0.006f * (0.4f + viento); CU.nz += dt * 0.0025f;
    CU.cov = 0.35f + 0.35f * lluvia + 0.2f * (0.5f + 0.5f * sinf((float)t / 300.0f)); if (g_via_postal) CU.cov = 0.02f + 0.3f * lluvia;
    { float my = fminf(fmaxf(CU.my, 0), YH - 2), mx = fminf(fmaxf(CU.mx, 0), VW);
      float e = (float)(YH - my) / VH + 0.006f, dist = 0.32f / e, cx = (mx - VW * 0.5f) / (VW * 0.5f) * dist * 0.8f + CU.nx, cz = dist + CU.nz;
      float d = 0.65f * tex(NUBE, cx * 22, cz * 22) + 0.35f * tex(NUBE, cx * 57 + 91, cz * 30 + 17), dens = suave((d - (1 - CU.cov)) / 0.22f);
      float vis = g_nubes_gpu && g_mvis_gpu >= 0 ? g_mvis_gpu : 1 - dens;   /* con nubes de la gráfica, ella dice cuánto deja pasar la luna */
      CU.mvis += (vis * CU.mluz - CU.mvis) * clampf(dt * 1.5f, 0, 1); }
    /* el ojo se acostumbra a la oscuridad en un par de minutos: sin luna se ve más de lo que daría la cuenta, pero nunca como con luna llena */
    { float objetivo = clampf(powf(0.3f / (0.04f + CU.mvis), 0.4f), 1, 2.3f); CU.adapt += (objetivo - CU.adapt) * clampf(dt / 40, 0, 1); CU.brillo = brillo * CU.adapt; }
    /* el picado se mueve con el viento: x a lo largo de la playa, z mar adentro */
    float U = viento * 10; CU.rug = 0.10f + 0.035f * U + 0.06f * lluvia;
    CU.wox += dt * (M.vx * 0.22f + 0.15f); CU.woz += dt * (M.vz * 0.22f - 0.35f);
    for (int k = 0; k < NBAR; k++) { barco *b = &BAR[k]; float lim = 0.62f * b->Z; b->X += b->v * dt; if (b->X > lim) b->X = -lim; else if (b->X < -lim) b->X = lim; }
    FUEGO.brillo = 0.78f + 0.12f * sinf((float)t * 7.3f) + 0.07f * sinf((float)t * 13.1f + 1.2f) + 0.05f * sinf((float)t * 23.7f + 0.4f) + 0.1f * sinf((float)t * 0.05f) + g_llamarada;   /* el fuego respira */
    g_llamarada *= expf(-dt * 1.8f);
}

/* ---------------- la gente de la fogata ---------------- */
/* cuatro personas sentadas en la arena, mirando el fuego. Cada una es un cuerpo en 3D (tronco, cabeza con pelo, brazos con codo y
   piernas con las rodillas arriba, todo hecho de cápsulas) que se proyecta con la misma cámara que el resto y se alumbra con el fuego
   (naranja y parpadeando; a la que le da la espalda se le enciende el borde) y con la luna. Respiran, se mecen, uno conversa con la
   mano y los otros se inclinan a escucharlo, se pasan el mate en ronda y uno atiza el fuego con un palo: saltan chispas */
#define NGENTE 4
static const float GENTE_ANG[NGENTE] = { 200, 108, 28, 292 };   /* alrededor del fuego, en grados: 0 a la derecha, 90 detrás, 270 delante */
static const float ROPA[NGENTE][3] = { { 0.05f, 0.07f, 0.13f }, { 0.16f, 0.06f, 0.045f }, { 0.11f, 0.11f, 0.105f }, { 0.05f, 0.09f, 0.06f } };
static const float PIEL[3] = { 0.42f, 0.27f, 0.19f }, PELO[3] = { 0.03f, 0.025f, 0.02f }, JEANS[3] = { 0.035f, 0.04f, 0.055f };
static const float MATE_C[3] = { 0.09f, 0.07f, 0.03f }, PALO[3] = { 0.06f, 0.04f, 0.03f };
typedef struct { float a[3], b[3], r; const float *col; } capsula;
static inline void gente_lugar(int k, float fxc, float fzc, float *x, float *z) {
    float an = GENTE_ANG[k] * (PI / 180), R = 1.5f + 0.12f * (k & 1); *x = fxc + R * cosf(an); *z = fzc + R * sinf(an);
}
static void codo(const float *S, float *H, const float *dob, float *E) {   /* el codo, con brazo de 0,30 m y antebrazo de 0,28 m doblados hacia dob */
    const float A = 0.30f, B = 0.28f; float d[3] = { H[0] - S[0], H[1] - S[1], H[2] - S[2] }, L = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) + 1e-5f;
    for (int i = 0; i < 3; i++) d[i] /= L;
    if (L > A + B - 0.02f) { L = A + B - 0.02f; for (int i = 0; i < 3; i++) H[i] = S[i] + d[i] * L; }   /* no alcanza: el brazo estirado */
    float x = (A * A - B * B + L * L) / (2 * L), y = sqrtf(fmaxf(A * A - x * x, 0)), p = dob[0] * d[0] + dob[1] * d[1] + dob[2] * d[2];
    float v[3] = { dob[0] - p * d[0], dob[1] - p * d[1], dob[2] - p * d[2] }, iv = 1 / (sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]) + 1e-5f);
    for (int i = 0; i < 3; i++) E[i] = S[i] + d[i] * x + v[i] * iv * y;
}
#define LOC(v, A, B, C) (v[0] = px + (A) * f[0] + (C) * r[0], v[1] = fh + (B), v[2] = pz + (A) * f[2] + (C) * r[2])
#define DIR(v, A, B, C) (v[0] = (A) * f[0] + (C) * r[0], v[1] = (B), v[2] = (A) * f[2] + (C) * r[2])
#define MEZCLA(m, v, w) do { float w_ = (w); for (int i_ = 0; i_ < 3; i_++) m[i_] += (v[i_] - m[i_]) * w_; } while (0)
#define CAP(A, B, R, C) do { for (int i_ = 0; i_ < 3; i_++) { c[n].a[i_] = A[i_]; c[n].b[i_] = B[i_]; } c[n].r = R; c[n].col = C; n++; } while (0)
static int persona(int k, float t, float fxc, float fzc, float fh, capsula *c) {   /* el cuerpo de la persona k ahora mismo, en metros de la vista */
    float px, pz; gente_lugar(k, fxc, fzc, &px, &pz);
    float an = GENTE_ANG[k] * (PI / 180), f[3] = { -cosf(an), 0, -sinf(an) }, r[3] = { f[2], 0, -f[0] };   /* hacia el fuego y su derecha */
    /* lo que está haciendo: respira, se mece; cada 6,5 s conversa otro; el mate da la vuelta cada 19 s; el de la derecha atiza cada 27 s */
    float resp = 0.007f * sinf(t * 1.3f + k * 2.1f), mece = 0.03f * sinf(t * 0.43f + k * 1.9f) + 0.012f * sinf(t * 1.1f + k);
    u32 hh = (u32)(int)floorf(t / 6.5f) * 2654435761u; hh ^= hh >> 15; int habla = (int)(hh % NGENTE);
    float th = fmodf(t, 6.5f), hv = suave(th / 0.8f) * (1 - suave((th - 5.6f) / 0.8f));
    const float T = 19; float ph = fmodf(t, 4 * T); int g = (int)(ph / T) % NGENTE; float u = ph - g * T; int rc = (g + 1) % NGENTE;
    float pasa = (k == g || k == rc) ? suave(u / 0.7f) * (1 - suave((u - 2.0f) / 0.7f)) : 0;
    float toma = k == rc ? suave((u - 2.7f) / 0.8f) * (1 - suave((u - 8.5f) / 0.9f)) : 0;
    int tiene = k == (u < 1.35f ? g : rc);
    float up = fmodf(t + 5, 27), atiza = k == 2 && pasa < 0.01f ? suave(up / 0.8f) * (1 - suave((up - 2.6f) / 0.7f)) : 0;
    float ox, oz; gente_lugar(habla, fxc, fzc, &ox, &oz);   /* los que escuchan se inclinan hacia el que habla */
    float lat = k == habla ? 0 : 0.03f * hv * ((ox - px) * r[0] + (oz - pz) * r[2] > 0 ? 1 : -1);
    int otro = k == g ? rc : g; float qx, qz; gente_lugar(otro, fxc, fzc, &qx, &qz);
    float lado = (qx - px) * r[0] + (qz - pz) * r[2] > 0 ? 1.0f : -1.0f;   /* el mate va en la mano del lado del otro */
    float lean = mece + 0.17f * atiza + 0.06f * pasa, nod = k == habla ? 0.012f * sinf(t * 2.3f) * hv : 0;
    /* el esqueleto: adelante (hacia el fuego), arriba, a la derecha */
    float cadL[3], cadR[3], homL[3], homR[3], cue[3], cab[3], pel[3], rodL[3], rodR[3], pieL[3], pieR[3], rk = 0.38f + 0.02f * (k & 1);
    LOC(cadL, 0, 0.13f, -0.09f); LOC(cadR, 0, 0.13f, 0.09f);
    LOC(homL, 0.06f + lean, 0.57f + resp, -0.17f + lat); LOC(homR, 0.06f + lean, 0.57f + resp, 0.17f + lat); LOC(cue, 0.07f + lean, 0.62f + resp, lat);
    LOC(cab, 0.10f + lean + nod - 0.02f * toma, 0.77f + resp, lat); LOC(pel, 0.065f + lean + nod - 0.02f * toma, 0.80f + resp, lat);
    LOC(rodL, 0.35f, rk, -0.12f); LOC(rodR, 0.35f, rk, 0.12f); LOC(pieL, 0.6f, 0.045f, -0.14f); LOC(pieR, 0.6f, 0.045f, 0.14f);
    float mL[3], mR[3];   /* las manos: cada uno a su manera cuando no hace nada */
    switch (k) {
    case 0: LOC(mL, 0.37f, 0.33f, -0.04f); LOC(mR, 0.37f, 0.35f, 0.05f); break;                                  /* abrazado a las rodillas */
    case 1: LOC(mL, 0.33f, 0.42f, -0.12f); LOC(mR, -0.22f, 0.03f, 0.27f); break;                                 /* una mano en la rodilla y la otra atrás, en la arena */
    case 2: LOC(mL, 0.47f, 0.37f, -0.1f); LOC(mR, 0.47f, 0.37f, 0.1f); break;                                    /* los brazos sobre las rodillas */
    default: { float fr = 0.015f * sinf(t * 5.1f); LOC(mL, 0.52f, 0.44f, -0.08f - fr); LOC(mR, 0.52f, 0.44f, 0.08f + fr); }   /* calentándose las manos */
    }
    if (k == habla) { float v[3]; LOC(v, 0.3f + 0.06f * sinf(t * 2.7f), 0.5f + 0.07f * (0.5f + 0.5f * sinf(t * 3.3f + 1)), 0.17f); MEZCLA(mR, v, hv); }   /* conversa con la mano */
    float *mm = lado > 0 ? mR : mL;
    if (pasa > 0) { float v[3] = { 0.5f * (px + qx), fh + 0.52f, 0.5f * (pz + qz) }; MEZCLA(mm, v, pasa); }   /* le pasa el mate al de al lado */
    if (toma > 0) { float v[3]; LOC(v, 0.2f + lean, 0.72f + resp, 0.03f * lado + lat); MEZCLA(mm, v, toma); }   /* lo toma */
    if (atiza > 0) { float v[3]; LOC(v, 0.62f + lean, 0.33f, 0.06f); MEZCLA(mR, v, atiza); }                      /* atiza el fuego */
    float codL[3], codR[3], dL[3], dR[3];
    DIR(dL, -0.1f, -0.5f, -0.6f); DIR(dR, -0.1f, -0.5f, 0.6f);   /* los codos se doblan hacia afuera y hacia abajo */
    codo(homL, mL, dL, codL); codo(homR, mR, dR, codR);
    int n = 0; float tL[3] = { cadL[0], cadL[1] + 0.03f, cadL[2] }, tR[3] = { cadR[0], cadR[1] + 0.03f, cadR[2] };
    CAP(cadL, rodL, 0.085f, JEANS); CAP(cadR, rodR, 0.085f, JEANS); CAP(rodL, pieL, 0.062f, JEANS); CAP(rodR, pieR, 0.062f, JEANS);
    CAP(tL, homL, 0.12f, ROPA[k]); CAP(tR, homR, 0.12f, ROPA[k]); CAP(cue, cab, 0.05f, PIEL);   /* con chaqueta y jeans: abrigados */
    CAP(homL, codL, 0.058f, ROPA[k]); CAP(codL, mL, 0.05f, ROPA[k]); CAP(mL, mL, 0.05f, PIEL);
    CAP(homR, codR, 0.058f, ROPA[k]); CAP(codR, mR, 0.05f, ROPA[k]); CAP(mR, mR, 0.05f, PIEL);
    CAP(cab, cab, 0.105f, PIEL); CAP(pel, pel, 0.1f, PELO);
    if (tiene) CAP(mm, mm, 0.042f, MATE_C);
    if (atiza > 0.05f) { float pf[3] = { fxc + 0.1f * f[0], fh + 0.12f, fzc + 0.1f * f[2] }, pa[3]; for (int i = 0; i < 3; i++) pa[i] = mR[i] + (pf[i] - mR[i]) * atiza; CAP(mR, pa, 0.014f, PALO); }
    return n;
}
#undef LOC
#undef DIR
#undef MEZCLA
#undef CAP
static void gente_sombras(float fxc, float fzc, float fh) {   /* el fuego tira la sombra de cada uno hacia afuera, sobre la arena que alumbra */
    float E = OJO_VISTA - fh;
    for (int k = 0; k < NGENTE; k++) {
        float px, pz, an = GENTE_ANG[k] * (PI / 180), ux = cosf(an), uz = sinf(an); gente_lugar(k, fxc, fzc, &px, &pz);
        float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
        for (int q = 0; q < 4; q++) {
            float al = q < 2 ? -0.3f : 2.6f, ac = (q & 1) ? 0.5f : -0.5f, X = px + al * ux - ac * uz, Z = pz + al * uz + ac * ux;
            if (Z < 1) continue;
            float sx = VW * 0.5f + FOC * X / Z, sy = YH + FOC * E / Z; x0 = fminf(x0, sx); x1 = fmaxf(x1, sx); y0 = fminf(y0, sy); y1 = fmaxf(y1, sy);
        }
        for (int y = (int)fmaxf(y0 - 1, (float)YH + 1); y <= (int)fminf(y1 + 1, (float)VH - 1); y++) {
            float Z = FOC * E / (y + 0.5f - YH);
            for (int x = (int)fmaxf(x0 - 1, 0); x <= (int)fminf(x1 + 1, (float)VW - 1); x++) {
                float gx = (x + 0.5f - VW * 0.5f) * Z / FOC - px, gz = Z - pz, al = gx * ux + gz * uz, ac = -gx * uz + gz * ux;
                float s = (1 - suave((fabsf(ac) - 0.2f - 0.1f * al) / 0.1f)) * suave((al + 0.25f) / 0.3f) * (1 - suave((al - 1.2f) / 1.3f));
                if (s <= 0) continue;
                float m = 1 - 0.65f * s / (1 + 0.12f * (1.5f + al) * (1.5f + al)), *d = FB + 3 * (y * VW + x); d[0] *= m; d[1] *= m; d[2] *= m;
            }
        }
    }
}
static void gente(float t, float fxc, float fzc, float fh, int delante) {   /* dibuja a los que están detrás del fuego (0) o delante (1) */
    float lmx, lmz; a_camara(CU.mdx, CU.mdz, &lmx, &lmz);   /* la luna, en la vista */
    float F[3] = { fxc, fh + 0.45f, fzc };
    for (int k = 0; k < NGENTE; k++) {
        float px, pz; gente_lugar(k, fxc, fzc, &px, &pz); if ((pz < fzc) != delante) continue;
        capsula c[20]; int n = persona(k, t, fxc, fzc, fh, c);
        float S[20][6], rp[20], x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
        for (int i = 0; i < n; i++) {
            float za = c[i].a[2], zb = c[i].b[2]; if (za < 1 || zb < 1) return;
            S[i][0] = VW * 0.5f + FOC * c[i].a[0] / za; S[i][1] = YH + FOC * (OJO_VISTA - c[i].a[1]) / za; S[i][2] = za - c[i].r;
            S[i][3] = VW * 0.5f + FOC * c[i].b[0] / zb; S[i][4] = YH + FOC * (OJO_VISTA - c[i].b[1]) / zb; S[i][5] = zb - c[i].r;
            rp[i] = fmaxf(c[i].r * FOC * 2 / (za + zb), 0.55f);
            x0 = fminf(x0, fminf(S[i][0], S[i][3]) - rp[i]); x1 = fmaxf(x1, fmaxf(S[i][0], S[i][3]) + rp[i]);
            y0 = fminf(y0, fminf(S[i][1], S[i][4]) - rp[i]); y1 = fmaxf(y1, fmaxf(S[i][1], S[i][4]) + rp[i]);
        }
        for (int y = (int)fmaxf(y0 - 1, 0); y <= (int)fminf(y1 + 1, (float)VH - 1); y++) for (int x = (int)fmaxf(x0 - 1, 0); x <= (int)fminf(x1 + 1, (float)VW - 1); x++) {
            float pxl = x + 0.5f, pyl = y + 0.5f, cov = 0, zb_ = 1e9f, bt = 0, bcov = 0; int best = -1;
            for (int i = 0; i < n; i++) {   /* de las partes que cubren el píxel, gana la de más adelante */
                float ax = S[i][0], ay = S[i][1], bx = S[i][3] - ax, by = S[i][4] - ay, l2 = bx * bx + by * by;
                float tq = l2 > 1e-6f ? clampf(((pxl - ax) * bx + (pyl - ay) * by) / l2, 0, 1) : 0, dx = pxl - ax - bx * tq, dy = pyl - ay - by * tq;
                float cv = clampf(0.5f - (sqrtf(dx * dx + dy * dy) - rp[i]), 0, 1); if (cv <= 0) continue;
                float z = S[i][2] + (S[i][5] - S[i][2]) * tq; cov = fmaxf(cov, cv);
                if (cv > 0.5f ? (bcov <= 0.5f || z < zb_) : (bcov <= 0.5f && cv > bcov)) { best = i; zb_ = z; bt = tq; bcov = cv; }
            }
            if (best < 0) continue;
            const capsula *q = &c[best];
            float ax = S[best][0] + (S[best][3] - S[best][0]) * bt, ay = S[best][1] + (S[best][4] - S[best][1]) * bt;
            float nx = (pxl - ax) / rp[best], ny = -(pyl - ay) / rp[best], l = nx * nx + ny * ny; if (l > 1) { float il = 1 / sqrtf(l); nx *= il; ny *= il; l = 1; }
            float nz = -sqrtf(1 - l), P3[3], L[3];
            for (int i = 0; i < 3; i++) { P3[i] = q->a[i] + (q->b[i] - q->a[i]) * bt; L[i] = F[i] - P3[i]; }
            float dd = L[0] * L[0] + L[1] * L[1] + L[2] * L[2], il = 1 / sqrtf(dd); L[0] *= il; L[1] *= il; L[2] *= il;
            float If = 0.32f * FUEGO.brillo / (dd + 0.35f), nl = fmaxf(0, nx * L[0] + ny * L[1] + nz * L[2]);
            float rim = (1 - fabsf(nz)) * (1 - fabsf(nz)) * fmaxf(0, L[2]) * If * 0.25f;   /* a contraluz, el borde se enciende */
            float nm = fmaxf(0, nx * lmx + ny * CU.mdy + nz * lmz) * CU.mvis * 0.045f, amb = 0.0035f * (0.3f + 0.7f * CU.mvis);
            const float *al = q->col; float *d = FB + 3 * (y * VW + x);
            float cr = (al[0] * (If * nl + nm * LUNA[0] + amb * 0.8f) + rim) * CU.dis;
            float cg = (al[1] * (If * nl * 0.47f + nm * LUNA[1] + amb * 0.9f) + rim * 0.5f) * CU.dis;
            float cb = (al[2] * (If * nl * 0.16f + nm * LUNA[2] + amb * 1.1f) + rim * 0.18f) * CU.dis;
            d[0] += (cr - d[0]) * cov; d[1] += (cg - d[1]) * cov; d[2] += (cb - d[2]) * cov;
        }
    }
}

static void detalles(double t, float dt) {
    for (int i = 0; i < 500 && !g_nubes_gpu; i++) {   /* estrellas donde las nubes se abren (con la gráfica, las pone ella detrás de sus nubes) */
        int x = (int)(E[i].x * VW), y = (int)(E[i].y * (YH - 2)); int idx = y * VW + x;
        float dx = x - CU.mx, dy = y - CU.my; if (dx * dx + dy * dy < CU.mr * CU.mr * 9) continue;
        float cl = 1 - DENS[idx]; cl *= cl; float tw = E[i].b * (0.6f + 0.4f * sinf((float)t * E[i].w + E[i].f)) * cl * 0.4f * CU.dis;
        FB[3 * idx] += tw * 0.9f; FB[3 * idx + 1] += tw * 0.95f; FB[3 * idx + 2] += tw * 1.05f;
    }
    /* los barcos de pesca: sus luces suben y bajan con la marejada, se esconden detrás de las olas y el agua las refleja temblando */
    {
        float amax = 0; for (int k = 0; k < 6; k++) amax += M.c[LEJ[k]].a * M.a_sw;
        for (int k = 0; k < NBAR; k++) {
            barco *b = &BAR[k]; float eta = 0, zb = ZPRIM + (NZ - 0.5f) * DXM, aire = expf(-b->Z / 14000) * CU.dis, bX, bZ; a_grilla(b->X, b->Z, &bX, &bZ);
            for (int q = 0; q < 6; q++) { const componente *c = &M.c[LEJ[q]]; eta += c->a * (c->viento ? M.a_ws : M.a_sw) * cosf(c->kx * bX + c->kz * (bZ - zb) - c->w * (float)t + c->ph); }
            float hondo = clampf((eta + amax * 0.55f) / (amax * 0.5f + 0.01f), 0, 1);   /* en el seno de una ola, lo bajo se esconde */
            /* luces: { metros a lo largo, altura, rojo, verde, azul, intensidad } */
            float L[8][6]; int nl = 0, verde = b->v > 0; aire *= 1.5f;
            if (b->tipo) {   /* un barco grande más afuera: dos luces de tope y una fila de luces de cubierta */
                float a1[6] = { -11, b->h, 1, 1, 1, 0.9f }, a2[6] = { 13, b->h + 4, 1, 1, 1, 0.9f }; for (int i = 0; i < 6; i++) { L[0][i] = a1[i]; L[1][i] = a2[i]; } nl = 2;
                for (int i = 0; i < 4; i++) { float d[6] = { -14.0f + i * 9, 6, 1, 0.82f, 0.55f, 0.45f }; for (int j = 0; j < 6; j++) L[nl][j] = d[j]; nl++; }
            } else {   /* una lancha: la luz de tope, la de faena que alumbra la cubierta y la de costado según hacia dónde va */
                float a1[6] = { 0, b->h, 1, 1, 1, 0.8f }, a2[6] = { 0.5f, 2.2f, 1, 0.8f, 0.5f, 1.5f }, a3[6] = { verde ? 1.0f : -1.0f, 2.0f, verde ? 0.1f : 1.0f, verde ? 1.0f : 0.1f, verde ? 0.35f : 0.05f, 0.5f };
                for (int i = 0; i < 6; i++) { L[0][i] = a1[i]; L[1][i] = a2[i]; L[2][i] = a3[i]; } nl = 3;
            }
            for (int i = 0; i < nl; i++) {
                float hl = L[i][1], vis = hl > 5 ? 1 : hondo; if (vis <= 0.02f) continue;
                float X = b->X + L[i][0], sx = VW * 0.5f + FOC * X / b->Z, sy = YH + FOC * (OJO_VISTA - (eta + hl)) / b->Z;
                float I = L[i][5] * aire * vis * (0.9f + 0.1f * sinf((float)t * 2.3f + b->fase + i));
                int x0 = (int)floorf(sx), y0 = (int)floorf(sy); float fx = sx - x0, fy = sy - y0;
                for (int dy = -1; dy <= 2; dy++) for (int dx = -1; dx <= 2; dx++) {   /* un punto de luz con su pequeño halo */
                    int xx = x0 + dx, yy = y0 + dy; if (xx < 0 || xx >= VW || yy < 0 || yy >= VH) continue;
                    float ddx = dx - fx, ddy = dy - fy, w = I * expf(-(ddx * ddx + ddy * ddy) * 1.6f);
                    FB[3 * (yy * VW + xx)] += w * L[i][2]; FB[3 * (yy * VW + xx) + 1] += w * L[i][3]; FB[3 * (yy * VW + xx) + 2] += w * L[i][4];
                }
                /* el reflejo: una columna de destellos bajo la luz, que cambian con cada ola */
                float yag = YH + FOC * (OJO_VISTA - eta) / b->Z; int largo = (int)(5 + 30 * 2000 / b->Z);
                for (int r = 0; r < largo; r++) {
                    int yy = (int)yag + r; if (yy < 0 || yy >= VH) break;
                    u32 hsh = (u32)(r * 7919 + k * 104729 + i * 31 + (int)((float)t * 7 + r * 0.37f) * 2654435761u); hsh ^= hsh >> 13; hsh *= 0x5bd1e995; hsh ^= hsh >> 15;
                    if ((hsh & 255) < 150) continue;
                    int xx = (int)(sx + ((int)((hsh >> 8) & 3) - 1.5f) * 0.6f); if (xx < 0 || xx >= VW) continue;
                    float w = I * 0.4f * (1 - (float)r / largo) * (1 - (float)r / largo);
                    FB[3 * (yy * VW + xx)] += w * L[i][2]; FB[3 * (yy * VW + xx) + 1] += w * L[i][3]; FB[3 * (yy * VW + xx) + 2] += w * L[i][4];
                }
            }
        }
    }
    /* la fogata: llamas que se mueven, el resplandor alrededor y las chispas que suben con el viento */
    float fxc, fzc; a_camara(FUEGO.X, FUEGO.Z, &fxc, &fzc);   /* la fogata vive en la grilla (alumbra la arena); aquí se dibuja en la vista */
    if (fzc > 0) {
        float px = FOC / fzc, sx = VW * 0.5f + fxc * px, sy = YH + FOC * (OJO_VISTA - FUEGO.H) / fzc, I = 2.2f * FUEGO.brillo * CU.dis, tt = (float)t;
        float h = 0.8f * px, w = 0.3f * px, sg = 1.3f * px;   /* llamas de unos 80 cm; el resplandor, de un par de metros */
        /* el resplandor primero, redondo y sin bordes */
        int gx0 = (int)(sx - 4 * sg), gx1 = (int)(sx + 4 * sg), gy0 = (int)(sy - 4 * sg), gy1 = (int)(sy + 2 * sg);
        for (int y = gy0; y <= gy1; y++) for (int x = gx0; x <= gx1; x++) {
            if (x < 0 || x >= VW || y < 0 || y >= VH) continue;
            float dx = (x + 0.5f - sx) / sg, dy = (y + 0.5f - (sy - 0.5f * h)) / sg, gl = expf(-(dx * dx + dy * dy) * 0.5f) * 0.035f * FUEGO.brillo * CU.dis;
            float *d = FB + 3 * (y * VW + x); d[0] += gl; d[1] += gl * 0.42f; d[2] += gl * 0.1f;
        }
        /* la gente: primero sus sombras en la arena y los que están detrás del fuego (las llamas les quedan delante) */
        { static float ant; float up = fmodf(tt + 5, 27); if (ant < 1.35f && up >= 1.35f) { g_atizo += 30; g_llamarada = 0.35f; } ant = up; }   /* el palo golpea las brasas */
        gente_sombras(fxc, fzc, FUEGO.H);
        gente(tt, fxc, fzc, FUEGO.H, 0);
        /* las llamas: cuatro lenguas que crecen y bajan a destiempo, torcidas por el aire caliente, y las brasas abajo */
        int x0 = (int)(sx - 2.5f * w), x1 = (int)(sx + 2.5f * w), y0 = (int)(sy - 1.5f * h), y1 = (int)(sy + 0.3f * h);
        static const float lx[4] = { -0.55f, -0.1f, 0.3f, 0.7f }, lf[4] = { 6.1f, 8.3f, 7.2f, 9.7f };
        for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
            if (x < 0 || x >= VW || y < 0 || y >= VH) continue;
            float u = (x + 0.5f - sx) / w, v = (sy - (y + 0.5f)) / h;
            float tu = u + 0.7f * fmaxf(v, 0) * (tex(GRANO, x * 1.3f, y * 1.3f + tt * 160) - 0.5f) * 2, dens = 0;
            for (int k = 0; k < 4; k++) {
                float alto = 0.55f + 0.4f * (0.5f + 0.5f * sinf(tt * lf[k] + k * 1.7f)) * (0.6f + 0.4f * FUEGO.brillo), vv = v / alto;
                float an = 0.42f * powf(clampf(1 - vv, 0, 1), 0.65f), d = clampf(1 - fabsf(tu - lx[k] * (1 - vv * 0.6f)) / fmaxf(an, 0.03f), 0, 1) * clampf(1 - vv, 0, 1);
                dens = fmaxf(dens, d);
            }
            dens *= clampf(v * 6 + 1, 0, 1) * (0.65f + 0.7f * tex(NUBE, x * 0.9f + tt * 30, y * 0.7f + tt * 70));
            float brasa = v < 0.12f && v > -0.25f ? expf(-u * u * 0.35f) * (0.12f - fabsf(v + 0.05f)) * 6 * (0.7f + 0.3f * sinf(tt * 3 + u * 4)) : 0;
            float rr = 1.0f, gg = 0.78f - 0.48f * clampf(v, 0, 1), bb = 0.38f - 0.35f * clampf(v * 1.5f, 0, 1);   /* blanca amarilla abajo, roja en las puntas */
            float *d = FB + 3 * (y * VW + x); d[0] += dens * I * rr + fmaxf(brasa, 0) * 0.9f * CU.dis; d[1] += dens * I * gg + fmaxf(brasa, 0) * 0.25f * CU.dis; d[2] += dens * I * bb;
        }
        gente(tt, fxc, fzc, FUEGO.H, 1);   /* los que están entre la ventana y el fuego: oscuros, con el borde encendido */
        /* el humo: bocanadas que suben y el viento tuerce, alumbradas por el fuego abajo y por el cielo y la luna arriba */
        for (int k = 0; k < 16; k++) {
            float a = tt * 0.22f + k / 16.0f; a -= floorf(a);
            float alto = 0.9f + a * 7.5f, deriva = M.vx * a * 2.2f + 0.5f * sinf(tt * 0.7f + k * 2.3f) * a, rad = (0.35f + a * 1.7f) * px;
            float cx = sx + deriva * px, cy = sy - alto * px, op = 0.26f * (1 - a) * suave(a * 10) * CU.dis;
            float fr = (1 - a) * (1 - a) * FUEGO.brillo * 0.08f, lu = 0.003f + 0.007f * CU.mvis;
            for (int y = (int)(cy - rad); y <= (int)(cy + rad); y++) for (int x = (int)(cx - rad); x <= (int)(cx + rad); x++) {
                if (x < 0 || x >= VW || y < 0 || y >= VH) continue;
                float dx = (x + 0.5f - cx) / rad, dy = (y + 0.5f - cy) / rad, r2 = dx * dx + dy * dy; if (r2 >= 1) continue;
                float al = op * (1 - r2) * (0.4f + 0.9f * tex(NUBE, x * 0.6f + k * 31, y * 0.6f + tt * 12));
                float *d = FB + 3 * (y * VW + x);
                d[0] = d[0] * (1 - al) + (lu + fr) * al; d[1] = d[1] * (1 - al) + (lu * 1.05f + fr * 0.5f) * al; d[2] = d[2] * (1 - al) + (lu * 1.2f + fr * 0.2f) * al;
            }
        }
        /* chispas */
        float tasa = 12 * CU.dt + g_atizo; int brote = g_atizo > 0; g_atizo = 0;   /* con el golpe del palo salta un puñado, más alto */
        while (tasa > 0 && NCH < 200) { if (uni(&vr, 0, 1) > tasa) break; tasa -= 1;
            chispa *c = &CH[NCH++]; c->X = fxc + uni(&vr, -0.2f, 0.2f); c->Z = fzc + uni(&vr, -0.2f, 0.2f); c->Y = FUEGO.H + uni(&vr, 0.2f, 0.5f);
            c->vx = M.vx * 0.15f + uni(&vr, -0.3f, 0.3f); c->vy = uni(&vr, 1.0f, brote ? 3.8f : 2.6f); c->vida = c->v0 = uni(&vr, 0.8f, 2.2f); }
        for (int i = 0; i < NCH; i++) {
            chispa *c = &CH[i]; c->vida -= CU.dt; c->X += c->vx * CU.dt; c->Y += c->vy * CU.dt; c->vy *= 1 - 0.6f * CU.dt;
            if (c->vida <= 0) { CH[i--] = CH[--NCH]; continue; }
            int x = (int)(VW * 0.5f + FOC * c->X / c->Z), y = (int)(YH + FOC * (OJO_VISTA - c->Y) / c->Z); if (x < 0 || x >= VW || y < 0 || y >= VH) continue;
            float e = c->vida / c->v0, w = 0.9f * e * e * CU.dis * (0.6f + 0.4f * sinf(c->vida * 40)); float *d = FB + 3 * (y * VW + x); d[0] += w; d[1] += w * 0.45f; d[2] += w * 0.1f;
        }
    }
    /* el rocío que salta donde el labio de una ola cae: nace en la simulación */
    LONG ne = M.nest; if (ne - g_est_visto > NEST) g_est_visto = ne - NEST;
    for (; g_est_visto < ne; g_est_visto++) {
        const estallido *s = &M.est[g_est_visto & (NEST - 1)]; int n = (int)clampf(s->f / 2500, 2, 18);
        for (int q = 0; q < n && NRO < 3000; q++) {
            rocio *r = &RO[NRO++]; float v0 = sqrtf(s->f / 4000) * uni(&vr, 0.6f, 1.4f);
            r->X = s->x + uni(&vr, -1.5f, 1.5f); r->Z = s->z + uni(&vr, -1, 1); r->Y = IZ0 + uni(&vr, 0.5f, 1.5f);
            r->vx = uni(&vr, -0.5f, 0.5f) + M.vx * 0.3f; r->vz = -uni(&vr, 0.5f, 2.5f) + M.vz * 0.3f; r->vy = v0 * uni(&vr, 0.8f, 2.2f);
            r->vida = uni(&vr, 0.7f, 1.8f); r->b = uni(&vr, 0.5f, 1);
        }
    }
    for (int i = 0; i < NRO; i++) {
        rocio *r = &RO[i]; r->vida -= dt; r->vy -= 9.8f * dt * 0.6f; r->X += r->vx * dt; r->Y += r->vy * dt; r->Z += r->vz * dt;
        if (r->vida <= 0 || r->Y < IZ0 - 0.5f) { RO[i--] = RO[--NRO]; continue; }
        float xc, zc; a_camara(r->X, r->Z, &xc, &zc);   /* el rocío nace en la grilla */
        float sx = VW * 0.5f + FOC * xc / zc, sy = YH + FOC * (OJO_VISTA - r->Y) / zc;
        int x = (int)sx, y = (int)sy; if (x < 1 || x >= VW - 1 || y < 1 || y >= VH - 1) continue;
        float w = clampf(r->vida, 0, 1) * r->b * (0.03f + 0.22f * CU.mvis) * CU.dis * clampf(120 / zc, 0.25f, 1);
        int idx = y * VW + x; FB[3 * idx] += w * 0.9f; FB[3 * idx + 1] += w * 0.95f; FB[3 * idx + 2] += w * 1.05f;
        FB[3 * (idx + 1)] += w * 0.3f; FB[3 * (idx - VW) + 1] += w * 0.3f;
    }
    /* la lluvia cayendo afuera, apenas visible contra la noche e inclinada por el viento */
    if (CU.lluvia > 0.03f) {
        int n = (int)(CU.lluvia * 260); float incl = 0.25f + 0.6f * CU.viento;
        for (int i = 0; i < n; i++) {
            float sx = uni(&vr, 0, VW), sy = uni(&vr, 0, VH * 0.95f), lar = uni(&vr, 6, 16) * VW / 960.0f;
            float br = uni(&vr, 0.004f, 0.012f) * (0.4f + 0.6f * CU.mvis + 0.3f * CU.tv) * CU.dis;
            for (int k = 0; k < (int)lar; k++) {
                int x = (int)(sx + incl * k), y = (int)(sy + k); if (x < 0 || x >= VW || y >= VH) break;
                int idx = y * VW + x; FB[3 * idx] += br * 0.8f; FB[3 * idx + 1] += br * 0.85f; FB[3 * idx + 2] += br * 0.95f;
            }
        }
    }
    /* gotas en el vidrio que deforman la vista */
    float tasa = CU.lluvia * 25 * dt;
    while (tasa > 0 && NGO < 420) { if (uni(&vr, 0, 1) > tasa) break; tasa -= 1; gota *g = &GO[NGO++]; g->x = uni(&vr, 0, VW); g->y = uni(&vr, 0, VH * 0.9f); g->r = uni(&vr, 1.5f, 5.0f) * VW / 960.0f; g->vy = 0; g->vida = uni(&vr, 60, 240); g->baja = uni(&vr, 0, 1) < 0.08f; }
    if (NGO) {
        CopyMemory(FB2, FB, (size_t)VW * VH * 12);
        for (int i = 0; i < NGO; i++) {
            gota *g = &GO[i]; g->vida -= dt * (CU.lluvia > 0.05f ? 0.3f : 2.0f);
            g->x += CU.viento * dt * (g->baja ? 4 : 0.3f);
            if (g->baja) { g->vy += 6 * dt; g->y += g->vy * dt * 8; if (uni(&vr, 0, 1) < 0.25f && NGO < 420) { gota *n = &GO[NGO++]; *n = *g; n->r = g->r * 0.4f; n->baja = 0; n->vida = 30; } }
            if (g->vida <= 0 || g->y > VH * 0.92f) { GO[i--] = GO[--NGO]; continue; }
            int r = (int)(g->r + 1);
            for (int dy = -r; dy <= r; dy++) for (int dx = -r; dx <= r; dx++) {
                int x = (int)g->x + dx, y = (int)g->y + dy; if (x < 0 || x >= VW || y < 0 || y >= VH || MARCO[y * VW + x]) continue;
                float nx = (x - g->x) / g->r, ny = (y - g->y) / (g->r * 1.15f), rr = nx * nx + ny * ny; if (rr >= 1) continue;
                int sx = (int)(g->x - nx * g->r * 2.4f), sy = (int)(g->y - ny * g->r * 2.4f - g->r * 0.6f);
                if (sx < 0) sx = 0; if (sx >= VW) sx = VW - 1; if (sy < 0) sy = 0; if (sy >= VH) sy = VH - 1;
                float bord = 0.55f + 0.45f * (1 - rr * rr * rr), *dst = FB + 3 * (y * VW + x), *src = FB2 + 3 * (sy * VW + sx); CIELO[y * VW + x] = 0;
                float hl = ((nx + 0.35f) * (nx + 0.35f) + (ny + 0.45f) * (ny + 0.45f) < 0.07f) ? (0.012f + 0.03f * CU.tv + 0.02f * CU.mvis) * CU.dis : 0;
                dst[0] = src[0] * bord + hl; dst[1] = src[1] * bord + hl; dst[2] = src[2] * bord + hl * 1.1f;
            }
        }
    }
}

static void vista_buffers(int w, int h) {
    /* sentado junto a la ventana: unos 66 grados de ancho y el horizonte a un tercio de la altura, así se ve la playa de abajo
       (con 53 grados y el horizonte a media ventana la arena quedaba en una franja y el mar se veía encima) */
    VW = w; VH = h > MAXVH ? MAXVH : h; YH = (int)(0.35f * VH); FOC = VW / 1.3f;
    g_cg = cosf(GIRO_VISTA); g_sg = sinf(GIRO_VISTA); via_postal_preparar();
    FB = reservar((size_t)VW * VH * 12); FB2 = reservar((size_t)VW * VH * 12); DENS = reservar((size_t)VW * VH * 4);
    HAZE = reservar((size_t)VW * VH * 4); TVM = reservar((size_t)VW * VH * 4); MARCO = reservar((size_t)VW * VH); PIX = reservar((size_t)VW * VH * 4);
    SKY = reservar((size_t)VW * VH * 12); CIELO = reservar((size_t)VW * VH);
    IE = campo(); IW = campo(); IR = campo(); IS = campo(); IU = campo(); IV = campo(); IRE = campo(); IDU = campo(); IDV = campo(); SUP = campo();
    CV = reservar((size_t)NXP * NZP * sizeof(celda));
    escena_iniciar();
    duna_preparar();
    /* la fogata, en la arena seca a unos 42 m, a la derecha y bien a la vista (a la izquierda está el reflejo de la tele) */
    { float d[3]; a_grilla(0.27f * 42, 42, &FUEGO.X, &FUEGO.Z); duna(FUEGO.X, FUEGO.Z, d); FUEGO.H = d[0]; FUEGO.brillo = 0.8f; }
}

/* dibuja un cuadro con la instantánea que haya: el hilo del mar la renueva cuando se la pido */
static double g_tv[6];
static double reloj_v(void) { LARGE_INTEGER c, f; QueryPerformanceCounter(&c); QueryPerformanceFrequency(&f); return (double)c.QuadPart / f.QuadPart; }
static void dibujar(double t, float dt, float brillo, float deriva, float maestro, float tv, float lluvia, float viento) {
    double a = reloj_v(), b;
    preparar_cuadro(t, dt, brillo, deriva, maestro, tv, lluvia, viento);
    if (fabsf(CU.mdx - g_mds[0]) + fabsf(CU.mdy - g_mds[1]) + fabsf(CU.mdz - g_mds[2]) > 0.002f) {   /* la luna se movió: sombras nuevas en la arena */
        g_mds[0] = CU.mdx; g_mds[1] = CU.mdy; g_mds[2] = CU.mdz; lanzar(3);
    }
    g_col = 0; piscina_correr(&g_pv, fase_prep);
    for (int r = 1; r <= 2; r++) { CopyMemory(SUP + IX(-r, -2), SUP + IX(0, -2), NXP * 4); CopyMemory(SUP + IX(NZ - 1 + r, -2), SUP + IX(NZ - 1, -2), NXP * 4); }
    g_col = 0; piscina_correr(&g_pv, fase_grad);
    b = reloj_v(); g_tv[0] += b - a; a = b;
    lanzar(0);
    b = reloj_v(); g_tv[2] += b - a; a = b;
    lanzar(1);
    b = reloj_v(); g_tv[3] += b - a; a = b;
    detalles(t, dt);
    b = reloj_v(); g_tv[4] += b - a; a = b;
    lanzar(2);
    b = reloj_v(); g_tv[5] += b - a;
}
