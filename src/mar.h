/* El Pacífico frente a Pellines resuelto como ecuaciones, no como animación.
   - Mar adentro (más de 6 m de fondo) la marejada viaja como onda lineal con la dispersión de Boussinesq
     de Madsen y Sørensen (1992): cada ola va a la velocidad que le dan su período y la profundidad, se
     refracta sobre el fondo y se agrupa en series por interferencia, como en el mar.
   - Entre 6 y 4 m la no linealidad se enciende: las olas se empinan, la cresta se afila y el valle se aplana.
   - Desde 4 m hacia la orilla son aguas someras con un esquema escalonado que conserva el momento
     (Stelling y Duinmeijer 2003): cada frente que revienta se vuelve un resalto que pierde energía como
     uno de verdad, y la orilla se moja y se seca sola.
   - Una ola revienta cuando su cara sube más rápido que 0,6 veces la velocidad de una onda a esa
     profundidad (Smit, Zijlema y Stelling 2013) o cuando su cresta o su valle ya no caben en el fondo.
   - El fondo es una playa de arena gruesa de perfil de Dean con dos barras en media luna y canales de
     resaca; la costa sigue a ambos lados (la grilla es periódica a lo largo de la playa).
   - Por el borde de mar adentro entra la marejada como suma de 112 componentes de espectro JONSWAP,
     y lo que la playa refleja sale sin rebotar.
   La espuma, el rocío y el sonido salen de la energía que pierde cada frente al reventar. */

#define NX 640
#define NZ 272
#define NXP (NX + 4)
#define NZP (NZ + 4)
#define IX(j, i) (((j) + 2) * NXP + (i) + 2)
#define DXM 1.25f            /* metros por celda */
#define GRAV 9.81f
#define RHO 1025.0f
#define ZPRIM 82.0f          /* distancia horizontal de la ventana al primer borde de la grilla */
#define ZORILLA 110.0f       /* donde el nivel medio del mar toca la arena: la cabaña está a 110 m de la orilla */
#define ALTO_VENTANA 13.0f   /* altura de los ojos sobre el nivel medio del mar: la cabaña en la duna, a unos 10 m, y la ventana sobre el piso */
#define DTM 0.0625f
#define BMS (1.0f / 15)
#define ALFA_D (BMS + 1.0f / 3)
#define BETA_D (2 * BMS + 1)
#define NCOMP 112
#define NBORDE 8
#define NANG 18
#define NZONA 4
#define NBIN (NANG * NZONA)
#define MAXESP 131072        /* las versiones anteriores guardaban la espuma como partículas: el estado las trae y se saltan */
#define NECO 1024
#define H0 7.6f
#define SECO 0.002f
#define NEST 512
#define T_RODILLO 1.5f
#define TAU_BLANCA 6.0f
#define TAU_RESTO 40.0f      /* la espuma que queda flotando cuando el agua blanca se apaga */
#define K_RESTO 0.12f
#define LARGO (NX * DXM)
#define ANCHO (NZ * DXM)

typedef struct { float a, w, kx, kz, ph, U, cs, sn; int viento; } componente;
typedef struct { double t; float pot[NBIN], ini[NBIN], rd[NBIN], sube[NANG], baja[NANG], burb[NANG]; } eco;
typedef struct { float x, z, edad, vida, w; } espuma;   /* en celdas de la grilla */
typedef struct { float x, z, f, t; } estallido;         /* en metros del mundo */

static struct {
    float *eta, *hb, *u, *v, *un, *vn, *qx, *qz, *qxs, *qzs, *rf, *W, *W2, *fsrc, *pend, *seco, *rod, *wd, *rcel;
    float *R, *R2, *du, *dv, *du2, *dv2;   /* la espuma que queda y de dónde viene cada gota de agua (para que el dibujo de la espuma viaje con ella) */
    u8 *brk, *brk2, *bin;
    componente c[NCOMP]; int nc;
    double t; long pasos; float zeta0, vx, vz, alfa, beta, a_sw, a_ws, rampa;
    estallido est[NEST]; volatile LONG nest;
    eco ecos[NECO]; volatile LONG neco;
    eco acc[MAXHIL];
    float *tri[MAXHIL];
    piscina *P; u32 rng; volatile LONG sig;
    float etain[NX], vin[NX];
    float hs_sw, tp_sw, dir_sw, hs_ws, tp_ws, dir_ws;
} M;

/* ---------------- el fondo: playa de arena gruesa con dos barras en media luna ---------------- */
static float g_fase[8];
static float fondo(float X, float Z) {   /* altura del fondo sobre el nivel medio (negativa bajo el agua) */
    float s = Z - ZORILLA, zb;
    if (s >= 0) zb = -0.17f * (powf(s + 1.455f, 2.0f / 3) - 1.2834f);   /* perfil de equilibrio de Dean, A = 0,17 */
    else { float a = -s; zb = a < 25 ? a / 9 : 2.78f + (a - 25) * 0.12f; }   /* cara de playa 1:9 y la berma */
    float w = 2 * PI / LARGO;
    if (s > 4) {
        float si = 42 + 9 * sinf(w * 4 * X + g_fase[0]);   /* barra interior: media luna cada 200 m */
        float ci = fmodf(X - (LARGO / 4) * (0.25f - g_fase[0] / (2 * PI)) + 4 * LARGO, LARGO / 4); ci = fminf(ci, LARGO / 4 - ci);
        float ai = 0.95f * (1 - 0.8f * expf(-(ci * ci) / (16.0f * 16.0f)));   /* canal de resaca donde la barra se aleja */
        float so = 118 + 16 * sinf(w * 2 * X + g_fase[1]) + 6 * sinf(w * 3 * X + g_fase[2]);   /* barra exterior */
        float co = fmodf(X - (LARGO / 2) * (0.25f - g_fase[1] / (2 * PI)) + 4 * LARGO, LARGO / 2); co = fminf(co, LARGO / 2 - co);
        float ao = 1.05f * (1 - 0.45f * expf(-(co * co) / (28.0f * 28.0f)));
        float ramp = suave((s - 4) / 10);
        zb += ramp * (ai * expf(-(s - si) * (s - si) / (11.0f * 11.0f)) + ao * expf(-(s - so) * (s - so) / (20.0f * 20.0f)));
        zb -= ramp * 0.35f * expf(-(s - 22) * (s - 22) / 36.0f);   /* la canaleta entre la orilla y la barra */
    }
    zb += 0.05f * sinf(w * 29 * X + g_fase[3]) * sinf(0.13f * Z + g_fase[4]) + 0.04f * sinf(w * 57 * X + g_fase[5] + 0.21f * Z);
    if (s > 255) { float q = suave((s - 255) / 30); zb = zb * (1 - q) - H0 * q; }   /* fondo parejo junto al borde */
    return zb;
}

/* ---------------- la marejada que entra: espectro JONSWAP con dispersión direccional ---------------- */
static float k_ms(float w, float h) {   /* número de onda que cumple la dispersión de Madsen y Sørensen */
    float k = w / sqrtf(GRAV * h);
    for (int it = 0; it < 30; it++) {
        float kh2 = k * k * h * h, n = 1 + BMS * kh2, d = 1 + ALFA_D * kh2, f = GRAV * h * k * k * n / d - w * w;
        float df = GRAV * h * (2 * k * n / d + k * k * (2 * BMS * k * h * h * d - n * 2 * ALFA_D * k * h * h) / (d * d));
        k -= f / df; if (k < 1e-4f) k = 1e-4f;
    }
    return k;
}
static float jonswap(float f, float fp) {
    float s = f <= fp ? 0.07f : 0.09f, r = expf(-(f - fp) * (f - fp) / (2 * s * s * fp * fp));
    return powf(f, -5) * expf(-1.25f * powf(fp / f, 4)) * powf(3.3f, r);
}
static void mar_tren(int c0, int n, float hs, float tp, float dir, float spread, int viento) {
    float fp = 1 / tp, f0 = 0.62f * fp, f1 = 2.4f * fp, df = (f1 - f0) / n, m0 = 0;
    float S[NCOMP];
    for (int k = 0; k < n; k++) { float f = f0 + (k + uni(&M.rng, 0.15f, 0.85f)) * df; M.c[c0 + k].w = 2 * PI * f; S[k] = jonswap(f, fp); m0 += S[k] * df; }
    for (int k = 0; k < n; k++) {
        componente *c = &M.c[c0 + k];
        c->a = sqrtf(2 * S[k] * df * (hs * hs / 16) / m0);
        float th;   /* dirección con reparto cos^2s */
        for (;;) { th = dir + uni(&M.rng, -1.2f, 1.2f); float p = powf(fmaxf(cosf((th - dir) * 0.5f), 0), 2 * spread); if (uni(&M.rng, 0, 1) < p) break; }
        float k = k_ms(c->w, H0); int m = (int)lroundf(k * sinf(th) * LARGO / (2 * PI));
        while (fabsf(2 * PI * m / LARGO) >= k * 0.98f) m += m > 0 ? -1 : 1;
        c->kx = 2 * PI * m / LARGO; c->kz = sqrtf(k * k - c->kx * c->kx); c->sn = c->kx / k; c->cs = c->kz / k;
        c->U = c->w * c->a / (k * H0); c->ph = uni(&M.rng, 0, 2 * PI); c->viento = viento;
    }
}
static void mar_oleaje(float hs_sw, float tp_sw, float dir_sw, float hs_ws, float tp_ws, float dir_ws) {
    M.hs_sw = hs_sw; M.tp_sw = tp_sw; M.dir_sw = dir_sw; M.hs_ws = hs_ws; M.tp_ws = tp_ws; M.dir_ws = dir_ws;
    mar_tren(0, 80, hs_sw, tp_sw, dir_sw, 45, 0);   /* marejada del suroeste: crestas largas */
    mar_tren(80, 32, hs_ws, tp_ws, dir_ws, 6, 1);   /* mar de viento: más corto y revuelto */
    M.nc = 112;
}

/* ---------------- la grilla ---------------- */
static float *campo(void) { return reservar((size_t)NXP * NZP * 4); }
static void fantasma_tierra(float *a, int modo) {   /* modo 0: copia la primera fila; 1: ceros */
    for (int r = 1; r <= 2; r++) { if (modo == 0) CopyMemory(a + IX(-r, -2), a + IX(0, -2), NXP * 4); else ZeroMemory(a + IX(-r, -2), NXP * 4); }
}
static void fantasma_mar(float *a) { for (int r = 1; r <= 2; r++) CopyMemory(a + IX(NZ - 1 + r, -2), a + IX(NZ - 1, -2), NXP * 4); }
static void filas_fantasma(float *a, int modo) { fantasma_tierra(a, modo); fantasma_mar(a); }
static inline void fila_periodica(float *f) { f[-2] = f[NX - 2]; f[-1] = f[NX - 1]; f[NX] = f[0]; f[NX + 1] = f[1]; }

static void mar_iniciar(piscina *P, u32 semilla) {
    M.P = P; M.rng = semilla | 1;
    float **cs[] = { &M.eta, &M.hb, &M.u, &M.v, &M.un, &M.vn, &M.qx, &M.qz, &M.qxs, &M.qzs, &M.rf, &M.W, &M.W2, &M.fsrc, &M.pend, &M.seco, &M.rod, &M.wd, &M.rcel,
                     &M.R, &M.R2, &M.du, &M.dv, &M.du2, &M.dv2 };
    for (int k = 0; k < (int)(sizeof cs / sizeof *cs); k++) *cs[k] = campo();
    M.brk = reservar((size_t)NXP * NZP); M.brk2 = reservar((size_t)NXP * NZP); M.bin = reservar((size_t)NXP * NZP);
    for (int i = 0; i < P->n; i++) M.tri[i] = reservar((size_t)(12 * NX + 9 * NZ * 16) * 4);
    for (int k = 0; k < 8; k++) g_fase[k] = uni(&M.rng, 0, 2 * PI);
    for (int j = -2; j < NZ + 2; j++) for (int i = -2; i < NX + 2; i++) {
        int jj = j < 0 ? 0 : j >= NZ ? NZ - 1 : j, ii = (i + NX) % NX;
        float X = (ii + 0.5f - NX / 2) * DXM, Z = ZPRIM + (jj + 0.5f) * DXM, zb = fondo(X, Z);
        int c = IX(j, i); M.hb[c] = -zb; M.eta[c] = zb < 0 ? 0 : zb; M.seco[c] = 1e4f;
        /* a qué parte del oído llega cada trozo de mar: dirección desde la ventana y franja de la rompiente */
        float ang = atan2f(X, Z) * 180 / PI, s = Z - ZORILLA; int a = (int)((ang + 80) / 160 * NANG);
        int zona = s < 6 ? 0 : s < 62 ? 1 : s < 165 ? 2 : 3;
        M.bin[c] = (a < 0 || a >= NANG) ? 255 : (u8)(a * NZONA + zona);
        M.rcel[c] = sqrtf(X * X + Z * Z + ALTO_VENTANA * ALTO_VENTANA);
    }
    M.alfa = 0.6f; M.beta = 0.3f; M.a_sw = M.a_ws = 1;
}
static void mar_nivel_inicial(float z0) {
    M.zeta0 = z0;
    for (int c = 0; c < NXP * NZP; c++) M.eta[c] = M.hb[c] + z0 > 0 ? z0 : -M.hb[c];
}

/* ---------------- un paso de 1/16 s ---------------- */
/* reparto dinámico: cada hilo toma bloques de 4 filas a medida que termina, así los núcleos rápidos hacen más */
static inline int tomar(int total) { LONG k = InterlockedIncrement(&M.sig) - 1; return k < total ? (int)k : -1; }
#define POR_FILAS for (int _b; (_b = tomar((NZ + 3) / 4)) >= 0;) for (int j = _b * 4, _jf = j + 4 > NZ ? NZ : j + 4; j < _jf; j++)
static void correr(tarea fn) { M.sig = 0; piscina_correr(M.P, fn); }
#define CDT (DTM / DXM)

/* mar adentro la marejada es casi lineal; la no linealidad se enciende entre 6 y 4 m de fondo */
static inline float peso_nl(float hf) { return 1 - suave((hf - 4.0f) / 2.0f); }
/* dispersión plena desde 5,5 m de fondo y aguas someras desde 4 m hacia la orilla */
static inline float peso_disp(float hf, int j) { return suave((hf - 4.0f) / 1.5f) * suave((float)(NZ - NBORDE - j) / 8.0f); }
/* la ola que revienta en agua honda no forma un resalto: pierde su energía en la espuma de la cresta */
static inline float rodillo(float frac, float hf) { return frac * 0.12f * peso_nl(hf); }
static inline float raiz3_inv(float x) {   /* x^(-1/3) con dos pasos de Newton desde una semilla por bits */
    union { float f; u32 i; } u = { x }; u.i = 0x54A2FA8C - u.i / 3; float y = u.f;
    y = y * (4.0f / 3) - x * y * y * y * y * (1.0f / 3); y = y * (4.0f / 3) - x * y * y * y * y * (1.0f / 3); return y;
}
static inline float manning(float H) { return GRAV * 0.02f * 0.02f * raiz3_inv(fmaxf(H, 0.05f)); }

static void fase_flujos(int id, int n) {
     float z0 = M.zeta0;
    POR_FILAS {
        const float *e = M.eta + IX(j, 0), *h = M.hb + IX(j, 0), *uu = M.u + IX(j, 0), *vv = M.v + IX(j, 0);
        float *qx = M.qx + IX(j, 0), *qz = M.qz + IX(j, 0);
        for (int i = 0; i < NX; i++) {
            float uf = uu[i], Dl = fmaxf(h[i] + e[i], 0), Dr = fmaxf(h[i + 1] + e[i + 1], 0), Dh;
            if (uf > 0) { if (Dl < SECO) Dh = 0; else Dh = Dl + 0.5f * minmod(Dl - fmaxf(h[i - 1] + e[i - 1], 0), Dr - Dl); }
            else { if (Dr < SECO) Dh = 0; else Dh = Dr - 0.5f * minmod(Dr - Dl, fmaxf(h[i + 2] + e[i + 2], 0) - Dr); }
            float hs = 0.5f * (h[i] + h[i + 1]) + z0; if (hs > 4) Dh = hs + peso_nl(hs) * (Dh - hs);
            qx[i] = Dh * uf;
        }
        fila_periodica(qx);
        const float *e1 = e + NXP, *h1 = h + NXP, *e2 = e + 2 * NXP, *h2 = h + 2 * NXP, *em = e - NXP, *hm = h - NXP;
        for (int i = 0; i < NX; i++) {
            float vf = vv[i], Dd = fmaxf(h[i] + e[i], 0), Du = fmaxf(h1[i] + e1[i], 0), Dh;
            if (vf > 0) { if (Dd < SECO) Dh = 0; else Dh = Dd + 0.5f * minmod(Dd - fmaxf(hm[i] + em[i], 0), Du - Dd); }
            else { if (Du < SECO) Dh = 0; else Dh = Du - 0.5f * minmod(Du - Dd, fmaxf(h2[i] + e2[i], 0) - Du); }
            float hs = 0.5f * (h[i] + h1[i]) + z0; if (hs > 4) Dh = hs + peso_nl(hs) * (Dh - hs);
            qz[i] = Dh * vf;
        }
        fila_periodica(qz);
    }
}
static void fase_limite(int id, int n) {   /* ninguna celda puede entregar más agua de la que tiene */
    
    POR_FILAS {
        const float *qx = M.qx + IX(j, 0), *qz = M.qz + IX(j, 0), *qzm = qz - NXP, *e = M.eta + IX(j, 0), *h = M.hb + IX(j, 0);
        float *rf = M.rf + IX(j, 0);
        for (int i = 0; i < NX; i++) {
            float sale = CDT * (fmaxf(qx[i], 0) + fmaxf(-qx[i - 1], 0) + fmaxf(qz[i], 0) + fmaxf(-qzm[i], 0)), D = fmaxf(h[i] + e[i], 0);
            rf[i] = sale > D && sale > 0 ? D / sale : 1;
        }
        fila_periodica(rf);
    }

}
static void fase_continuidad(int id, int n) {
    
    float a = M.alfa, b = M.beta, z0 = M.zeta0, kw = DTM / 0.4f;
    POR_FILAS {
        const float *qx = M.qx + IX(j, 0), *qz = M.qz + IX(j, 0), *qzm = qz - NXP, *rf = M.rf + IX(j, 0), *rfu = rf + NXP, *rfd = rf - NXP, *h = M.hb + IX(j, 0);
        const u8 *bo = M.brk + IX(j, 0); u8 *bn = M.brk2 + IX(j, 0);
        float *e = M.eta + IX(j, 0), *qxs = M.qxs + IX(j, 0), *qzs = M.qzs + IX(j, 0), *rd = M.rod + IX(j, 0), *wd = M.wd + IX(j, 0);
        int borde = j >= NZ - NBORDE;
        for (int i = 0; i < NX; i++) {
            float fr = qx[i] * (qx[i] > 0 ? rf[i] : rf[i + 1]), fl = qx[i - 1] * (qx[i - 1] > 0 ? rf[i - 1] : rf[i]);
            float fu = qz[i] * (qz[i] > 0 ? rf[i] : rfu[i]), fd = qzm[i] * (qzm[i] > 0 ? rfd[i] : rf[i]);
            qxs[i] = fr; qzs[i] = fu;
            float e0 = e[i], e1 = e0 - CDT * (fr - fl + fu - fd);
            if (h[i] + e1 < 0) e1 = -h[i];
            e[i] = e1;
            float D = h[i] + e1, et = (e1 - e0) / DTM, c = sqrtf(GRAV * fmaxf(D, 0.05f));
            int vec = bo[i] | bo[i - 1] | bo[i + 1] | bo[i - NXP] | bo[i + NXP];
            /* revienta por la cara (Smit y otros), o por una cresta o un valle que el fondo ya no aguanta (H > 0,8 h) */
            float cresta = e1 - z0, hs = h[i] + z0, fn = peso_nl(hs), gc = 0.42f + 0.18f * fn, gt = 0.38f + 0.07f * fn;
            bn[i] = !borde && D > 0.15f && (et > a * c || (vec && et > b * c) || (hs > 0.5f && ((cresta > gc * hs && et > 0) || (vec && cresta > 0.8f * gc * hs) || (fn > 0.5f && -cresta > gt * hs))));
            /* donde revienta, el agua queda turbulenta un rato y sin el sostén de la dispersión, que vuelve de a poco */
            rd[i] = bn[i] ? T_RODILLO : fmaxf(rd[i] - DTM, 0);
            float obj = rd[i] > 0 && fn > 0.5f ? 0 : 1; wd[i] += clampf(obj - wd[i], -kw, kw);
        }
        fila_periodica(qxs); fila_periodica(qzs); fila_periodica(e); fila_periodica(rd); fila_periodica(wd);
        bn[-2] = bn[NX - 2]; bn[-1] = bn[NX - 1]; bn[NX] = bn[0]; bn[NX + 1] = bn[1];
    }

}

/* sistema tridiagonal periódico (Sherman y Morrison) */
static void tri_ciclico(int N, const float *a, const float *b, const float *c, float *d, float *w) {
    float *cp = w, *y = w + N, *z = w + 2 * N;
    if (a[0] == 0 && c[N - 1] == 0) {
        float m = b[0]; cp[0] = c[0] / m; y[0] = d[0] / m;
        for (int i = 1; i < N; i++) { m = b[i] - a[i] * cp[i - 1]; float im = 1 / m; cp[i] = c[i] * im; y[i] = (d[i] - a[i] * y[i - 1]) * im; }
        d[N - 1] = y[N - 1]; for (int i = N - 2; i >= 0; i--) d[i] = y[i] - cp[i] * d[i + 1];
        return;
    }
    float g = -b[0], b0 = b[0] - g, bn = b[N - 1] - c[N - 1] * a[0] / g;
    float m = b0; cp[0] = c[0] / m; y[0] = d[0] / m; z[0] = g / m;
    for (int i = 1; i < N; i++) {
        float bi = i == N - 1 ? bn : b[i]; m = bi - a[i] * cp[i - 1]; float im = 1 / m;
        cp[i] = c[i] * im; y[i] = (d[i] - a[i] * y[i - 1]) * im; z[i] = ((i == N - 1 ? c[N - 1] : 0) - a[i] * z[i - 1]) * im;
    }
    for (int i = N - 2; i >= 0; i--) { y[i] -= cp[i] * y[i + 1]; z[i] -= cp[i] * z[i + 1]; }
    float f = (y[0] + a[0] * y[N - 1] / g) / (1 + z[0] + a[0] * z[N - 1] / g);
    for (int i = 0; i < N; i++) d[i] = y[i] - f * z[i];
}

/* La dispersión se arma con pesos por arista: dos caras vecinas se acoplan con el mismo peso en los dos
   sentidos, y el mismo operador L corrige la presión. Así cada modo queda multiplicado por un número entre
   0 y 1 y el esquema no puede inventar energía donde el agua pasa de no reventar a reventar. */
#define I2 (1.0f / (DXM * DXM))
static void fase_u(int id, int n) {
    
    float *A = M.tri[id], *B = A + NX, *C = B + NX, *Dd = C + NX, *W = Dd + NX, *Hb = W + 3 * NX, *Wd = Hb + NX, *HF = Wd + NX, *G = HF + NX, *Fx = G + NX;
    float tvx = 1.22f * 1.3e-3f * sqrtf(M.vx * M.vx + M.vz * M.vz) * M.vx / RHO, z0 = M.zeta0;
    eco *ac = &M.acc[id];
    POR_FILAS {
        const float *e = M.eta + IX(j, 0), *h = M.hb + IX(j, 0), *u = M.u + IX(j, 0), *qxs = M.qxs + IX(j, 0), *wc = M.wd + IX(j, 0);
        const float *qzs = M.qzs + IX(j, 0), *qzm = qzs - NXP, *v = M.v + IX(j, 0), *vm = v - NXP;
        const u8 *bk = M.brk2 + IX(j, 0);
        int alguno = 0;
        for (int i = 0; i < NX; i++) {
            float Dl = fmaxf(h[i] + e[i], 0), Dr = fmaxf(h[i + 1] + e[i + 1], 0);
            if ((Dl < SECO && Dr < SECO) || (Dl < SECO && -h[i] >= e[i + 1]) || (Dr < SECO && -h[i + 1] >= e[i])) { Fx[i] = -u[i] / DTM; Hb[i] = -1; Wd[i] = 0; G[i] = 0; HF[i] = 0; continue; }
            float H = fmaxf(0.5f * (Dl + Dr), 0.01f); Hb[i] = H;
            G[i] = e[i + 1] - e[i];
            float qb0 = 0.5f * (qxs[i - 1] + qxs[i]), qb1 = 0.5f * (qxs[i] + qxs[i + 1]);
            float us0 = qb0 > 0 ? u[i - 1] : u[i], us1 = qb1 > 0 ? u[i] : u[i + 1];
            float ax = (qb1 * us1 - qb0 * us0 - u[i] * (qb1 - qb0)) / H;
            float pu = 0.5f * (qzs[i] + qzs[i + 1]), pd = 0.5f * (qzm[i] + qzm[i + 1]);
            float c0 = u[i], cm = u[i - NXP], cp = u[i + NXP];
            float uu = pu > 0 ? c0 : cp, ud = pd > 0 ? cm : c0;
            float az = (pu * uu - pd * ud - c0 * (pu - pd)) / H;
            float hf = 0.5f * (h[i] + h[i + 1]) + z0, fn = peso_nl(hf);
            Fx[i] = (-GRAV * G[i] - fn * (ax + az)) / DXM + tvx / fmaxf(H, 0.3f);
            HF[i] = hf;
            Wd[i] = peso_disp(hf, j) * fminf(wc[i], wc[i + 1]);
        }
        for (int i = 0; i < NX; i++) {
            int im = i ? i - 1 : NX - 1, ip = i < NX - 1 ? i + 1 : 0;
            float cl = fminf(Wd[i], Wd[im]), cr = fminf(Wd[i], Wd[ip]), F = Fx[i];
            if (cl + cr > 0) {
                float al = ALFA_D * HF[i] * HF[i] * I2, bg = BMS * GRAV * HF[i] * HF[i] * I2 / DXM;
                F += bg * (cl * (G[im] - G[i]) + cr * (G[ip] - G[i]));
                A[i] = -al * cl; C[i] = -al * cr; B[i] = 1 + al * (cl + cr);
                if (cl > 0.99f && cr > 0.99f) {   /* fondo en pendiente: Madsen y Sørensen 1992, segunda parte */
                    const float *E = e + i; float dh = h[i + 1] - h[i], be = BETA_D * HF[i] * dh * 0.5f * I2;
                    A[i] += be; C[i] -= be; F += BMS * GRAV * HF[i] * dh * (E[2] - E[1] - E[0] + E[-1]) * I2 / DXM;
                }
                alguno = 1;
            } else { A[i] = C[i] = 0; B[i] = 1; }
            Dd[i] = DTM * F;
        }
        if (alguno) tri_ciclico(NX, A, B, C, Dd, W);
        float *un = M.un + IX(j, 0);
        for (int i = 0; i < NX; i++) {
            float x = u[i] + Dd[i];
            if (Hb[i] < 0) x = 0;
            else {
                float vb = 0.25f * (v[i] + v[i + 1] + vm[i] + vm[i + 1]), sp = sqrtf(x * x + vb * vb);
                const float *rr = M.rod + IX(j, i);
                float fr = (rr[0] + rr[1] + rr[-NXP] + rr[1 - NXP] + rr[NXP] + rr[1 + NXP]) * (1.0f / (6 * T_RODILLO));
                x /= 1 + DTM * (manning(Hb[i]) * sp / Hb[i] + rodillo(fr, HF[i]));
                x = clampf(x, -12, 12);
            }
            un[i] = x;
        }
        fila_periodica(un);
        /* la energía que pierde cada frente que revienta: resalto de altura H en agua de profundidad D */
        float *fs = M.fsrc + IX(j, 0); const u8 *b0 = M.brk + IX(j, 0); const float *rc = M.rcel + IX(j, 0); const u8 *bn = M.bin + IX(j, 0);
        for (int i = 0; i < NX; i++) {
            fs[i] = 0; if (!bk[i]) continue;
            const float *E = e + i; float mx = E[0], mn = E[0];
            for (int k = 1; k <= 2; k++) {
                float a1 = E[k], a2 = E[-k], a3 = E[k * NXP], a4 = E[-k * NXP];
                mx = fmaxf(mx, fmaxf(fmaxf(a1, a2), fmaxf(a3, a4))); mn = fminf(mn, fminf(fminf(a1, a2), fminf(a3, a4)));
            }
            float D = fmaxf(h[i] + e[i], 0.1f), Hf = fminf(mx - fmaxf(mn, -h[i]), 1.2f * D);
            float eps = RHO * GRAV * Hf * Hf * Hf * sqrtf(GRAV * D) / (4 * D * 3);   /* W/m² repartidos en 3 m de frente */
            fs[i] = eps;
            /* una ola que empieza a reventar donde nada reventaba: el golpe y el rocío */
            int nuevo = !(b0[i] | b0[i - 1] | b0[i + 1] | b0[i - NXP] | b0[i + NXP]);
            if (nuevo && eps > 9000) {
                LONG k = InterlockedIncrement(&M.nest) - 1; estallido *s = &M.est[k & (NEST - 1)];
                s->x = (i + 0.5f - NX / 2) * DXM; s->z = ZPRIM + (j + 0.5f) * DXM; s->f = eps; s->t = (float)M.t;
            }
            int bb = bn[i]; if (bb == 255) continue;
            float pw = eps * DXM * DXM;
            ac->pot[bb] += pw; ac->rd[bb] += pw * rc[i];
            if (nuevo) ac->ini[bb] += pw;
        }
    }
}

static void entrada_bloque(int i0, int nb, double t) {   /* la marejada que llega por el borde, sumada componente a componente */
    float et[16], vi[16];
    for (int q = 0; q < nb; q++) et[q] = vi[q] = 0;
    float x0 = (i0 + 0.5f - NX / 2) * DXM;
    for (int k = 0; k < M.nc; k++) {
        const componente *c = &M.c[k]; float amp = c->a * (c->viento ? M.a_ws : M.a_sw) * M.rampa;
        if (amp < 1e-5f) continue;
        double fa = (double)c->kx * x0 - (double)c->w * t + c->ph;
        fa = fmod(fa, 2 * 3.14159265358979); float cr = cosf((float)fa), ci = sinf((float)fa), rr = cosf(c->kx * DXM), ri = sinf(c->kx * DXM);
        float vu = -c->U * c->cs * amp / c->a;
        for (int q = 0; q < nb; q++) { et[q] += amp * cr; vi[q] += vu * cr; float t2 = cr * rr - ci * ri; ci = cr * ri + ci * rr; cr = t2; }
    }
    for (int q = 0; q < nb; q++) { M.etain[i0 + q] = et[q] + M.zeta0; M.vin[i0 + q] = vi[q]; }
}

static void fase_v(int id, int n) {
    const int S = NZ * 16;
    float *T = M.tri[id] + 12 * NX, *A = T, *B = A + S, *C = B + S, *R = C + S, *Hb = R + S, *cp = Hb + S, *Wd = cp + S, *HF = Wd + S, *G = HF + S;
    float tvz = 1.22f * 1.3e-3f * sqrtf(M.vx * M.vx + M.vz * M.vz) * M.vz / RHO, z0 = M.zeta0, c0 = sqrtf(GRAV / (H0 + z0));
    double t1 = M.t + DTM;
    for (int bl; (bl = tomar(NX / 16)) >= 0;) {
        int i0 = bl * 16;
        entrada_bloque(i0, 16, t1);
        for (int j = 0; j < NZ; j++) {
            for (int q = 0; q < 16; q++) {
                int i = i0 + q, c = IX(j, i), r = j * 16 + q;
                const float *e = M.eta + c, *h = M.hb + c, *v = M.v + c;
                Wd[r] = 0; G[r] = 0; HF[r] = 0;
                if (j == NZ - 1) {   /* borde: entra la marejada y sale lo que vuelve de la playa */
                    float vb = M.vin[i] + c0 * (e[0] - M.etain[i]);
                    R[r] = vb - v[0]; Hb[r] = H0; continue;
                }
                float Dd = fmaxf(h[0] + e[0], 0), Du = fmaxf(h[NXP] + e[NXP], 0);
                if ((Dd < SECO && Du < SECO) || (Dd < SECO && -h[0] >= e[NXP]) || (Du < SECO && -h[NXP] >= e[0])) { R[r] = -v[0]; Hb[r] = -1; continue; }
                float H = fmaxf(0.5f * (Dd + Du), 0.01f); Hb[r] = H;
                G[r] = e[NXP] - e[0];
                const float *qz = M.qzs + c, *qx = M.qxs + c;
                float qb0 = 0.5f * (qz[-NXP] + qz[0]), qb1 = 0.5f * (qz[0] + qz[NXP]);
                float vm = v[-NXP], vp = v[NXP], vc = v[0];
                float vs0 = qb0 > 0 ? vm : vc, vs1 = qb1 > 0 ? vc : vp;
                float az = (qb1 * vs1 - qb0 * vs0 - vc * (qb1 - qb0)) / H;
                float pr = 0.5f * (qx[0] + qx[NXP]), pl = 0.5f * (qx[-1] + qx[NXP - 1]);
                float vr = pr > 0 ? vc : v[1], vl = pl > 0 ? v[-1] : vc;
                float ax = (pr * vr - pl * vl - vc * (pr - pl)) / H;
                float hf = 0.5f * (h[0] + h[NXP]) + z0, fn = peso_nl(hf);
                R[r] = DTM * ((-GRAV * G[r] - fn * (az + ax)) / DXM + tvz / fmaxf(H, 0.3f));
                HF[r] = hf;
                Wd[r] = j > 0 ? peso_disp(hf, j) * fminf(M.wd[c], M.wd[c + NXP]) : 0;
            }
        }
        for (int j = 0; j < NZ; j++) for (int q = 0; q < 16; q++) {
            int r = j * 16 + q;
            float cl = j > 0 ? fminf(Wd[r], Wd[r - 16]) : 0, cr = j < NZ - 1 ? fminf(Wd[r], Wd[r + 16]) : 0;
            if (cl + cr > 0) {
                float al = ALFA_D * HF[r] * HF[r] * I2, bg = BMS * GRAV * HF[r] * HF[r] * I2 / DXM;
                float F = bg * (cl * (G[r - 16] - G[r]) + cr * (G[r + 16] - G[r]));
                A[r] = -al * cl; C[r] = -al * cr; B[r] = 1 + al * (cl + cr);
                if (cl > 0.99f && cr > 0.99f) {
                    int c = IX(j, i0 + q); const float *e = M.eta + c, *h = M.hb + c;
                    float dh = h[NXP] - h[0], be = BETA_D * HF[r] * dh * 0.5f * I2;
                    A[r] += be; C[r] -= be; F += BMS * GRAV * HF[r] * dh * (e[2 * NXP] - e[NXP] - e[0] + e[-NXP]) * I2 / DXM;
                }
                R[r] += DTM * F;
            } else { A[r] = C[r] = 0; B[r] = 1; }
        }
        /* Thomas a lo largo de cada columna, 16 columnas a la vez */
        for (int q = 0; q < 16; q++) { float m = B[q]; cp[q] = C[q] / m; R[q] /= m; }
        for (int j = 1; j < NZ; j++) for (int q = 0; q < 16; q++) {
            int r = j * 16 + q; float m = B[r] - A[r] * cp[r - 16], im = 1 / m; cp[r] = C[r] * im; R[r] = (R[r] - A[r] * R[r - 16]) * im;
        }
        for (int j = NZ - 2; j >= 0; j--) for (int q = 0; q < 16; q++) { int r = j * 16 + q; R[r] -= cp[r] * R[r + 16]; }
        for (int j = 0; j < NZ; j++) for (int q = 0; q < 16; q++) {
            int i = i0 + q, c = IX(j, i), r = j * 16 + q; float x = M.v[c] + R[r];
            if (Hb[r] < 0) x = 0;
            else if (j < NZ - 1) {
                float ub = 0.25f * (M.un[c] + M.un[c - 1] + M.un[c + NXP] + M.un[c + NXP - 1]), sp = sqrtf(x * x + ub * ub);
                const float *rr = M.rod + c;
                float fr = (rr[0] + rr[NXP] + rr[-1] + rr[1] + rr[NXP - 1] + rr[NXP + 1]) * (1.0f / (6 * T_RODILLO));
                x /= 1 + DTM * (manning(Hb[r]) * sp / Hb[r] + rodillo(fr, HF[r]));
                x = clampf(x, -12, 12);
            }
            M.vn[c] = x;
        }
    }
}

/* ---------------- después del paso: la espuma viaja con el agua, se gasta y deja encaje ---------------- */
static inline float muestra_u(const float *u, float gx, float gz) {   /* u vive en (i+1, j+0,5), en celdas */
    float x = gx - 1, z = gz - 0.5f; int i = (int)floorf(x), j = (int)floorf(z); float fx = x - i, fz = z - j;
    j = j < 0 ? 0 : j > NZ - 2 ? NZ - 2 : j; i = ((i % NX) + NX) % NX;
    const float *p = u + IX(j, i);
    return (p[0] + (p[1] - p[0]) * fx) * (1 - fz) + (p[NXP] + (p[NXP + 1] - p[NXP]) * fx) * fz;
}
static inline float muestra_v(const float *v, float gx, float gz) {   /* v vive en (i+0,5, j+1) */
    float x = gx - 0.5f, z = gz - 1; int i = (int)floorf(x), j = (int)floorf(z); float fx = x - i, fz = z - j;
    j = j < 0 ? 0 : j > NZ - 2 ? NZ - 2 : j; i = ((i % NX) + NX) % NX;
    const float *p = v + IX(j, i);
    return (p[0] + (p[1] - p[0]) * fx) * (1 - fz) + (p[NXP] + (p[NXP + 1] - p[NXP]) * fx) * fz;
}
static inline float muestra_c(const float *a, float gx, float gz) {   /* campos de celda viven en (i+0,5, j+0,5) */
    float x = gx - 0.5f, z = gz - 0.5f; int i = (int)floorf(x), j = (int)floorf(z); float fx = x - i, fz = z - j;
    if (j < 0) { j = 0; fz = 0; } if (j > NZ - 2) { j = NZ - 2; fz = 1; } i = ((i % NX) + NX) % NX;
    const float *p = a + IX(j, i);
    return (p[0] + (p[1] - p[0]) * fx) * (1 - fz) + (p[NXP] + (p[NXP + 1] - p[NXP]) * fx) * fz;
}

static void fase_despues(int id, int n) {
    eco *ac = &M.acc[id]; float dec = expf(-DTM / TAU_BLANCA), dr = expf(-DTM / TAU_RESTO), area = DXM * DXM;
    /* la espuma que nace al reventar recibe un dibujo nuevo, el de esta hora, que cambia despacio */
    float fu = 37 * sinf(0.021f * (float)M.t), fv = 29 * cosf(0.017f * (float)M.t);
    POR_FILAS {
        float *W2 = M.W2 + IX(j, 0), *R2 = M.R2 + IX(j, 0), *du2 = M.du2 + IX(j, 0), *dv2 = M.dv2 + IX(j, 0), *sc = M.seco + IX(j, 0);
        const float *u = M.u + IX(j, 0), *v = M.v + IX(j, 0), *vm = v - NXP, *e = M.eta + IX(j, 0), *h = M.hb + IX(j, 0), *fs = M.fsrc + IX(j, 0);
        const float *Wv = M.W + IX(j, 0), *Rv = M.R + IX(j, 0), *dua = M.du + IX(j, 0), *dva = M.dv + IX(j, 0);
        const u8 *bn = M.bin + IX(j, 0);
        for (int i = 0; i < NX; i++) {
            float D = h[i] + e[i];
            float uc = 0.5f * (u[i - 1] + u[i]), vc = 0.5f * (vm[i] + v[i]), gx = i + 0.5f - uc * CDT, gz = j + 0.5f - vc * CDT;
            float w = 0, r = 0;
            if (D > SECO || Wv[i] > 0.001f) w = muestra_c(M.W, gx, gz);
            if (D > SECO || Rv[i] > 0.001f) r = muestra_c(M.R, gx, gz);
            float w2 = w * dec + fs[i] * DTM / 1500.0f; if (w2 > 1.5f) w2 = 1.5f; if (D < SECO) w2 *= 0.85f;
            /* el agua blanca que se apaga deja espuma flotando, que la corriente lleva y se deshace en decenas de segundos */
            float r2 = r * dr + (D > SECO ? K_RESTO * w * (1 - dec) : 0); if (r2 > 1) r2 = 1; if (D < SECO) r2 *= 0.9f;
            W2[i] = w2; R2[i] = r2;
            /* de dónde viene esta agua: el dibujo de la espuma viaja con ella; donde revienta una ola, el agua se revuelve y el dibujo es otro */
            float bu = dua[i], bv = dva[i];
            if (D > SECO) { bu = muestra_c(M.du, gx, gz) - uc * CDT; bv = muestra_c(M.dv, gx, gz) - vc * CDT; }
            float s = clampf(fs[i] * (DTM / 1500.0f) * 25, 0, 1);
            if (w2 + r2 < 0.01f) s = fmaxf(s, 0.005f);   /* sin espuma, el dibujo vuelve despacio a su lugar: no se ve */
            du2[i] = bu + (fu - bu) * s; dv2[i] = bv + (fv - bv) * s;
            if (h[i] < 1.0f) { if (D > 0.01f) sc[i] = 0; else sc[i] += DTM; }
            int bb = bn[i];
            if (bb != 255 && D > SECO && h[i] < 0.6f) {   /* la orilla: el agua que sube por la arena y la que vuelve */
                int a = bb / NZONA; float sp = uc * uc + vc * vc, pw = RHO * D * sp * sqrtf(sp) * area;
                if (vc < 0) ac->sube[a] += pw; else ac->baja[a] += pw;
            }
            if (bb != 255 && (bb % NZONA) <= 1) ac->burb[bb / NZONA] += (w2 + 0.3f * r2) * area;
        }
        fila_periodica(W2); fila_periodica(R2); fila_periodica(du2); fila_periodica(dv2);
    }
}

#ifdef MEDIR_FASES
static double g_fase_t[8];
static double reloj(void) { LARGE_INTEGER c, f; QueryPerformanceCounter(&c); QueryPerformanceFrequency(&f); return (double)c.QuadPart / f.QuadPart; }
#define MEDIR(k, x) do { double _t = reloj(); x; g_fase_t[k] += reloj() - _t; } while (0)
#else
#define MEDIR(k, x) x
#endif
static void mar_paso(void) {
    for (int i = 0; i < M.P->n; i++) ZeroMemory(&M.acc[i], sizeof(eco));
    MEDIR(0, correr(fase_flujos));
    MEDIR(1, correr(fase_limite));
    for (int i = -2; i < NX + 2; i++) { M.rf[IX(NZ, i)] = M.rf[IX(NZ + 1, i)] = M.rf[IX(-1, i)] = 1; }
    MEDIR(2, correr(fase_continuidad));
    fantasma_tierra(M.eta, 0); fantasma_tierra(M.wd, 0); fantasma_mar(M.eta); fantasma_mar(M.wd);
    MEDIR(3, correr(fase_u));
    MEDIR(4, correr(fase_v));
    { float *t = M.u; M.u = M.un; M.un = t; t = M.v; M.v = M.vn; M.vn = t; u8 *b = M.brk; M.brk = M.brk2; M.brk2 = b; }
    for (int j = 0; j < NZ; j++) fila_periodica(M.v + IX(j, 0));
    filas_fantasma(M.u, 1); filas_fantasma(M.v, 1);
    for (int i = -2; i < NX + 2; i++) { M.v[IX(-1, i)] = M.v[IX(-2, i)] = 0; }
    MEDIR(5, correr(fase_despues));
    { float *t = M.W; M.W = M.W2; M.W2 = t; t = M.R; M.R = M.R2; M.R2 = t; t = M.du; M.du = M.du2; M.du2 = t; t = M.dv; M.dv = M.dv2; M.dv2 = t; }
    filas_fantasma(M.W, 0); filas_fantasma(M.R, 0); filas_fantasma(M.du, 0); filas_fantasma(M.dv, 0);
    M.t += DTM; M.pasos++;
    /* el eco de este paso para el sonido */
    LONG k = M.neco; eco *e = &M.ecos[k & (NECO - 1)]; ZeroMemory(e, sizeof *e); e->t = M.t;
    for (int q = 0; q < M.P->n; q++) {
        eco *a = &M.acc[q];
        for (int b = 0; b < NBIN; b++) { e->pot[b] += a->pot[b]; e->ini[b] += a->ini[b]; e->rd[b] += a->rd[b]; }
        for (int b = 0; b < NANG; b++) { e->sube[b] += a->sube[b]; e->baja[b] += a->baja[b]; e->burb[b] += a->burb[b]; }
    }
    MemoryBarrier(); M.neco = k + 1;
}
