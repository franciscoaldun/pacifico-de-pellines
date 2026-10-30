/* La luna y el sol de verdad sobre la cabaña de Pellines (35,49 °S, 72,51 °O), con el método de Paul Schlyter: la
   posición sale con un par de minutos de arco de error, suficiente para poner la luna en su lugar, con su fase, su brillo y su
   color. La ventana mira al oeste-noroeste (al mar): lo que no cae en ese pedazo de cielo no se ve, pero su luz sí llega. */
#pragma once
#define LAT_P (-35.49)
#define LON_P (-72.51)
#define AZ_VISTA 311.0   /* hacia dónde mira la ventana: la pared noroeste de la cabaña (medida en el techo, en la imagen satelital) */
#define GRAD (3.14159265358979323846 / 180)

static struct {
    int ok; float luna[3], sol[3];   /* direcciones en la vista: x a la derecha, y arriba, z hacia el mar */
    float alt_luna, alt_sol, fase;   /* alturas en grados; fracción iluminada de la luna */
    float luz, color[3], disco[3];   /* cuánta luz de luna llega (1 = luna llena alta y despejada); su color; el brillo del disco */
    float brillo_fase;               /* solo lo que da la fase, sin el aire ni la altura */
    float gal[9];                    /* de la vista a coordenadas galácticas: para las estrellas y la Vía Láctea en su lugar */
} ASTRO;

static double rev360(double x) { return x - 360.0 * floor(x / 360.0); }
static void a_horizonte(double ra, double dec, double lst, double *alt, double *az) {   /* ecuatoriales a altura y acimut */
    double ha = lst - ra, x = cos(ha * GRAD) * cos(dec * GRAD), y = sin(ha * GRAD) * cos(dec * GRAD), z = sin(dec * GRAD);
    double xh = x * sin(LAT_P * GRAD) - z * cos(LAT_P * GRAD), zh = x * cos(LAT_P * GRAD) + z * sin(LAT_P * GRAD);
    *az = rev360(atan2(y, xh) / GRAD + 180); *alt = asin(zh) / GRAD;
}
static void a_vista(double alt, double az, float *v) {   /* altura y acimut a la dirección en la vista de la ventana */
    double da = (az - AZ_VISTA) * GRAD, a = alt * GRAD;
    v[0] = (float)(cos(a) * sin(da)); v[1] = (float)sin(a); v[2] = (float)(cos(a) * cos(da));
}
static double refraccion(double alt) { return alt > -1.5 ? 1.02 / tan((alt + 10.3 / (alt + 5.11)) * GRAD) / 60 : 0; }   /* Bennett */

static void astro_calcular(double unix_s) {
    double d = unix_s / 86400.0 + 2440587.5 - 2451543.5, ecl = 23.4393 - 3.563e-7 * d;
    /* el sol */
    double ws = 282.9404 + 4.70935e-5 * d, es = 0.016709 - 1.151e-9 * d, Ms = rev360(356.0470 + 0.9856002585 * d);
    double Es = Ms + es / GRAD * sin(Ms * GRAD) * (1 + es * cos(Ms * GRAD));
    double xv = cos(Es * GRAD) - es, yv = sqrt(1 - es * es) * sin(Es * GRAD), lons = rev360(atan2(yv, xv) / GRAD + ws);
    /* la luna, con sus perturbaciones principales */
    double N = rev360(125.1228 - 0.0529538083 * d), inc = 5.1454, w = rev360(318.0634 + 0.1643573223 * d), a = 60.2666, e = 0.0549, M = rev360(115.3654 + 13.0649929509 * d);
    double E = M + e / GRAD * sin(M * GRAD) * (1 + e * cos(M * GRAD));
    for (int k = 0; k < 6; k++) E -= (E - e / GRAD * sin(E * GRAD) - M) / (1 - e * cos(E * GRAD));
    xv = a * (cos(E * GRAD) - e); yv = a * sqrt(1 - e * e) * sin(E * GRAD);
    double v = atan2(yv, xv) / GRAD, r = sqrt(xv * xv + yv * yv), vw = (v + w) * GRAD;
    double xh = r * (cos(N * GRAD) * cos(vw) - sin(N * GRAD) * sin(vw) * cos(inc * GRAD)), yh = r * (sin(N * GRAD) * cos(vw) + cos(N * GRAD) * sin(vw) * cos(inc * GRAD)), zh = r * sin(vw) * sin(inc * GRAD);
    double lonm = atan2(yh, xh) / GRAD, latm = atan2(zh, sqrt(xh * xh + yh * yh)) / GRAD;
    double Ls = rev360(Ms + ws), Lm = rev360(M + w + N), D = rev360(Lm - Ls), F = rev360(Lm - N);
    #define S(x) sin((x) * GRAD)
    lonm += -1.274 * S(M - 2 * D) + 0.658 * S(2 * D) - 0.186 * S(Ms) - 0.059 * S(2 * M - 2 * D) - 0.057 * S(M - 2 * D + Ms) + 0.053 * S(M + 2 * D)
            + 0.046 * S(2 * D - Ms) + 0.041 * S(M - Ms) - 0.035 * S(D) - 0.031 * S(M + Ms) - 0.015 * S(2 * F - 2 * D) + 0.011 * S(M - 4 * D);
    latm += -0.173 * S(F - 2 * D) - 0.055 * S(M - F - 2 * D) - 0.046 * S(M + F - 2 * D) + 0.033 * S(F + 2 * D) + 0.017 * S(2 * M + F);
    #undef S
    r += -0.58 * cos((M - 2 * D) * GRAD) - 0.46 * cos(2 * D * GRAD);
    /* a ecuatoriales, y de ahí al cielo de Pellines con el tiempo sidéreo local */
    double lst = rev360(Ls + 180 + rev360(unix_s / 240.0) + LON_P);   /* grados: 15 por hora de tiempo universal */
    double ras, decs, ram, decm;
    { double xe = cos(lons * GRAD), ye = sin(lons * GRAD) * cos(ecl * GRAD), ze = sin(lons * GRAD) * sin(ecl * GRAD); ras = atan2(ye, xe) / GRAD; decs = atan2(ze, sqrt(xe * xe + ye * ye)) / GRAD; }
    { double cl = cos(latm * GRAD), xe = cos(lonm * GRAD) * cl, ye = sin(lonm * GRAD) * cl * cos(ecl * GRAD) - sin(latm * GRAD) * sin(ecl * GRAD), ze = sin(lonm * GRAD) * cl * sin(ecl * GRAD) + sin(latm * GRAD) * cos(ecl * GRAD);
      ram = atan2(ye, xe) / GRAD; decm = atan2(ze, sqrt(xe * xe + ye * ye)) / GRAD; }
    double alts, azs, altm, azm; a_horizonte(ras, decs, lst, &alts, &azs); a_horizonte(ram, decm, lst, &altm, &azm);
    altm -= asin(1 / r) / GRAD * cos(altm * GRAD);   /* la paralaje: la luna está cerca */
    altm += refraccion(altm); alts += refraccion(alts);
    a_vista(alts, azs, ASTRO.sol); a_vista(altm, azm, ASTRO.luna);
    ASTRO.alt_luna = (float)altm; ASTRO.alt_sol = (float)alts;
    /* la fase: el ángulo sol-luna visto desde aquí */
    float ce = ASTRO.sol[0] * ASTRO.luna[0] + ASTRO.sol[1] * ASTRO.luna[1] + ASTRO.sol[2] * ASTRO.luna[2], fa = 180 - acosf(fmaxf(-1, fminf(1, ce))) / (float)GRAD;
    ASTRO.fase = (1 + cosf(fa * (float)GRAD)) / 2;
    /* cuánta luz da: la luna llena es la referencia; la de cuarto da un 9 % (Allen); y el aire se la come cerca del horizonte, más el azul */
    float brillo = powf(10, -0.4f * (0.026f * fa + 4e-9f * fa * fa * fa * fa));
    float h = (float)altm, sh = sinf(fmaxf(h, 0) * (float)GRAD), masa = 1 / (sh + 0.025f * expf(-11 * sh)), arriba = clampf((h + 0.8f) / 1.6f, 0, 1);
    static const float k[3] = { 0.12f, 0.20f, 0.33f }, base[3] = { 1.0f, 0.96f, 0.86f };
    float tm = 0;
    for (int c = 0; c < 3; c++) { float t = powf(10, -0.4f * k[c] * fminf(masa, 40)); ASTRO.disco[c] = base[c] * t * arriba; tm += t / 3; }
    for (int c = 0; c < 3; c++) ASTRO.color[c] = ASTRO.disco[c] / fmaxf(tm * arriba, 1e-4f);   /* el tono de la luz, sin la intensidad */
    ASTRO.luz = brillo * tm * arriba; ASTRO.brillo_fase = brillo;
    /* cada eje de la vista llevado al cielo de esta hora y de ahí a coordenadas galácticas (matriz J2000 a galácticas) */
    static const double EG[9] = { -0.0548755604, -0.8734370902, -0.4838350155, 0.4941094279, -0.4448296300, 0.7469822445, -0.8676661490, -0.1980763734, 0.4559837762 };
    for (int e = 0; e < 3; e++) {
        double v[3] = { e == 0, e == 1, e == 2 }, alt = asin(v[1]), az = (AZ_VISTA * GRAD) + atan2(v[0], v[2]), fi = LAT_P * GRAD;
        double dec = asin(sin(fi) * sin(alt) + cos(fi) * cos(alt) * cos(az));
        double ha = atan2(-sin(az) * cos(alt), cos(fi) * sin(alt) - sin(fi) * cos(alt) * cos(az)), ra = lst * GRAD - ha;
        double q[3] = { cos(dec) * cos(ra), cos(dec) * sin(ra), sin(dec) };
        for (int f = 0; f < 3; f++) ASTRO.gal[f * 3 + e] = (float)(EG[f * 3] * q[0] + EG[f * 3 + 1] * q[1] + EG[f * 3 + 2] * q[2]);
    }
    ASTRO.ok = 1;
}
