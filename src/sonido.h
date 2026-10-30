/* El sonido del Pacífico de Pellines, oído desde la pieza.
   - El mar no tiene olas programadas: cada trozo de rompiente suena con la energía que de verdad pierde en la
     simulación, repartido en 18 direcciones y 4 franjas (orilla, barra interior, barra exterior, afuera). Cada
     fuente llega con el retardo de su distancia real hasta la ventana y con los agudos que el aire se come.
   - Cuando una ola empieza a reventar donde nada reventaba, suena el golpe grave del labio que cae.
   - En la orilla se oye el agua que sube por la arena y la que vuelve arrastrando arena, y la espuma que chispea.
   - El viento es lento: una media que cambia en minutos y ráfagas suaves de decenas de segundos. Se oye en el
     pasto de la duna y como un soplo grave alrededor de la casa, nunca como un silbido.
   - La lluvia cae gota a gota, con los tamaños de la distribución de Marshall y Palmer, sobre un techo de zinc
     forrado en madera; además pica en el vidrio, gotea del alero a un charco y corre por la canaleta, que sigue
     goteando un rato después de que para.
   - El vidrio deja pasar el grave y apaga lo agudo; la pieza suma sus primeras reflexiones y una cola corta. */

#define K 32
#define NBURB 96
#define LEAD 0.2

/* ---------------- la Play en la pieza ---------------- */
#define TVN 65536
typedef struct { u64 fase, paso; float nivel, amp, inc1, inc2, rel; u32 t, hold; u8 capa, activa, etapa; } tvvoz;
static const struct { float lo, hi, ataque, vol, f0, f1, s0, s1, peso; } TVINS[4] = {
    { 35, 140, 1.6f, 0.60f, -10, -14, -5, 3, 0.30f }, { 160, 650, 2.7f, 0.80f, -10, -4, -4, 4, 0.27f },
    { 300, 2600, 2.2f, 0.45f, -10, 2, -5, 5, 0.33f }, { 1500, 9000, 3.8f, 0.22f, -6, -4, -3, 2, 0.10f } };

#define FDN 8
static const int FDN_LEN[FDN] = { 1031, 1327, 1523, 1871, 2053, 2381, 2699, 3001 };
static const int CUARTO_LEN[4] = { 613, 787, 1031, 1201 };

typedef struct { float r, gl, gr, ret, roar, golpe, crack, roar_o, crack_o; ruido rn; svf f1, fcr, fg; } fuente_mar;
typedef struct { float r, gl, gr, ret, sube, baja, sube_o, baja_o; ruido rn; svf fs, fb; } fuente_orilla;
typedef struct { int activa; float fase, w, dw, wmax, amp, dec, gl, gr; } burbuja;
typedef struct { float t, base, gl, gr; } gotera;
typedef struct { float amp, dec, lp, k, gl, gr; } chasquido;   /* una gota contra el zinc: un golpe corto de ruido */

static struct {
    u32 rng; u64 muestra; double t0; float luz;
    fuente_mar f[NBIN]; fuente_orilla o[NANG]; burbuja b[NBURB]; float burb_acc, burb_peso[NANG], burb_tot;
    ruido rsub; svf fsub; float sub, sub_o;
    /* viento */
    float U, Umed, Ubase, g, rus, rus_o, dir; ruido rv1, rv2; svf fv1, fv2;
    /* lluvia */
    int lluvia_modo, lloviendo; float R, Robj, Rmed, Rou, Rflujo; double prox_chubasco;
    modo techo[4][7]; float tin[4], tgl[4], tgr[4]; svf ftl, fth, ftr, fthr; float tic, tic_lp, techo_acc, fondo_acc;
    chasquido ch[64]; u32 rch; svf ffondo; float fondo_x;
    modo vidrio[5]; float vin, vgl, vgr, vidrio_acc;
    gotera got[5]; burbuja pl[10]; float sp_amp, sp_gl, sp_gr; svf fsp; ruido rsp;
    ruido rll; svf fll, fll2; float canal_acc;
    /* tele */
    float *tvtab[4]; tvvoz tv[32]; u64 tv_prox; float tv_on, tv_obj; svf tv_hp, tv_lp; float tv_rms_acc, tv_rms;
    /* el vidrio, la reverberación de afuera y la pieza */
    float vidrio_fc, vidrio_obj; int ventana; svf vl1, vl2, vr1, vr2;
    float fdn[FDN][3100]; int fdn_i[FDN]; float fdn_lp[FDN], fdn_g[FDN]; float pre[1600]; int pre_i;
    float cto[4][1300]; int cto_i[4]; float cto_lp[4], cto_g[4]; float er[2][2400]; int er_i;
    float vol, vol_obj, fundido, fundido_paso, rms_acc, rms; int rms_n;
    float mar_rms_acc, mar_rms, debug_pot; svf dcl, dcr;
} S;

/* ---------------- la tele ---------------- */
static void fft(double *re, double *im, int n, int inv) {
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit;
        if (i < j) { double t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
    }
    for (int len = 2; len <= n; len <<= 1) {
        double ang = 2 * 3.14159265358979 / len * (inv ? 1 : -1), wr = cos(ang), wi = sin(ang);
        for (int i = 0; i < n; i += len) {
            double cr = 1, ci = 0;
            for (int k = 0; k < len / 2; k++) {
                int a = i + k, b = a + len / 2; double xr = re[b] * cr - im[b] * ci, xi = re[b] * ci + im[b] * cr;
                re[b] = re[a] - xr; im[b] = im[a] - xi; re[a] += xr; im[a] += xi;
                double t = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = t;
            }
        }
    }
}
static void tv_tabla(float *t, double lo, double hi, double *re, double *im) {
    for (int i = 0; i < TVN; i++) { re[i] = xs(&S.rng) / 2147483648.0 - 1.0; im[i] = 0; }
    fft(re, im, TVN, 0);
    for (int k = 0; k <= TVN / 2; k++) {
        double f = (double)k * FS / TVN, xl = (f - lo * 0.65) / (0.7 * lo), xh = (f - hi * 0.65) / (0.7 * hi);
        xl = xl < 0 ? 0 : xl > 1 ? 1 : xl; xh = xh < 0 ? 0 : xh > 1 ? 1 : xh;
        double m = (0.5 - 0.5 * cos(3.14159265 * xl)) * (0.5 + 0.5 * cos(3.14159265 * xh));
        re[k] *= m; im[k] *= m; if (k > 0 && k < TVN / 2) { re[TVN - k] *= m; im[TVN - k] *= m; }
    }
    fft(re, im, TVN, 1);
    double pico = 1e-9; for (int i = 0; i < TVN; i++) if (fabs(re[i]) > pico) pico = fabs(re[i]);
    for (int i = 0; i < TVN; i++) t[i] = (float)(re[i] / pico);
}
static void tv_nota(void) {
    float x = uni(&S.rng, 0, 1), acum = 0; int c = 3;
    for (int i = 0; i < 4; i++) { acum += TVINS[i].peso; if (x <= acum) { c = i; break; } }
    float semi = uni(&S.rng, TVINS[c].s0, TVINS[c].s1) - 3 * (1 - S.luz), u = uni(&S.rng, 0.15f, 1), vel = u * sqrtf(u), hold = uni(&S.rng, 0.6f, 6.6f);
    for (int canal = 0; canal < 2; canal++) {
        int k = -1; float menor = 9;
        for (int i = 0; i < 32; i++) { if (!S.tv[i].activa) { k = i; break; } if (S.tv[i].nivel < menor) { menor = S.tv[i].nivel; k = i; } }
        tvvoz *v = &S.tv[k]; float fino = canal ? TVINS[c].f1 : TVINS[c].f0;
        v->paso = (u64)(pow(2.0, (semi + fino / 16.0) / 12.0) * 281474976710656.0); v->fase = (u64)uni(&S.rng, 0, TVN) << 48;
        float t1 = TVINS[c].ataque * 0.43f; v->inc1 = 0.75f / (t1 * FS); v->inc2 = 0.25f / ((TVINS[c].ataque - t1) * FS);
        v->rel = expf(-1.0f / (2.73f * FS)); v->amp = vel * TVINS[c].vol; v->nivel = 0; v->t = 0; v->hold = (u32)(hold * FS);
        v->capa = c; v->etapa = 0; v->activa = 1;
    }
    S.tv_prox += (u64)(-2.4 * log(1 - uni(&S.rng, 0, 0.999f)) * FS);
}
static inline float tv_muestra(void) {
    while (S.muestra >= S.tv_prox) tv_nota();
    float s = 0;
    for (int k = 0; k < 32; k++) {
        tvvoz *v = &S.tv[k]; if (!v->activa) continue;
        const float *t = S.tvtab[v->capa]; u32 i = (u32)(v->fase >> 48); float fr = (float)((v->fase >> 24) & 0xFFFFFF) * (1.0f / 16777216.0f);
        s += (t[i] + (t[(i + 1) & (TVN - 1)] - t[i]) * fr) * v->nivel * v->amp; v->fase += v->paso;
        if (v->t < v->hold) { if (v->etapa == 0) { v->nivel += v->inc1; if (v->nivel >= 0.75f) { v->nivel = 0.75f; v->etapa = 1; } } else if (v->etapa == 1) { v->nivel += v->inc2; if (v->nivel >= 1) { v->nivel = 1; v->etapa = 2; } } }
        else { v->nivel *= v->rel; if (v->nivel < 1e-4f) v->activa = 0; }
        v->t++;
    }
    return s * 0.5f;
}

/* ---------------- lo que dice la simulación, con el retardo de cada distancia ---------------- */
static void eco_leer(double t, const eco **a, const eco **b, float *fr) {
    LONG n = M.neco; static const eco cero;
    if (n < 2) { *a = *b = &cero; *fr = 0; return; }
    const eco *ult = &M.ecos[(n - 1) & (NECO - 1)];
    double d = (ult->t - t) / DTM; if (d < 0) d = 0; if (d > NECO - 8) d = NECO - 8; if (d > n - 2) d = n - 2;
    double kf = (n - 1) - d; LONG k = (LONG)floor(kf);
    *a = &M.ecos[k & (NECO - 1)]; *b = &M.ecos[(k + (k < n - 1)) & (NECO - 1)]; *fr = (float)(kf - k);
}

static void pan(float p, float *gl, float *gr) { *gl = cosf((p + 1) * PI / 4); *gr = sinf((p + 1) * PI / 4); }

static void sonido_fuentes(void) {   /* dónde queda cada trozo de mar respecto de la ventana */
    double sr[NBIN] = { 0 }; int nr[NBIN] = { 0 };
    for (int j = 0; j < NZ; j++) for (int i = 0; i < NX; i++) { int c = IX(j, i), b = M.bin[c]; if (b == 255 || M.hb[c] < 0.2f) continue; sr[b] += M.rcel[c]; nr[b]++; }
    for (int b = 0; b < NBIN; b++) {
        fuente_mar *f = &S.f[b]; int a = b / NZONA; float ang = (-80 + (a + 0.5f) * 160.0f / NANG) * PI / 180;
        f->r = nr[b] ? (float)(sr[b] / nr[b]) : 400; f->ret = f->r / 343.0f;
        pan(sinf(ang) * 0.8f, &f->gl, &f->gr);
        f->rn.s = xs(&S.rng) | 1;
        float fc = 900 + 7000 * expf(-f->r / 150);   /* el aire se come los agudos con la distancia */
        svf_fijar(&f->f1, fc, 0.6f); svf_fijar(&f->fcr, fminf(2600, fc * 1.3f), 0.9f); svf_fijar(&f->fg, 85, 0.75f);
    }
    for (int a = 0; a < NANG; a++) {
        fuente_orilla *o = &S.o[a]; float ang = (-80 + (a + 0.5f) * 160.0f / NANG) * PI / 180, X = ZORILLA * tanf(ang);
        o->r = sqrtf(X * X + ZORILLA * ZORILLA + ALTO_VENTANA * ALTO_VENTANA); o->ret = o->r / 343.0f;
        pan(sinf(ang) * 0.8f, &o->gl, &o->gr); o->rn.s = xs(&S.rng) | 1;
        svf_fijar(&o->fs, 1300 + 2500 * expf(-o->r / 120), 0.6f); svf_fijar(&o->fb, 3200, 0.7f);
    }
}

static void reverb_init(void) {
    for (int i = 0; i < FDN; i++) S.fdn_g[i] = powf(10, -3.0f * FDN_LEN[i] / (FS * 2.6f));
    for (int i = 0; i < 4; i++) S.cto_g[i] = powf(10, -3.0f * CUARTO_LEN[i] / (FS * 0.5f));
}

static void techo_iniciar(void) {   /* cuatro paños de zinc, cada uno con sus modos, y el vidrio de la ventana */
    static const float fb[7] = { 170, 310, 520, 790, 1200, 1900, 2900 };
    for (int z = 0; z < 4; z++) {
        for (int m = 0; m < 7; m++) {
            float f = fb[m] * uni(&S.rng, 0.75f, 1.3f), tau = m < 3 ? uni(&S.rng, 0.012f, 0.028f) : uni(&S.rng, 0.004f, 0.010f);
            modo_fijar(&S.techo[z][m], f, tau, 0.004f / (1 + m * 0.5f));
        }
        pan(-0.75f + 0.5f * z, &S.tgl[z], &S.tgr[z]);
    }
    svf_fijar(&S.ftl, 2000, 0.6f); svf_fijar(&S.ftr, 2000, 0.6f); svf_fijar(&S.fth, 110, 0.7f); svf_fijar(&S.fthr, 110, 0.7f); svf_fijar(&S.ffondo, 1800, 0.5f); S.rch = xs(&S.rng) | 1;
    static const float fv[5] = { 950, 1700, 2600, 3900, 5400 };
    for (int m = 0; m < 5; m++) modo_fijar(&S.vidrio[m], fv[m] * uni(&S.rng, 0.95f, 1.05f), uni(&S.rng, 0.004f, 0.012f), 0.02f / (1 + m * 0.4f));
    for (int g = 0; g < 5; g++) { S.got[g].base = uni(&S.rng, 0.5f, 1.6f); S.got[g].t = uni(&S.rng, 0, 2); pan(uni(&S.rng, -0.8f, 0.8f), &S.got[g].gl, &S.got[g].gr); }
    svf_fijar(&S.fsp, 3000, 0.8f); svf_fijar(&S.fll, 4200, 0.7f); svf_fijar(&S.fll2, 1500, 0.5f);
}

static void sint_iniciar(u32 semilla, float hora, void *mem) {
    S.rng = semilla ? semilla : 0x2545F491; S.luz = 0.5f - 0.5f * cosf(2 * PI * hora / 24);
    sonido_fuentes();
    S.rsub.s = xs(&S.rng) | 1; svf_fijar(&S.fsub, 55, 0.7f);
    S.rv1.s = xs(&S.rng) | 1; S.rv2.s = xs(&S.rng) | 1; S.rll.s = xs(&S.rng) | 1; S.rsp.s = xs(&S.rng) | 1;
    S.Ubase = uni(&S.rng, 2.5f, 7.5f); S.Umed = S.Ubase; S.U = S.Umed;
    S.lluvia_modo = 0; S.lloviendo = uni(&S.rng, 0, 1) < 0.5f; S.Rmed = uni(&S.rng, 1.5f, 6); S.R = S.Robj = S.lloviendo ? S.Rmed : 0; S.Rflujo = S.R;
    S.prox_chubasco = uni(&S.rng, 300, 1500);
    techo_iniciar();
    S.vidrio_fc = S.vidrio_obj = 1900; S.ventana = 0;
    reverb_init();
    double *re = (double *)mem, *im = re + TVN; float *tablas = (float *)(im + TVN);
    for (int c = 0; c < 4; c++) { S.tvtab[c] = tablas + c * TVN; tv_tabla(S.tvtab[c], TVINS[c].lo, TVINS[c].hi, re, im); }
    S.tv_prox = FS; S.tv_on = 1; S.tv_obj = 1;
    svf_fijar(&S.tv_hp, 180, 0.7f); svf_fijar(&S.tv_lp, 4800, 0.7f);
    S.vol = S.vol_obj = 0.5f; S.fundido_paso = 1.0f / (10 * FS);
    svf_fijar(&S.dcl, 28, 0.6f); svf_fijar(&S.dcr, 28, 0.6f);   /* lo que está bajo 28 Hz no se oye: solo mueve los parlantes */
}

/* calibración: de vatios que pierde el mar a amplitud en el oído (con la cabaña a 110 m el mar llega unos 3 dB más
   bajo y más apagado; se compensa la mitad para que el nivel siga parecido y lo que cambie sea la distancia) */
#define G_ROAR (0.0085f * 1.2f)
#define G_GOLPE (0.020f * 1.2f)
#define G_ORILLA (0.0030f * 1.2f)
#define G_SUB 0.00018f
static float g_solo_mar = 1, g_solo_viento = 1, g_solo_lluvia = 1;

static void controles(void) {
    float dtc = (float)K / FS; double tn = S.t0 + (double)S.muestra / FS;
    float total = 0;
    for (int b = 0; b < NBIN; b++) {
        fuente_mar *f = &S.f[b]; const eco *a, *c; float fr; eco_leer(tn - f->ret, &a, &c, &fr);
        float pot = a->pot[b] + (c->pot[b] - a->pot[b]) * fr, ini = a->ini[b] + (c->ini[b] - a->ini[b]) * fr;
        f->roar_o = G_ROAR * sqrtf(pot) / f->r; total += pot / f->r;
        f->crack_o = f->roar_o * (0.25f + 1.5f * clampf(ini / (pot + 1), 0, 1));
        float g = G_GOLPE * sqrtf(ini) / f->r; if (g > f->golpe) f->golpe = g;
        f->roar += (f->roar_o - f->roar) * 0.08f; f->crack += (f->crack_o - f->crack) * 0.08f; f->golpe *= 0.9982f;
    }
    S.debug_pot = total;
    S.sub_o = G_SUB * sqrtf(total); S.sub += (S.sub_o - S.sub) * 0.02f;
    S.burb_tot = 0;
    for (int q = 0; q < NANG; q++) {
        fuente_orilla *o = &S.o[q]; const eco *a, *c; float fr; eco_leer(tn - o->ret, &a, &c, &fr);
        float su = a->sube[q] + (c->sube[q] - a->sube[q]) * fr, ba = a->baja[q] + (c->baja[q] - a->baja[q]) * fr, bu = a->burb[q] + (c->burb[q] - a->burb[q]) * fr;
        o->sube_o = G_ORILLA * sqrtf(su) / o->r; o->baja_o = G_ORILLA * 0.8f * sqrtf(ba) / o->r;
        o->sube += (o->sube_o - o->sube) * 0.06f; o->baja += (o->baja_o - o->baja) * 0.06f;
        S.burb_peso[q] = bu / (o->r * o->r) * 4000; S.burb_tot += S.burb_peso[q];
    }
    /* la espuma que chispea cerca de la orilla */
    S.burb_acc += clampf(S.burb_tot, 0, 400) * dtc;
    while (S.burb_acc >= 1) {
        S.burb_acc -= 1;
        float x = uni(&S.rng, 0, S.burb_tot), ac = 0; int q = 0; for (; q < NANG - 1; q++) { ac += S.burb_peso[q]; if (x <= ac) break; }
        for (int k = 0; k < NBURB; k++) if (!S.b[k].activa) {
            burbuja *u = &S.b[k]; float f0 = 1200 * powf(5, uni(&S.rng, 0, 1)), dur = uni(&S.rng, 0.004f, 0.02f);
            u->activa = 1; u->fase = 0; u->w = 2 * PI * f0 / FS; u->dw = 1 + 0.2f / (dur * FS); u->wmax = 2.8f;
            u->amp = uni(&S.rng, 0.0015f, 0.006f); u->dec = expf(-1 / (dur * FS)); u->gl = S.o[q].gl; u->gr = S.o[q].gr; break;
        }
    }
    /* el viento: una media que cambia en minutos y ráfagas suaves de decenas de segundos */
    S.Umed += (S.Ubase - S.Umed) * dtc / 900 + 0.02f * sqrtf(dtc) * gauss(&S.rng);
    S.Umed = clampf(S.Umed, 1.5f, 10);
    S.g += -S.g * dtc / 22 + 0.14f * sqrtf(2 * dtc / 22) * gauss(&S.rng);
    S.U = fmaxf(S.Umed * (1 + clampf(S.g, -0.5f, 0.6f)), 0);
    S.rus_o = 0.75f + 0.25f * clampf(gauss(&S.rng), -2, 2) * 0.5f; S.rus += (S.rus_o - S.rus) * dtc / 1.8f;
    svf_fijar(&S.fv1, 700 + 60 * S.U, 0.55f); svf_fijar(&S.fv2, 160 + 12 * S.U, 0.7f);
    /* la lluvia: chubascos que llegan y se van en minutos, con su intensidad respirando */
    if (S.lluvia_modo == 0 && tn >= S.prox_chubasco) {
        S.lloviendo = !S.lloviendo; if (S.lloviendo) S.Rmed = uni(&S.rng, 1.2f, 7);
        S.prox_chubasco = tn + (S.lloviendo ? uni(&S.rng, 300, 1500) : uni(&S.rng, 600, 2400));
    }
    int llueve = S.lluvia_modo == 2 || (S.lluvia_modo == 0 && S.lloviendo);
    if (S.lluvia_modo == 2 && S.Rmed < 2.5f) S.Rmed = 3.5f;
    S.Rou += -S.Rou * dtc / 90 + 0.5f * sqrtf(2 * dtc / 90) * gauss(&S.rng);
    S.Robj = llueve ? S.Rmed * expf(0.6f * S.Rou) : 0;
    S.R += (S.Robj - S.R) * dtc / 25;
    float tf = S.Robj > S.Rflujo ? 40 : 160; S.Rflujo += (S.R - S.Rflujo) * dtc / tf;   /* la canaleta y el alero se vacían lento */
    /* el vidrio y la tele */
    S.vidrio_obj = S.ventana ? 9000 : 1900; S.vidrio_fc += (S.vidrio_obj - S.vidrio_fc) * dtc * 2;
    svf_fijar(&S.vl1, S.vidrio_fc, 0.55f); svf_fijar(&S.vl2, S.vidrio_fc * 1.1f, 0.75f); svf_fijar(&S.vr1, S.vidrio_fc * 0.97f, 0.55f); svf_fijar(&S.vr2, S.vidrio_fc * 1.07f, 0.75f);
    S.tv_on += ((float)S.tv_obj - S.tv_on) * dtc / 1.5f;
}

/* una gota de lluvia: su diámetro sale de la distribución de Marshall y Palmer para la intensidad actual */
static float gota_mm(float R) {
    float lam = 4.1f * powf(fmaxf(R, 0.2f), -0.21f), D = -logf(uni(&S.rng, 1e-4f, 1)) / lam;
    return clampf(D + 0.5f, 0.5f, 5.5f);
}
static float golpe_gota(float D) {   /* lo que entrega al techo: masa por velocidad terminal (Atlas y otros) */
    float v = 9.65f - 10.3f * expf(-0.6f * D); return D * D * D * v * (1.0f / 150);
}

static void sint_generar(float *out, int frames) {
    for (int i = 0; i < frames; i++) {
        if ((S.muestra % K) == 0) controles();
        float OL = 0, OR = 0, w, p, b, lp, bp, hp;
        /* la rompiente */
        for (int k = 0; k < NBIN; k++) {
            fuente_mar *f = &S.f[k]; if (f->roar < 2e-6f && f->golpe < 2e-6f) continue;
            ruido_paso(&f->rn, &w, &p, &b);
            svf_paso(&f->f1, 0.9f * p + 0.1f * b, &lp, &bp, &hp); float s = lp * f->roar;
            float cr = fabsf(w) > 0.955f ? w : 0; svf_paso(&f->fcr, cr, &lp, &bp, &hp); s += bp * f->crack * 1.4f;
            if (f->golpe > 2e-6f) { svf_paso(&f->fg, b, &lp, &bp, &hp); s += lp * f->golpe; }
            OL += s * f->gl; OR += s * f->gr;
        }
        float mar = 0;
        /* la orilla: el agua que sube y la que vuelve arrastrando arena */
        for (int q = 0; q < NANG; q++) {
            fuente_orilla *o = &S.o[q]; if (o->sube < 2e-6f && o->baja < 2e-6f) continue;
            ruido_paso(&o->rn, &w, &p, &b);
            svf_paso(&o->fs, w, &lp, &bp, &hp); float s = bp * o->sube;
            float ar = fabsf(w) > 0.9f ? w : 0; svf_paso(&o->fb, ar + 0.15f * w, &lp, &bp, &hp); s += hp * o->baja;
            OL += s * o->gl; OR += s * o->gr;
        }
        for (int k = 0; k < NBURB; k++) {
            burbuja *u = &S.b[k]; if (!u->activa) continue;
            float s = sin_rapido(u->fase - PI) * u->amp; OL += s * u->gl; OR += s * u->gr;
            u->fase += u->w; if (u->fase > 2 * PI) u->fase -= 2 * PI; u->w *= u->dw; if (u->w > u->wmax) u->w = u->wmax;
            u->amp *= u->dec; if (u->amp < 1e-6f) u->activa = 0;
        }
        /* el retumbo que atraviesa las paredes */
        ruido_paso(&S.rsub, &w, &p, &b); svf_paso(&S.fsub, b, &lp, &bp, &hp); OL += lp * S.sub; OR += lp * S.sub;
        OL *= g_solo_mar; OR *= g_solo_mar; mar = OL + OR; S.mar_rms_acc += mar * mar;
        /* el viento afuera: el pasto de la duna y el soplo grave alrededor de la casa */
        {
            float u8n = S.U / 8, nv = u8n * u8n;
            ruido_paso(&S.rv1, &w, &p, &b); svf_paso(&S.fv1, p, &lp, &bp, &hp); float pasto = bp * 0.022f * nv * S.rus;
            ruido_paso(&S.rv2, &w, &p, &b); svf_paso(&S.fv2, b, &lp, &bp, &hp); float soplo = lp * 0.030f * nv * u8n;
            pasto *= g_solo_viento; soplo *= g_solo_viento;
            OL += pasto * 1.05f + soplo; OR += pasto * 0.95f + soplo * 0.97f;
        }
        /* la lluvia afuera: sobre la arena, el pasto y el mar */
        if (S.R > 0.02f) {
            ruido_paso(&S.rll, &w, &p, &b);
            float gate = fabsf(w) > 0.93f - 0.04f * clampf(S.R / 8, 0, 1) ? w : 0;
            svf_paso(&S.fll, gate, &lp, &bp, &hp); float s = bp * 0.05f * sqrtf(S.R);
            svf_paso(&S.fll2, p, &lp, &bp, &hp); s += bp * 0.006f * sqrtf(S.R);
            OL += s * g_solo_lluvia; OR += s * 0.96f * g_solo_lluvia;
        }
        /* el alero que gotea a un charco bajo la ventana, y la canaleta */
        if (S.Rflujo > 0.03f) {
            float dts = 1.0f / FS;
            for (int g = 0; g < 5; g++) {
                gotera *q = &S.got[g]; q->t -= dts;
                if (q->t > 0) continue;
                q->t = clampf(q->base / powf(S.Rflujo, 0.7f), 0.22f, 9) * uni(&S.rng, 0.85f, 1.15f);
                for (int k = 0; k < 10; k++) if (!S.pl[k].activa) {
                    burbuja *u = &S.pl[k]; int plink = uni(&S.rng, 0, 1) < 0.4f;   /* no toda gota atrapa una burbuja que canta */
                    float f0 = plink ? uni(&S.rng, 1100, 2300) : uni(&S.rng, 380, 650), dur = plink ? uni(&S.rng, 0.03f, 0.06f) : 0.018f;
                    u->activa = 1; u->fase = 0; u->w = 2 * PI * f0 / FS; u->dw = plink ? 1 + 0.45f / (dur * FS) : 1; u->wmax = u->w * 1.5f;   /* la burbuja sube de tono al acercarse a la superficie */
                    u->amp = uni(&S.rng, 0.010f, 0.022f) * (plink ? 1 : 0.7f); u->dec = expf(-1 / (dur * FS)); u->gl = q->gl; u->gr = q->gr;
                    S.sp_amp = 0.012f; S.sp_gl = q->gl; S.sp_gr = q->gr; break;
                }
            }
            S.canal_acc += (8 + 90 * clampf(S.Rflujo / 5, 0, 1)) * dts;   /* la canaleta: agua corriendo con burbujas chicas */
            if (S.canal_acc >= 1) {
                S.canal_acc -= 1;
                for (int k = 0; k < NBURB; k++) if (!S.b[k].activa) {
                    burbuja *u = &S.b[k]; float f0 = uni(&S.rng, 450, 1500), dur = uni(&S.rng, 0.01f, 0.035f);
                    u->activa = 1; u->fase = 0; u->w = 2 * PI * f0 / FS; u->dw = 1 + 0.15f / (dur * FS); u->wmax = u->w * 1.3f;
                    u->amp = uni(&S.rng, 0.002f, 0.006f) * clampf(S.Rflujo / 3, 0.3f, 1); u->dec = expf(-1 / (dur * FS)); pan(0.85f, &u->gl, &u->gr); break;
                }
            }
        }
        for (int k = 0; k < 10; k++) {
            burbuja *u = &S.pl[k]; if (!u->activa) continue;
            float s = sin_rapido(u->fase - PI) * u->amp; OL += s * u->gl; OR += s * u->gr;
            u->fase += u->w; if (u->fase > 2 * PI) u->fase -= 2 * PI; u->w *= u->dw; if (u->w > u->wmax) u->w = u->wmax;
            u->amp *= u->dec; if (u->amp < 1e-6f) u->activa = 0;
        }
        if (S.sp_amp > 1e-5f) { ruido_paso(&S.rsp, &w, &p, &b); svf_paso(&S.fsp, w, &lp, &bp, &hp); float s = bp * S.sp_amp; OL += s * S.sp_gl; OR += s * S.sp_gr; S.sp_amp *= 0.9975f; }
        /* reverberación de afuera */
        float ent = S.pre[S.pre_i]; S.pre[S.pre_i] = (OL + OR) * 0.5f; if (++S.pre_i >= 1440) S.pre_i = 0;
        float y[FDN];
        for (int j = 0; j < FDN; j++) { float v = S.fdn[j][S.fdn_i[j]]; S.fdn_lp[j] += (v - S.fdn_lp[j]) * 0.5f; y[j] = S.fdn_lp[j]; }
        for (int h = 1; h < FDN; h <<= 1) for (int j = 0; j < FDN; j += h << 1) for (int q = j; q < j + h; q++) { float a = y[q], c = y[q + h]; y[q] = a + c; y[q + h] = a - c; }
        for (int j = 0; j < FDN; j++) { S.fdn[j][S.fdn_i[j]] = y[j] * 0.35355339f * S.fdn_g[j] + ent; if (++S.fdn_i[j] >= FDN_LEN[j]) S.fdn_i[j] = 0; }
        OL += 0.14f * (S.fdn_lp[0] + S.fdn_lp[2] + S.fdn_lp[4] + S.fdn_lp[6]) * 0.25f; OR += 0.14f * (S.fdn_lp[1] + S.fdn_lp[3] + S.fdn_lp[5] + S.fdn_lp[7]) * 0.25f;
        /* el vidrio: deja pasar el grave y apaga lo agudo; algo se cuela por las rendijas */
        float fuga = S.ventana ? 0.35f : 0.06f, at = S.ventana ? 1.0f : 0.75f, IL, IR;
        svf_paso(&S.vl1, OL, &lp, &bp, &hp); svf_paso(&S.vl2, lp, &lp, &bp, &hp); IL = (lp + fuga * (OL - lp)) * at;
        svf_paso(&S.vr1, OR, &lp, &bp, &hp); svf_paso(&S.vr2, lp, &lp, &bp, &hp); IR = (lp + fuga * (OR - lp)) * at;
        /* adentro: la lluvia en el techo de zinc, oída a través del forro de madera */
        if (S.R > 0.02f) {
            float dts = 1.0f / FS, R = S.R;
            S.techo_acc += 260 * powf(R, 0.85f) * dts;   /* gotas que se alcanzan a oír una por una */
            while (S.techo_acc >= 1) {
                S.techo_acc -= 1; float D = gota_mm(R), A = golpe_gota(D); int z = xs(&S.rng) & 3;
                S.tin[z] += A * uni(&S.rng, 0.4f, 1.0f);
                for (int k = 0; k < 64; k++) if (S.ch[k].amp < 2e-4f) {   /* la gota grande suena más larga y más oscura */
                    chasquido *c = &S.ch[k]; c->amp = A * 0.05f * uni(&S.rng, 0.5f, 1.0f); c->dec = expf(-1.0f / ((0.0012f + 0.0011f * D) * FS));
                    c->k = clampf(0.95f - 0.15f * D, 0.2f, 0.9f); c->lp = 0; pan(uni(&S.rng, -0.8f, 0.8f), &c->gl, &c->gr); break;
                }
            }
            S.fondo_acc += 2500 * powf(R, 0.9f) * dts;   /* la llovizna fina que no se distingue gota a gota */
            while (S.fondo_acc >= 1) { S.fondo_acc -= 1; S.fondo_x += uni(&S.rng, -0.02f, 0.02f); }
            float tL = 0, tR = 0;
            for (int k = 0; k < 64; k++) {
                chasquido *c = &S.ch[k]; if (c->amp < 2e-4f) continue;
                float n = (int)xs(&S.rch) * 4.656613e-10f; c->lp += (n - c->lp) * c->k; float s = c->lp * c->amp; c->amp *= c->dec;
                tL += s * c->gl; tR += s * c->gr;
            }
            { float lp2, bp2, hp2; svf_paso(&S.ffondo, S.fondo_x, &lp2, &bp2, &hp2); S.fondo_x = 0; tL += bp2 * 0.5f; tR += bp2 * 0.5f; }
            for (int z = 0; z < 4; z++) {
                float x = S.tin[z], s = 0; S.tin[z] = 0;
                for (int m = 0; m < 7; m++) s += modo_paso(&S.techo[z][m], x);
                tL += s * S.tgl[z]; tR += s * S.tgr[z];
            }
            S.tic_lp += (S.tic - S.tic_lp) * 0.35f; float tk = S.tic - S.tic_lp; S.tic = 0;   /* el "tic" seco de cada gota */
            tL += tk * 0.05f; tR += tk * 0.05f;
            svf_paso(&S.ftl, tL, &lp, &bp, &hp); svf_paso(&S.fth, lp, &lp, &bp, &hp); IL += hp * 1.1f * g_solo_lluvia;
            svf_paso(&S.ftr, tR, &lp, &bp, &hp); svf_paso(&S.fthr, lp, &lp, &bp, &hp); IR += hp * 1.1f * g_solo_lluvia;
            /* las gotas que el viento tira contra el vidrio */
            float hacia = clampf(0.5f - 0.5f * M.vz / (S.U + 0.5f), 0.15f, 1);   /* cuánto sopla desde el mar hacia el vidrio */
            S.vidrio_acc += (1.5f + 22 * powf(R, 0.7f)) * hacia * (0.4f + S.U / 10) * dts;
            if (S.vidrio_acc >= 1) { S.vidrio_acc -= 1; S.vin += golpe_gota(gota_mm(R)) * 1.5f; pan(uni(&S.rng, -0.9f, 0.9f), &S.vgl, &S.vgr); }
            float vs = 0; for (int m = 0; m < 5; m++) vs += modo_paso(&S.vidrio[m], S.vin); S.vin = 0;
            IL += vs * S.vgl * g_solo_lluvia; IR += vs * S.vgr * g_solo_lluvia;
        }
        if (S.tv_on > 0.001f) {
            float tvs = tv_muestra();
            svf_paso(&S.tv_hp, tvs, &lp, &bp, &hp); svf_paso(&S.tv_lp, hp, &lp, &bp, &hp);
            float s = lp * 0.30f * S.tv_on; IL += s * 0.8f; IR += s * 0.55f; S.tv_rms_acc += s * s;
        }
        /* la pieza: primeras reflexiones en madera y una cola corta */
        S.er[0][S.er_i] = IL; S.er[1][S.er_i] = IR;
        static const int tap[5] = { 293, 467, 641, 859, 1123 }; static const float gt[5] = { 0.26f, 0.21f, 0.17f, 0.13f, 0.10f };
        float eL = 0, eR = 0;
        for (int q = 0; q < 5; q++) { int idx = S.er_i - tap[q]; if (idx < 0) idx += 2400; if (q & 1) { eL += S.er[1][idx] * gt[q]; eR += S.er[0][idx] * gt[q]; } else { eL += S.er[0][idx] * gt[q]; eR += S.er[1][idx] * gt[q]; } }
        if (++S.er_i >= 2400) S.er_i = 0;
        float cy[4];
        for (int j = 0; j < 4; j++) { float v = S.cto[j][S.cto_i[j]]; S.cto_lp[j] += (v - S.cto_lp[j]) * 0.55f; cy[j] = S.cto_lp[j]; }
        { float a = cy[0] + cy[1], c = cy[0] - cy[1], d = cy[2] + cy[3], e = cy[2] - cy[3]; cy[0] = a + d; cy[2] = a - d; cy[1] = c + e; cy[3] = c - e; }
        float cin = (IL + IR) * 0.5f;
        for (int j = 0; j < 4; j++) { S.cto[j][S.cto_i[j]] = cy[j] * 0.5f * S.cto_g[j] + cin; if (++S.cto_i[j] >= CUARTO_LEN[j]) S.cto_i[j] = 0; }
        float wl = (S.cto_lp[0] + S.cto_lp[2]) * 0.5f, wr = (S.cto_lp[1] + S.cto_lp[3]) * 0.5f;
        S.fundido = clampf(S.fundido + S.fundido_paso, 0, 1);
        S.vol += (S.vol_obj - S.vol) * 0.0005f;
        float o2[2] = { (IL + eL * 0.5f + wl * 0.10f) * 4.0f * S.vol * S.fundido, (IR + eR * 0.5f + wr * 0.10f) * 4.0f * S.vol * S.fundido };
        svf_paso(&S.dcl, o2[0], &lp, &bp, &hp); o2[0] = hp; svf_paso(&S.dcr, o2[1], &lp, &bp, &hp); o2[1] = hp;
        for (int c = 0; c < 2; c++) {
            float yv = o2[c], a = fabsf(yv);
            if (a > 0.8f) yv = (yv > 0 ? 1 : -1) * (0.8f + 0.2f * tanhf((a - 0.8f) / 0.2f));
            out[2 * i + c] = yv; S.rms_acc += yv * yv;
        }
        S.rms_n++; S.muestra++;
    }
    if (S.rms_n >= 2400) { S.rms = sqrtf(S.rms_acc / (2 * S.rms_n)); S.tv_rms = sqrtf(S.tv_rms_acc / S.rms_n); S.mar_rms = sqrtf(S.mar_rms_acc / S.rms_n); S.rms_acc = S.tv_rms_acc = S.mar_rms_acc = 0; S.rms_n = 0; }
}
