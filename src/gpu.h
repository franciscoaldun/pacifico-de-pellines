/* La tarjeta gráfica pone el cielo y muestra el cuadro.
   El procesador dibuja el mar y la arena en luz lineal y los deja en una textura de media precisión, marcando qué píxeles
   son cielo abierto. La gráfica hace ese cielo con nubes de verdad: una capa de estratocúmulos entre 850 y 1750 m, que
   recorre con 56 pasos por píxel, iluminada por la luna con cinco pasos más hacia ella (la luz que las atraviesa, la que
   rebota hacia adelante y les deja el borde plateado cerca de la luna), detrás las estrellas y el disco de la luna. Le dice
   al procesador cuánta luna pasa entre las nubes, para que el reflejo en el mar sea el mismo. Después le pone el resplandor
   de la luna y de la espuma (un bloom de seis niveles), la lleva a la resolución nativa con un filtro bicúbico, la
   convierte a lo que ve el ojo, le suma los textos y la entrega sincronizada con la pantalla. Sin Direct3D 11 la app sigue
   con GDI y el cielo del procesador, como antes. */
#pragma once
#include <d3d11.h>
#include <d3dcompiler.h>
#include <immintrin.h>

#define NBLOOM 6
static struct {
    ID3D11Device *dev; ID3D11DeviceContext *ctx; IDXGISwapChain *sc;
    ID3D11Texture2D *escena, *ovl, *bb, *fuera, *copia; ID3D11ShaderResourceView *escena_v, *ovl_v; ID3D11RenderTargetView *bb_v;
    ID3D11Texture2D *nivel[NBLOOM]; ID3D11ShaderResourceView *nivel_v[NBLOOM]; ID3D11RenderTargetView *nivel_r[NBLOOM]; int nw[NBLOOM], nh[NBLOOM];
    ID3D11VertexShader *vs; ID3D11PixelShader *ps_baja, *ps_sube, *ps_final, *ps_cielo, *ps_luna, *ps_nubes, *ps_marco, *ps_borra; ID3D11SamplerState *muestreo, *repite; ID3D11BlendState *suma; ID3D11Buffer *cb, *cbk, *cbm;
    ID3D11Texture2D *acum[2]; ID3D11ShaderResourceView *acum_v[2]; ID3D11RenderTargetView *acum_r[2];
    ID3D11Texture2D *marco[2]; ID3D11ShaderResourceView *marco_v[2]; ID3D11RenderTargetView *marco_r[2]; int mw, mh;
    ID3D11PixelShader *ps_tele; ID3D11Texture2D *tele, *atlas; ID3D11ShaderResourceView *tele_v, *atlas_v; ID3D11RenderTargetView *tele_r; ID3D11Buffer *cbt;
    ID3D11Texture3D *ruido; ID3D11ShaderResourceView *ruido_v;
    ID3D11Texture2D *completa, *luna, *luna_c[3]; ID3D11ShaderResourceView *completa_v; ID3D11RenderTargetView *completa_r, *luna_r;
    int w, h, ow, oh; u64 cuadro; int ovl_sucio; HWND hwnd; float pasos;
} G = { .pasos = 48 };
static int g_gpu;                       /* 1 si la gráfica está mostrando los cuadros */
static float g_tele_t, g_tele_on = 1;   /* segundos desde que se encendió la tele, y cuánto está encendida */
static u32 *OVL;                        /* textos y paneles encima, con transparencia (R, G, B, A premultiplicados) */
static volatile LONG g_gpu_tam;         /* la ventana cambió de tamaño */

static const char g_hlsl[] =
"cbuffer C : register(b0) { float2 texel; float2 tam; float bloom; float brillo; float cuadro; float uno; };\n"
"Texture2D t0 : register(t0); Texture2D t1 : register(t1); Texture2D t2 : register(t2); SamplerState s0 : register(s0);\n"
"struct V { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n"
"V vs(uint id : SV_VertexID) { V o; float2 uv = float2((id << 1) & 2, id & 2); o.uv = uv; o.p = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1); return o; }\n"
"float3 m(float2 uv) { return t0.SampleLevel(s0, uv, 0).rgb; }\n"
/* bajar a la mitad con 13 muestras (Jimenez 2014): sin parpadeo en la espuma que brilla */
"float4 ps_baja(V i) : SV_Target { float2 d = texel, u = i.uv;\n"
"  float3 a = m(u + d * float2(-2, -2)), b = m(u + d * float2(0, -2)), c = m(u + d * float2(2, -2));\n"
"  float3 e = m(u + d * float2(-1, -1)), f = m(u + d * float2(1, -1)), g = m(u + d * float2(-2, 0)), h = m(u), k = m(u + d * float2(2, 0));\n"
"  float3 l = m(u + d * float2(-1, 1)), n = m(u + d * float2(1, 1)), o = m(u + d * float2(-2, 2)), p = m(u + d * float2(0, 2)), q = m(u + d * float2(2, 2));\n"
"  float3 r = (e + f + l + n) * 0.125 + (a + b + g + h) * 0.03125 + (b + c + h + k) * 0.03125 + (g + h + o + p) * 0.03125 + (h + k + p + q) * 0.03125;\n"
"  return float4(r, 1); }\n"
/* subir con un filtro tienda de 9 muestras y sumar al nivel de arriba */
"float4 ps_sube(V i) : SV_Target { float2 d = texel, u = i.uv;\n"
"  float3 r = m(u + d * float2(-1, -1)) + 2 * m(u + d * float2(0, -1)) + m(u + d * float2(1, -1)) + 2 * m(u + d * float2(-1, 0)) + 4 * m(u)\n"
"           + 2 * m(u + d * float2(1, 0)) + m(u + d * float2(-1, 1)) + 2 * m(u + d * float2(0, 1)) + m(u + d * float2(1, 1));\n"
"  return float4(r * (uno / 16), 1); }\n"
/* bicúbica de Catmull-Rom con 9 muestras bilineales */
"float3 bicubica(float2 uv) { float2 ts = 1 / texel, sp = uv * ts, t1p = floor(sp - 0.5) + 0.5, f = sp - t1p;\n"
"  float2 w0 = f * (-0.5 + f * (1 - 0.5 * f)), w1 = 1 + f * f * (-2.5 + 1.5 * f), w2 = f * (0.5 + f * (2 - 1.5 * f)), w3 = f * f * (-0.5 + 0.5 * f);\n"
"  float2 w12 = w1 + w2, p0 = (t1p - 1) * texel, p3 = (t1p + 2) * texel, p12 = (t1p + w2 / w12) * texel;\n"
"  return m(float2(p0.x, p0.y)) * w0.x * w0.y + m(float2(p12.x, p0.y)) * w12.x * w0.y + m(float2(p3.x, p0.y)) * w3.x * w0.y\n"
"       + m(float2(p0.x, p12.y)) * w0.x * w12.y + m(float2(p12.x, p12.y)) * w12.x * w12.y + m(float2(p3.x, p12.y)) * w3.x * w12.y\n"
"       + m(float2(p0.x, p3.y)) * w0.x * w3.y + m(float2(p12.x, p3.y)) * w12.x * w3.y + m(float2(p3.x, p3.y)) * w3.x * w3.y; }\n"
/* ---- el cielo: nubes en volumen iluminadas por la luna ---- */
"cbuffer K : register(b1) { float4 cam; float4 luna; float4 nube; float4 luzd; float4 tiempo; float4 lluz; float4 disco; float4 sol; float4 g0; float4 g1; float4 g2; };\n"
/* g0..g2: de la vista a coordenadas galácticas a esta hora, desde la cabaña (para las estrellas y la Vía Láctea en su lugar) */
/* lluz: la luz de la luna que llega (color por intensidad); disco: el brillo de su superficie; sol: dónde está el sol y su altura */
"Texture3D<float> ruido : register(t3); SamplerState s1 : register(s1);\n"
"float hg(float c, float g) { float g2 = g * g; return (1 - g2) / (12.566 * pow(max(1 + g2 - 2 * g * c, 1e-4), 1.5)); }\n"
"float densidad(float3 p) {\n"   /* la capa entre 850 y 1750 m: base redonda, techo deshilachado; la cobertura viene del procesador */
"  float h = (p.y - 850) / 900; if (h <= 0 || h >= 1) return 0;\n"
"  float perfil = saturate(h * 5) * saturate((1 - h) * 2.2);\n"
"  float3 q = float3(p.x + nube.y, p.y * 1.6, p.z + nube.z) / 4200;\n"
"  float n = ruido.SampleLevel(s1, q, 0) * 0.68 + ruido.SampleLevel(s1, q * 3.1 + 0.31, 0) * 0.32;\n"
"  float c = nube.x;\n"
"  return saturate((n * perfil - (1 - c)) / max(c * 0.6, 0.05)) * 0.03; }\n"
"float3 nubes(float2 px, float3 dir, out float T) {\n"
"  T = 1; float3 L = 0;\n"
"  if (dir.y < 0.003) return L;\n"
"  float ini = 850 / dir.y, fin = min(1750 / dir.y, ini + 6000); if (ini > 60000) return L;\n"
"  int N = max((int)tiempo.w, 16); float dt = (fin - ini) / N;\n"   /* los pasos los fija la app según cuánto aguanta la gráfica */
"  float j = frac(sin(dot(px + tiempo.y * 0.37, float2(12.9898, 78.233))) * 43758.5453);\n"
"  float cl = dot(dir, luzd.xyz), ph = hg(cl, 0.55) * 0.75 + hg(cl, -0.15) * 0.25;\n"
"  [loop] for (int i = 0; i < N; i++) {\n"
"    float3 p = dir * (ini + (i + j) * dt); float d = densidad(p);\n"
"    if (d > 0) {\n"
"      float od = 0; [unroll] for (int k = 1; k <= 5; k++) od += densidad(p + luzd.xyz * (k * k * 40.0)) * (2 * k - 1) * 40.0;\n"
"      float tl = exp(-od) * (1 - 0.6 * exp(-od * 3));\n"   /* lo que llega de la luna, con el oscurecido de los bordes gruesos */
"      float3 s = lluz.rgb * 0.16 * ph * tl + float3(0.0016, 0.0020, 0.0032);\n"
"      float a = exp(-d * dt); L += T * (1 - a) * s; T *= a; if (T < 0.02) break;\n"
"    }\n"
"  }\n"
"  float bruma = 1 - exp(-ini / 26000);\n"   /* lejos, las nubes se pierden en la bruma del horizonte */
"  L = lerp(L, (1 - T) * float3(0.012, 0.015, 0.024), bruma); T = lerp(T, 1, bruma * 0.3);\n"
"  return L; }\n"
"float3 cielo_px(float2 px) {\n"
"  float3 dir = normalize(float3((px.x - cam.z * 0.5) / cam.x, (cam.y - px.y) / cam.x, 1));\n"
"  float T; float3 nub = nubes(px, dir, T);\n"
"  float f = saturate(px.y / cam.y); f *= f;\n"
"  float3 c = float3(0.0022, 0.003, 0.0068) + float3(0.009, 0.011, 0.015) * f * (0.35 + 0.65 * saturate(lluz.w * 3));\n"   /* sin luna el cielo es más negro */
/* el crepúsculo: con el sol hasta 18° bajo el horizonte, el cielo hacia él se aclara (naranjo cerca, azul más arriba). De día la
   ventana sigue mostrando la noche: sin crepúsculo */
"  if (sol.w > -18 && sol.w < 2) { float cr = saturate((sol.w + 18) / 18) * saturate((2 - sol.w) / 2);float3 sh = normalize(float3(sol.x, 0, sol.z) + 1e-5);\n"
"    float hacia = saturate(dot(normalize(float3(dir.x, 0, dir.z)), sh)), bajo = exp(-max(dir.y, 0) * 10);\n"
"    c += cr * cr * cr * bajo * (float3(0.42, 0.17, 0.05) * pow(hacia, 5) * cr + float3(0.05, 0.075, 0.13) * (0.3 + 0.7 * hacia)) * 0.3; }\n"
"  float2 dm = px - luna.xy; float d2 = dot(dm, dm), rr = luna.z;\n"
"  c += lluz.rgb * 0.028 / (1 + d2 / (0.0036 * cam.x * cam.x));\n"   /* el halo de la luna en el aire, del brillo que tiene esta noche */
"  float3 g = float3(dot(g0.xyz, dir), dot(g1.xyz, dir), dot(g2.xyz, dir));\n"
"  float oscuro = (1 - saturate(lluz.w * 2.2)) * (sol.w > -18 && sol.w < 2 ? saturate(-(sol.w + 12) / 6) : 1);\n"   /* con luna o con crepúsculo no se ven */
/* las estrellas, fijas en el cielo de verdad: giran con la noche */
"  float3 ce = floor(g * 420.0); float h1 = frac(sin(dot(ce, float3(127.1, 311.7, 74.7))) * 43758.5453);\n"
"  if (h1 < 0.007) {\n"
"    float3 o = frac(sin(float3(dot(ce, float3(269.5, 183.3, 246.1)), dot(ce, float3(113.5, 271.9, 124.6)), dot(ce, float3(12.99, 78.23, 37.72)))) * 43758.5453);\n"
"    float3 sd = normalize((ce + o) / 420.0); float dpx = length(g - sd) * cam.x, u = h1 / 0.007;\n"
"    float I = (0.35 * pow(u, 10) + 0.012) * (0.4 + 0.6 * oscuro) * (0.7 + 0.3 * sin(tiempo.x * (1.5 + 3 * u) + h1 * 900));\n"
"    c += I * exp(-dpx * dpx / 0.7) * lerp(float3(0.85, 0.9, 1.1), float3(1.1, 0.95, 0.8), frac(h1 * 5711)); }\n"
/* la Vía Láctea en su lugar: más ancha y clara hacia el centro de la galaxia (Sagitario), con nubes de estrellas y la grieta oscura del polvo */
"  { float b = asin(clamp(g.z, -1, 1)), l = atan2(g.y, g.x), cen = exp(-l * l / 0.6), an = 0.11 + 0.09 * cen, aire = saturate(dir.y * 8 + 0.15);\n"
"    float banda = exp(-b * b / (an * an)), nucleo = exp(-(l * l + b * b * 3) / 0.02);\n"
/* nubes de estrellas en cuatro tamaños (de lejos se ve granulada, nunca lisa) y la grieta del polvo: manchas oscuras sueltas que
   serpentean un poco por encima del medio de la banda */
"    float nub = ruido.SampleLevel(s1, g * 3.0 + 0.2, 0) * 0.7 + ruido.SampleLevel(s1, g * 9.0 + 0.6, 0) * 0.5;\n"
"    float fino = ruido.SampleLevel(s1, g * 31.0 + 0.1, 0) * ruido.SampleLevel(s1, g * 73.0 + 0.4, 0) * 2.2;\n"
"    float lane = exp(-pow(b - 0.035 + 0.02 * sin(l * 7) + 0.03 * (ruido.SampleLevel(s1, g * 6.0 + 0.8, 0) - 0.5), 2) / 0.0012);\n"
"    float polvo = saturate(1 - 0.8 * lane * (0.3 + 0.7 * cen) * saturate(ruido.SampleLevel(s1, g * 14.0 + 0.3, 0) * 1.6 - 0.2));\n"
"    float I = (banda * (0.2 + 0.8 * cen) * (0.2 + 0.9 * nub * nub) * (0.45 + 0.75 * fino) + 1.1 * nucleo * (0.6 + 0.5 * fino)) * polvo * oscuro * aire * g0.w;\n"
"    c += float3(0.0075, 0.0078, 0.0086) * lerp(float3(0.95, 0.97, 1.05), float3(1.06, 1.0, 0.94), cen) * I;\n"
/* y el grano: miles de estrellas débiles, amontonadas donde la banda es más densa */
"    float3 cs = floor(g * 1400.0); float hs = frac(sin(dot(cs, float3(41.3, 289.1, 113.7))) * 43758.5453);\n"
"    if (hs < 0.03 + 0.45 * (banda * (0.3 + 0.7 * cen) * nub + nucleo) * polvo) {\n"
"      float3 o2 = frac(sin(float3(dot(cs, float3(17.1, 31.7, 93.3)), dot(cs, float3(71.9, 13.3, 57.1)), dot(cs, float3(23.7, 67.1, 11.9)))) * 43758.5453);\n"
"      float dp = length(g - normalize((cs + o2) / 1400.0)) * cam.x;\n"
"      c += 0.0065 * frac(hs * 937.1) * exp(-dp * dp / 0.5) * oscuro * aire * g0.w * float3(1, 0.98, 0.95); } }\n"
/* la luna con su fase: cada punto del disco es un punto de una esfera que el sol ilumina; lo oscuro tiene la luz cenicienta de la Tierra */
"  if (d2 < rr * rr * 1.3) {\n"
"    float3 m = normalize(luzd.xyz), ex = normalize(float3(1, 0, 0) - m.x * m); float3 ey = normalize(float3(0, 1, 0) - m.y * m - dot(float3(0, 1, 0), ex) * ex);\n"
"    float2 q = float2(dm.x, -dm.y) / rr; float qq = dot(q, q);\n"
"    if (qq < 1) {\n"
"      float3 nn = q.x * ex + q.y * ey - sqrt(1 - qq) * m;\n"
"      float lit = smoothstep(-0.04, 0.08, dot(nn, normalize(sol.xyz)));\n"
"      float mares = 0.78 + 0.22 * ruido.SampleLevel(s1, float3(q * 0.21 + 0.37, 0.53), 0) + 0.08 * ruido.SampleLevel(s1, float3(q * 0.9 + 0.1, 0.17), 0);\n"
"      float borde = saturate((1 - sqrt(qq)) * rr / 1.1);\n"
"      c += borde * disco.rgb * (2.6 * lit * mares * (0.85 + 0.15 * sqrt(1 - qq)) + 0.02 * (1 - lit));\n"
"    }\n"
"  }\n"
"  return c * T + nub; }\n"
/* el cielo se promedia entre cuadros (la cámara no se mueve): el grano de los pocos pasos por píxel desaparece */
"float4 ps_nubes(V i) : SV_Target { float4 e = t0.SampleLevel(s0, i.uv, 0); float3 prev = t1.SampleLevel(s0, i.uv, 0).rgb;\n"
"  float3 nuevo = e.a < 0.5 ? 0 : cielo_px(i.uv * cam.zw); return float4(lerp(prev, nuevo, tiempo.z), 1); }\n"
"float4 ps_cielo(V i) : SV_Target { float4 e = t0.SampleLevel(s0, i.uv, 0); float3 s = t1.SampleLevel(s0, i.uv, 0).rgb;\n"
"  return float4(e.rgb + e.a * s * luna.w, 1); }\n"
"float4 ps_luna(V i) : SV_Target { float T; nubes(luna.xy, luzd.xyz, T); return float4(T, T, T, 1); }\n"
/* ---- la ventana de la cabaña: marco de pino barnizado, dos hojas con montante al centro y alféizar; la ilumina la tele
   desde atrás a la izquierda y el vidrio en los cantos; después se desenfoca, porque el ojo está mirando el mar ---- */
"cbuffer M : register(b2) { float4 vm; float4 lm; float4 tm; float4 lu; };\n"
"Texture2D t4 : register(t4);\n"
"float3 madera(float largo, float ancho, float id) {\n"
"  float n1 = ruido.SampleLevel(s1, float3(largo * 0.006, ancho * 0.09, id * 0.37 + 0.1), 0);\n"
"  float n2 = ruido.SampleLevel(s1, float3(largo * 0.03, ancho * 0.5, id * 0.53 + 0.4), 0);\n"
"  float veta = 0.5 + 0.5 * sin(ancho * 2.6 + n1 * 10 + n2 * 2.5);\n"
"  return float3(0.40, 0.235, 0.11) * (0.7 + 0.3 * veta) * (0.8 + 0.4 * n2); }\n"
"bool pieza(float2 p, float x0, float x1, float y0, float y1, float2 e, inout float d, inout float2 ed, inout float largo, inout float ancho, inout float id, float k) {\n"
"  if (p.x < x0 || p.x > x1 || p.y < y0 || p.y > y1) return false;\n"
"  d = e.x > 0 ? x1 - p.x : e.x < 0 ? p.x - x0 : e.y > 0 ? y1 - p.y : p.y - y0; ed = e; id = k;\n"
"  largo = abs(e.x) > 0 ? p.y : p.x; ancho = abs(e.x) > 0 ? p.x : p.y; return true; }\n"
"float4 ps_marco(V i) : SV_Target {\n"
"  float2 p = i.uv * float2(160, 100);\n"   /* en centímetros: la ventana se ve de 160 x 100 cm desde un metro */
"  float tv = vm.z; float3 luzv = lu.rgb;\n"
"  float d = 99, largo = 0, ancho = 0, id = 0, tipo = 0; float2 e = 0; float3 n = float3(0, 0, -1);\n"
"  if (tm.y < 0.5) return 0;\n"
"  if (p.y > 97.2) { tipo = 3; n = normalize(float3(0, -0.2, -1)); largo = p.x; ancho = p.y * 3; id = 7; }\n"
"  else if (p.y > 91.5) { tipo = 2; n = float3(0, 1, 0); largo = p.x; ancho = (p.y - 91.5) * 2.7; id = 6; d = p.y - 91.5; e = float2(0, -1); }\n"
"  else if (pieza(p, 0, 5.2, 0, 91.5, float2(1, 0), d, e, largo, ancho, id, 1) || pieza(p, 154.8, 160, 0, 91.5, float2(-1, 0), d, e, largo, ancho, id, 2)\n"
"        || pieza(p, 77, 80, 0, 91.5, float2(-1, 0), d, e, largo, ancho, id, 3) || pieza(p, 80, 83, 0, 91.5, float2(1, 0), d, e, largo, ancho, id, 4)\n"
"        || pieza(p, 0, 160, 0, 4.2, float2(0, 1), d, e, largo, ancho, id, 5) || pieza(p, 0, 160, 87.8, 91.5, float2(0, -1), d, e, largo, ancho, id, 8)) tipo = 1;\n"
"  float3 P = float3((p.x / 160 - 0.5) * 1.6, (0.5 - p.y / 100), 1.0);\n"   /* metros desde los ojos, el vidrio a un metro */
"  float3 TV = float3(-1.1, -0.3, -1.3), Lt = TV - P; float dt2 = dot(Lt, Lt); Lt *= rsqrt(dt2);\n"   /* la tele sobre una mesa, atrás a la izquierda */
"  float3 ctv = float3(0.55, 0.62, 1.0) * tv * 0.9 / dt2;\n"
/* la manilla de la ventana: una roseta en la hoja izquierda y la palanca de metal, que brilla con la tele */
"  float2 mr = (p - float2(78.5, 48)) / float2(0.9, 4.2); float2 ma = float2(p.x - 74.5, p.y - 47.6);\n"
"  bool ros = dot(mr, mr) < 1, pal = abs(ma.x) < 4.2 && abs(ma.y) < 0.75 && p.x < 78.5;\n"
"  if (ros || pal) {\n"
"    float3 nm = ros ? normalize(float3(mr.x * 0.8, -mr.y * 0.3, -1)) : normalize(float3(0, -ma.y / 0.75 * 0.9, -1));\n"
"    float3 hm = normalize(Lt + normalize(-P)); float em = pow(saturate(dot(nm, hm)), 60), dm = saturate(dot(nm, Lt));\n"
"    float3 cm = float3(0.62, 0.6, 0.56) * (ctv * (0.2 + 0.8 * dm) + luzv * 1.5 + 0.002) + ctv * em * 3 + luzv * em * 4;\n"
"    return float4(cm * vm.w, 1); }\n"
"  if (tipo == 0) {\n"   /* el vidrio: un poco empañado en las esquinas de abajo, que la tele alumbra */
"    float2 c1 = (p - float2(5, 88)) / float2(16, 10), c2 = (p - float2(155, 88)) / float2(16, 10), c3 = (p - float2(77, 88)) / float2(12, 8), c4 = (p - float2(83, 88)) / float2(12, 8);\n"
"    float m = pow(saturate(1 - min(min(length(c1), length(c2)), min(length(c3), length(c4)))), 1.6) * 0.42;\n"
"    m *= 0.55 + 0.45 * ruido.SampleLevel(s1, float3(p * 0.06, 0.7), 0) * (0.7 + 0.6 * ruido.SampleLevel(s1, float3(p * 0.9, 0.2), 0));\n"   /* el vaho no es parejo: se junta en gotitas */
"    return float4((luzv * 0.8 + ctv * 0.5) * m * vm.w, m); }\n"
"  if (tipo == 1 && d < 0.8) { float2 eu = float2(e.x, -e.y); n = normalize(float3(eu * 1.1, -1)); }\n"   /* el chaflán que mira al vidrio */
"  float3 alb = madera(largo, ancho, id);\n"
"  float dif = saturate(dot(n, Lt));\n"
"  float3 h = normalize(Lt + normalize(-P)); float esp = pow(saturate(dot(n, h)), 36) * 0.35;\n"
"  float canto = (tipo == 1 ? (d < 0.8 ? 1.0 : exp(-(d - 0.8) * 0.7) * 0.35) : tipo == 2 ? exp(-d * 0.9) * 0.6 : 0.05);\n"   /* la luz que entra por el vidrio alumbra los cantos */
"  float3 c = alb * (ctv * (0.15 + 0.85 * dif) + luzv * 4.0 * canto + 0.003) + ctv * esp + luzv * canto * esp * 2;\n"
"  if (tipo == 1 && id >= 3 && id <= 4 && abs(p.x - 80) < 0.18) c *= 0.15;\n"   /* la junta entre las dos hojas */
"  if (tipo == 2 && p.y < 92.1) c *= 0.4;\n"   /* la sombra bajo el riel */
"  return float4(c * vm.w, 1); }\n"
/* desenfoque del marco: el ojo enfoca a 200 m, lo que está a un metro se ve blando */
"float4 ps_borra(V i) : SV_Target { float4 s = 0; float w = 0;\n"
"  [unroll] for (int k = -6; k <= 6; k++) { float g = exp(-k * k / 8.0); s += g * t0.SampleLevel(s0, i.uv + texel * k, 0); w += g; }\n"
"  return s / w; }\n"
/* ---- la Play en la tele: la secuencia de cuando se enciende sin disco y después el menú, como en la consola de verdad ---- */
"cbuffer TV : register(b3) { float4 tv0; };\n"   /* x: segundos desde que se encendió; y: cuánto está encendida; z: tiempo; w: aspecto de la pantalla */
"Texture2D t5 : register(t5); Texture2D t6 : register(t6);\n"   /* t5: la pantalla de la tele ya dibujada; t6: los letreros */
"float letrero(float2 uv, float2 c, float alto, float fila, float letras) {\n"   /* un letrero del atlas (filas de 24 px, letras de 10 x 20) */
"  float ancho = alto * letras * 0.5 * 0.75; float2 q = (uv - c) / float2(ancho, alto) + 0.5;\n"
"  if (q.x < 0 || q.x > 1 || q.y < 0 || q.y > 1) return 0;\n"
"  return t6.SampleLevel(s0, float2(q.x * letras * 10 / 512, (fila * 24 + q.y * 20) / 96), 0).r; }\n"
"float4 ps_tele(V i) : SV_Target {\n"
"  float2 uv = i.uv; float t = tv0.x, T = tv0.z; float3 c = 0;\n"
"  if (t < 0.45) { float a = saturate(t / 0.45); float fil = exp(-pow((uv.y - 0.5) / (0.004 + 0.5 * a * a), 2)); c = float3(0.9, 0.95, 1) * fil * (1 - a) * 3; }\n"   /* el tubo que se prende */
"  else if (t < 6.5) {\n"   /* el logo de la empresa, con su brillo y luz que se junta */
"    float a = saturate((t - 1.4) / 1.2) * saturate((6.3 - t) / 0.8);\n"
"    float l = letrero(uv, float2(0.5, 0.5), 0.07, 2, 27);\n"
"    float hal = exp(-pow((uv.y - 0.5) / 0.06, 2)) * exp(-pow((uv.x - 0.5) / 0.38, 2));\n"
"    c = (float3(0.85, 0.9, 1.0) * l * 1.6 + float3(0.15, 0.3, 0.8) * hal * 0.25) * a; }\n"
"  else if (t < 11.8) {\n"   /* las torres: bloques azules transparentes en el espacio oscuro, y el nombre de la consola */
"    float a = saturate((t - 6.5) / 0.8) * saturate((11.8 - t) / 0.7); float cam = (t - 6.5) * 0.05;\n"
"    c = float3(0.01, 0.015, 0.05) * (1 - uv.y * 0.5);\n"
"    for (int k = 0; k < 9; k++) {\n"
"      float x0 = frac(k * 0.137 + 0.11 - cam * (0.5 + k * 0.07)) * 1.2 - 0.1, hgt = 0.18 + 0.5 * frac(k * 0.61 + 0.3), w = 0.035 + 0.02 * frac(k * 0.37);\n"
"      float2 d = float2(abs(uv.x - x0) / w, (0.82 - uv.y) / hgt);\n"
"      if (d.x < 1 && d.y > 0 && d.y < 1) c += float3(0.12, 0.25, 0.75) * (0.35 + 0.65 * d.y) * (0.6 + 0.4 * (1 - d.x)) * 0.5;\n"
"      c += float3(0.1, 0.2, 0.6) * exp(-pow((uv.x - x0) / (w * 1.8), 2)) * saturate(1 - abs(0.82 - hgt * 0.5 - uv.y) / hgt) * 0.08; }\n"
"    c += float3(0.8, 0.85, 1.0) * letrero(uv, float2(0.5, 0.25), 0.075, 3, 13) * saturate((t - 9.2) / 0.8) * 1.5;\n"
"    c *= a; }\n"
"  else {\n"   /* el menú: fondo azul de noche con luz que se mueve, la esfera celeste con su estela, Browser y System Configuration */
"    float a = saturate((t - 11.8) / 1.0);\n"
"    float n1 = ruido.SampleLevel(s1, float3(uv * 0.8 + float2(T * 0.006, 0), T * 0.004), 0), n2 = ruido.SampleLevel(s1, float3(uv * 1.7 - float2(0, T * 0.009), 0.4 + T * 0.003), 0);\n"
"    c = float3(0.008, 0.016, 0.06) + float3(0.03, 0.07, 0.2) * pow(n1 * n2 * 2.2, 3) * 0.6 + float3(0.01, 0.02, 0.06) * (1 - uv.y);\n"
"    for (int k = 0; k < 14; k++) {\n"   /* la esfera y su estela: el mismo camino, un poco antes */
"      float tk = T * 0.23 - k * 0.045; float2 p = float2(0.5 + 0.33 * sin(tk * 1.1), 0.5 + 0.26 * sin(tk * 1.7 + 0.6));\n"
"      float2 d = (uv - p) * float2(1.333, 1); float r = k == 0 ? 0.028 : 0.022 * (1 - k / 15.0);\n"
"      float g = exp(-dot(d, d) / (r * r)), h = exp(-dot(d, d) / (r * r * 9));\n"
"      c += (k == 0 ? float3(0.75, 0.9, 1.0) * g * 2.2 + float3(0.2, 0.45, 1.0) * h * 0.5 : float3(0.15, 0.35, 0.9) * (g * 0.5 + h * 0.08) * (1 - k / 15.0)); }\n"
"    float pul = 0.75 + 0.25 * sin(T * 2.1);\n"
"    c += float3(0.85, 0.92, 1.0) * letrero(uv, float2(0.5, 0.43), 0.06, 0, 7) * 1.4 * pul;\n"   /* Browser, seleccionado */
"    c += float3(0.2, 0.45, 1.0) * exp(-pow((uv.y - 0.43) / 0.035, 2)) * exp(-pow((uv.x - 0.5) / 0.14, 2)) * 0.18 * pul;\n"
"    c += float3(0.55, 0.62, 0.75) * letrero(uv, float2(0.5, 0.57), 0.05, 1, 20) * 0.9;\n"   /* System Configuration */
"    c *= a; }\n"
"  return float4(c * tv0.y, 1); }\n"
"float4 ps_final(V i) : SV_Target {\n"
"  float3 c = max(bicubica(i.uv), 0) + bloom * t1.SampleLevel(s0, i.uv, 0).rgb;\n"
/* el reflejo de la tele en el vidrio: espejado, débil, doble (los dos vidrios); la tele de la cabaña era grande y cuadrada */
"  float2 tc = float2(0.225, 0.64), th = float2(0.125, 0.125 * 0.75 * tv0.w);\n"
"  for (int k = 0; k < 2; k++) {\n"
"    float2 q = (i.uv - tc - float2(0.0035, 0.004) * k) / th; float2 qa = abs(q);\n"
"    if (qa.x < 1.12 && qa.y < 1.12) {\n"
"      float2 tu = float2(1 - (q.x * (1 + 0.01 * q.y * q.y) * 0.5 + 0.5), q.y * (1 + 0.01 * q.x * q.x) * 0.5 + 0.5);\n"   /* espejada, con la comba apenas de una pantalla plana */
"      float r = pow(pow(qa.x, 40) + pow(qa.y, 40), 1.0 / 40), forma = saturate((1 - r) * 60);\n"   /* esquinas rectas */
"      float3 pant = t5.SampleLevel(s0, tu, 0).rgb;\n"
"      float marco = saturate(1 - abs(r - 1.05) * 22) * tv0.y;\n"   /* el mueble de la tele, apenas alumbrado por su propia pantalla */
"      c += (pant * forma + float3(0.010, 0.012, 0.016) * marco) * (k ? 0.05 : 0.09) * vm.w; }\n"
"  }\n"
"  float4 mk = t4.SampleLevel(s0, i.uv, 0); c = c * (1 - mk.a) + mk.rgb;\n"
"  c = sqrt(c / (1 + c) * brillo);\n"
"  float n = frac(52.9829189 * frac(dot(i.p.xy + cuadro * 5.588238, float2(0.06711056, 0.00583715))));\n"
"  c += (n - 0.5) / 255;\n"
"  float4 o = t2.SampleLevel(s0, i.uv, 0); c = c * (1 - o.a) + o.rgb;\n"
"  return float4(c, 1); }\n";

typedef HRESULT(WINAPI *compilar_f)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO *, ID3DInclude *, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob **, ID3DBlob **);
static ID3DBlob *gpu_compilar(compilar_f f, const char *punto, const char *perfil) {
    ID3DBlob *b = 0, *err = 0;
    if (FAILED(f(g_hlsl, sizeof g_hlsl - 1, "pacifico", 0, 0, punto, perfil, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &b, &err))) { if (err) ID3D10Blob_Release(err); return 0; }
    if (err) ID3D10Blob_Release(err);
    return b;
}
static int gpu_textura(int w, int h, DXGI_FORMAT fmt, int dinamica, ID3D11Texture2D **t, ID3D11ShaderResourceView **v, ID3D11RenderTargetView **r) {
    D3D11_TEXTURE2D_DESC d = { (UINT)w, (UINT)h, 1, 1, fmt, { 1, 0 }, dinamica ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_DEFAULT,
                               D3D11_BIND_SHADER_RESOURCE | (r ? D3D11_BIND_RENDER_TARGET : 0), dinamica ? D3D11_CPU_ACCESS_WRITE : 0, 0 };
    if (FAILED(ID3D11Device_CreateTexture2D(G.dev, &d, 0, t))) return 0;
    if (v && FAILED(ID3D11Device_CreateShaderResourceView(G.dev, (ID3D11Resource *)*t, 0, v))) return 0;
    if (r && FAILED(ID3D11Device_CreateRenderTargetView(G.dev, (ID3D11Resource *)*t, 0, r))) return 0;
    return 1;
}
static int gpu_salida(int w, int h) {   /* el destino final: la pantalla (cadena de intercambio) o, en las pruebas, una textura que se lee */
    G.ow = w; G.oh = h;
    if (G.sc) {
        if (G.bb_v) { ID3D11RenderTargetView_Release(G.bb_v); G.bb_v = 0; } if (G.bb) { ID3D11Texture2D_Release(G.bb); G.bb = 0; }
        ID3D11DeviceContext_OMSetRenderTargets(G.ctx, 0, 0, 0);
        if (FAILED(IDXGISwapChain_ResizeBuffers(G.sc, 0, (UINT)w, (UINT)h, DXGI_FORMAT_UNKNOWN, 0))) return 0;
        if (FAILED(IDXGISwapChain_GetBuffer(G.sc, 0, &IID_ID3D11Texture2D, (void **)&G.bb))) return 0;
        return SUCCEEDED(ID3D11Device_CreateRenderTargetView(G.dev, (ID3D11Resource *)G.bb, 0, &G.bb_v));
    }
    D3D11_TEXTURE2D_DESC d = { (UINT)w, (UINT)h, 1, 1, DXGI_FORMAT_B8G8R8A8_UNORM, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET, 0, 0 };
    if (FAILED(ID3D11Device_CreateTexture2D(G.dev, &d, 0, &G.fuera))) return 0;
    d.Usage = D3D11_USAGE_STAGING; d.BindFlags = 0; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(ID3D11Device_CreateTexture2D(G.dev, &d, 0, &G.copia))) return 0;
    return SUCCEEDED(ID3D11Device_CreateRenderTargetView(G.dev, (ID3D11Resource *)G.fuera, 0, &G.bb_v));
}
/* ruido de valor en 3D que se repite cada 64 vóxeles, en cuatro octavas: de ahí salen las formas de las nubes */
static void gpu_ruido(u8 *out) {
    static float acc[64 * 64 * 64], lat[32 * 32 * 32]; u32 r = 0x2F6B0E1Du; float amp = 0.5f, tot = 0;
    for (int L = 4; L <= 32; L *= 2, amp *= 0.55f) {
        for (int k = 0; k < L * L * L; k++) lat[k] = uni(&r, 0, 1);
        for (int z = 0; z < 64; z++) for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) {
            float fx = x * L / 64.0f, fy = y * L / 64.0f, fz = z * L / 64.0f; int ix = (int)fx, iy = (int)fy, iz = (int)fz;
            float tx = suave(fx - ix), ty = suave(fy - iy), tz = suave(fz - iz); int jx = (ix + 1) % L, jy = (iy + 1) % L, jz = (iz + 1) % L;
            #define LT(a, b, c) lat[((c) * L + (b)) * L + (a)]
            float a0 = LT(ix, iy, iz) + (LT(jx, iy, iz) - LT(ix, iy, iz)) * tx, a1 = LT(ix, jy, iz) + (LT(jx, jy, iz) - LT(ix, jy, iz)) * tx;
            float b0 = LT(ix, iy, jz) + (LT(jx, iy, jz) - LT(ix, iy, jz)) * tx, b1 = LT(ix, jy, jz) + (LT(jx, jy, jz) - LT(ix, jy, jz)) * tx;
            #undef LT
            float v = (a0 + (a1 - a0) * ty) * (1 - tz) + (b0 + (b1 - b0) * ty) * tz;
            acc[(z * 64 + y) * 64 + x] = (L == 4 ? 0 : acc[(z * 64 + y) * 64 + x]) + amp * v;
        }
        tot += amp;
    }
    for (int k = 0; k < 64 * 64 * 64; k++) { float v = acc[k] / tot; v = clampf((v - 0.25f) / 0.5f, 0, 1); out[k] = (u8)(v * 255 + 0.5f); }
}
/* hwnd = 0: sin ventana, para las pruebas (el resultado se lee con gpu_leer) */
static int gpu_iniciar(HWND hwnd, int w, int h, int ow, int oh) {
    HMODULE dc = LoadLibraryW(L"d3dcompiler_47.dll"); compilar_f compilar = dc ? (compilar_f)(void *)GetProcAddress(dc, "D3DCompile") : 0;
    if (!compilar) return 0;
    D3D_FEATURE_LEVEL nv = D3D_FEATURE_LEVEL_11_0, obtenido;
    if (hwnd) {
        DXGI_SWAP_CHAIN_DESC sd = { { (UINT)ow, (UINT)oh, { 0, 1 }, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 0 }, { 1, 0 }, DXGI_USAGE_RENDER_TARGET_OUTPUT, 2, hwnd, TRUE, (DXGI_SWAP_EFFECT)4 /* FLIP_DISCARD */, 0 };
        if (FAILED(D3D11CreateDeviceAndSwapChain(0, D3D_DRIVER_TYPE_HARDWARE, 0, 0, &nv, 1, D3D11_SDK_VERSION, &sd, &G.sc, &G.dev, &obtenido, &G.ctx))) return 0;
    } else if (FAILED(D3D11CreateDevice(0, D3D_DRIVER_TYPE_HARDWARE, 0, 0, &nv, 1, D3D11_SDK_VERSION, &G.dev, &obtenido, &G.ctx))) return 0;
    G.w = w; G.h = h; G.hwnd = hwnd;
    static const char *pt[10] = { "vs", "ps_baja", "ps_sube", "ps_final", "ps_cielo", "ps_luna", "ps_nubes", "ps_marco", "ps_borra", "ps_tele" };
    ID3DBlob *b[10]; for (int k = 0; k < 10; k++) if (!(b[k] = gpu_compilar(compilar, pt[k], k ? "ps_5_0" : "vs_5_0"))) return 0;
    if (FAILED(ID3D11Device_CreateVertexShader(G.dev, ID3D10Blob_GetBufferPointer(b[0]), ID3D10Blob_GetBufferSize(b[0]), 0, &G.vs))) return 0;
    ID3D11PixelShader **ps[9] = { &G.ps_baja, &G.ps_sube, &G.ps_final, &G.ps_cielo, &G.ps_luna, &G.ps_nubes, &G.ps_marco, &G.ps_borra, &G.ps_tele };
    for (int k = 0; k < 9; k++) if (FAILED(ID3D11Device_CreatePixelShader(G.dev, ID3D10Blob_GetBufferPointer(b[k + 1]), ID3D10Blob_GetBufferSize(b[k + 1]), 0, ps[k]))) return 0;
    for (int k = 0; k < 10; k++) ID3D10Blob_Release(b[k]);
    /* la pantalla de la tele, a 384 x 288 como un tubo de esos años */
    if (!gpu_textura(384, 288, DXGI_FORMAT_R16G16B16A16_FLOAT, 0, &G.tele, &G.tele_v, &G.tele_r)) return 0;
    { D3D11_BUFFER_DESC td = { 16, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0 }; if (FAILED(ID3D11Device_CreateBuffer(G.dev, &td, 0, &G.cbt))) return 0; }
    G.mw = ow; G.mh = oh;   /* la ventana, nítida y a la resolución de la pantalla */
    for (int k = 0; k < 2; k++) if (!gpu_textura(G.mw, G.mh, DXGI_FORMAT_R16G16B16A16_FLOAT, 0, &G.marco[k], &G.marco_v[k], &G.marco_r[k])) return 0;
    { D3D11_BUFFER_DESC md = { 64, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0 }; if (FAILED(ID3D11Device_CreateBuffer(G.dev, &md, 0, &G.cbm))) return 0; }
    if (!gpu_textura(w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, 1, &G.escena, &G.escena_v, 0)) return 0;
    if (!gpu_textura(w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, 0, &G.completa, &G.completa_v, &G.completa_r)) return 0;
    for (int k = 0; k < 2; k++) if (!gpu_textura(w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, 0, &G.acum[k], &G.acum_v[k], &G.acum_r[k])) return 0;
    {   /* el ruido de las nubes y lo que se lee de vuelta: cuánta luna pasa */
        static u8 vox[64 * 64 * 64]; gpu_ruido(vox);
        D3D11_TEXTURE3D_DESC d3 = { 64, 64, 64, 1, DXGI_FORMAT_R8_UNORM, D3D11_USAGE_IMMUTABLE, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
        D3D11_SUBRESOURCE_DATA sd = { vox, 64, 64 * 64 };
        if (FAILED(ID3D11Device_CreateTexture3D(G.dev, &d3, &sd, &G.ruido))) return 0;
        if (FAILED(ID3D11Device_CreateShaderResourceView(G.dev, (ID3D11Resource *)G.ruido, 0, &G.ruido_v))) return 0;
        D3D11_TEXTURE2D_DESC d1 = { 1, 1, 1, 1, DXGI_FORMAT_R32_FLOAT, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET, 0, 0 };
        if (FAILED(ID3D11Device_CreateTexture2D(G.dev, &d1, 0, &G.luna)) || FAILED(ID3D11Device_CreateRenderTargetView(G.dev, (ID3D11Resource *)G.luna, 0, &G.luna_r))) return 0;
        d1.Usage = D3D11_USAGE_STAGING; d1.BindFlags = 0; d1.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        for (int k = 0; k < 3; k++) if (FAILED(ID3D11Device_CreateTexture2D(G.dev, &d1, 0, &G.luna_c[k]))) return 0;
        D3D11_SAMPLER_DESC sr = { D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_TEXTURE_ADDRESS_WRAP, 0, 1, D3D11_COMPARISON_NEVER, { 0, 0, 0, 0 }, 0, D3D11_FLOAT32_MAX };
        if (FAILED(ID3D11Device_CreateSamplerState(G.dev, &sr, &G.repite))) return 0;
        D3D11_BUFFER_DESC kd = { 176, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0 };
        if (FAILED(ID3D11Device_CreateBuffer(G.dev, &kd, 0, &G.cbk))) return 0;
    }
    if (!gpu_textura(w, h, DXGI_FORMAT_R8G8B8A8_UNORM, 1, &G.ovl, &G.ovl_v, 0)) return 0;
    for (int k = 0; k < NBLOOM; k++) {
        G.nw[k] = w >> (k + 1); G.nh[k] = h >> (k + 1); if (G.nw[k] < 1) G.nw[k] = 1; if (G.nh[k] < 1) G.nh[k] = 1;
        if (!gpu_textura(G.nw[k], G.nh[k], DXGI_FORMAT_R16G16B16A16_FLOAT, 0, &G.nivel[k], &G.nivel_v[k], &G.nivel_r[k])) return 0;
    }
    D3D11_SAMPLER_DESC sm = { D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_CLAMP, 0, 1, D3D11_COMPARISON_NEVER, { 0, 0, 0, 0 }, 0, D3D11_FLOAT32_MAX };
    if (FAILED(ID3D11Device_CreateSamplerState(G.dev, &sm, &G.muestreo))) return 0;
    D3D11_BLEND_DESC bd = { 0 }; bd.RenderTarget[0].BlendEnable = TRUE; bd.RenderTarget[0].SrcBlend = bd.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
    bd.RenderTarget[0].BlendOp = bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD; bd.RenderTarget[0].SrcBlendAlpha = bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(ID3D11Device_CreateBlendState(G.dev, &bd, &G.suma))) return 0;
    D3D11_BUFFER_DESC cd = { 32, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0 };
    if (FAILED(ID3D11Device_CreateBuffer(G.dev, &cd, 0, &G.cb))) return 0;
    if (!gpu_salida(ow, oh)) return 0;
    OVL = reservar((size_t)w * h * 4); G.ovl_sucio = 1;
    g_gpu = 1; g_nubes_gpu = !getenv("SIN_NUBES");
    return 1;
}
static void gpu_abrir(void) {   /* la textura de la escena queda abierta para que fila_final escriba ahí */
    D3D11_MAPPED_SUBRESOURCE ms;
    if (SUCCEEDED(ID3D11DeviceContext_Map(G.ctx, (ID3D11Resource *)G.escena, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) { g_hdr = ms.pData; g_hdr_paso = (int)ms.RowPitch; }
}
static void gpu_cerrar(void) { if (g_hdr) { ID3D11DeviceContext_Unmap(G.ctx, (ID3D11Resource *)G.escena, 0); g_hdr = 0; } }
static void gpu_pasada(ID3D11PixelShader *ps, ID3D11RenderTargetView *rt, int w, int h, ID3D11ShaderResourceView **srv, int nsrv, float tx, float ty, float uno) {
    float c[8] = { tx, ty, (float)w, (float)h, 0.035f, CU.brillo, (float)(G.cuadro & 1023), uno };
    ID3D11DeviceContext_UpdateSubresource(G.ctx, (ID3D11Resource *)G.cb, 0, 0, c, 0, 0);
    ID3D11ShaderResourceView *nada[7] = { 0, 0, 0, 0, 0, 0, 0 }; ID3D11DeviceContext_PSSetShaderResources(G.ctx, 0, 7, nada);
    ID3D11DeviceContext_OMSetRenderTargets(G.ctx, 1, &rt, 0);
    D3D11_VIEWPORT vp = { 0, 0, (float)w, (float)h, 0, 1 }; ID3D11DeviceContext_RSSetViewports(G.ctx, 1, &vp);
    ID3D11DeviceContext_PSSetShader(G.ctx, ps, 0, 0); ID3D11DeviceContext_PSSetShaderResources(G.ctx, 0, nsrv, srv);
    ID3D11DeviceContext_Draw(G.ctx, 3, 0);
}
static int gpu_mostrar(int ovl_hay) {
    if (g_gpu_tam && G.sc) { RECT cr; GetClientRect(G.hwnd, &cr); if (cr.right > 0 && cr.bottom > 0) gpu_salida(cr.right, cr.bottom); InterlockedExchange(&g_gpu_tam, 0); }
    if (ovl_hay || G.ovl_sucio) {   /* los textos: se suben solo cuando hay o cuando hay que borrarlos */
        D3D11_MAPPED_SUBRESOURCE ms;
        if (SUCCEEDED(ID3D11DeviceContext_Map(G.ctx, (ID3D11Resource *)G.ovl, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
            for (int y = 0; y < G.h; y++) CopyMemory((u8 *)ms.pData + (size_t)y * ms.RowPitch, OVL + (size_t)y * G.w, (size_t)G.w * 4);
            ID3D11DeviceContext_Unmap(G.ctx, (ID3D11Resource *)G.ovl, 0);
        }
        G.ovl_sucio = ovl_hay;
    }
    ID3D11DeviceContext_IASetPrimitiveTopology(G.ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST); ID3D11DeviceContext_IASetInputLayout(G.ctx, 0);
    ID3D11DeviceContext_VSSetShader(G.ctx, G.vs, 0, 0); ID3D11DeviceContext_PSSetSamplers(G.ctx, 0, 1, &G.muestreo); ID3D11DeviceContext_PSSetConstantBuffers(G.ctx, 0, 1, &G.cb);
    float f4[4] = { 0, 0, 0, 0 };
    ID3D11DeviceContext_OMSetBlendState(G.ctx, 0, f4, 0xffffffff);
    /* el cielo: la escena del procesador más las nubes, la luna y las estrellas donde hay cielo abierto */
    ID3D11ShaderResourceView *base = G.escena_v;
    float lcx, lcz; a_camara(CU.mdx, CU.mdz, &lcx, &lcz);   /* la luz de la luna en la vista (el procesador la usa en metros de la grilla) */
    const float *gv = g_via_postal ? g_via : ASTRO.gal;      /* el cielo de verdad o la postal de la Vía Láctea */
    if (g_nubes_gpu) {
        float ml = CU.mluz, k[44] = { FOC, (float)YH, (float)G.w, (float)G.h, CU.mx, CU.my, CU.mr, CU.dis, CU.cov, CU.nx * 1500, CU.nz * 1500, CU.lluvia,
                        lcx, CU.mdy, lcz, 0, (float)CU.t, (float)(G.cuadro & 1023), G.cuadro ? 0.15f : 1.0f, G.pasos,
                        LUNA[0] * ml, LUNA[1] * ml, LUNA[2] * ml, ml,
                        ASTRO.ok ? CU.disco[0] : 1.0f, ASTRO.ok ? CU.disco[1] : 0.96f, ASTRO.ok ? CU.disco[2] : 0.86f, 0,
                        ASTRO.ok ? CU.sol[0] : -lcx, ASTRO.ok ? CU.sol[1] : -0.5f, ASTRO.ok ? CU.sol[2] : -lcz, ASTRO.ok ? CU.altsol : -40,
                        gv[0], gv[1], gv[2], g_via_postal ? 2.2f : 1.8f, gv[3], gv[4], gv[5], 0, gv[6], gv[7], gv[8], 0 };
        ID3D11DeviceContext_UpdateSubresource(G.ctx, (ID3D11Resource *)G.cbk, 0, 0, k, 0, 0);
        ID3D11DeviceContext_PSSetConstantBuffers(G.ctx, 1, 1, &G.cbk); ID3D11DeviceContext_PSSetSamplers(G.ctx, 1, 1, &G.repite);
        int a = (int)(G.cuadro & 1);
        ID3D11ShaderResourceView *cua[4] = { G.escena_v, G.acum_v[a ^ 1], 0, G.ruido_v }; gpu_pasada(G.ps_nubes, G.acum_r[a], G.w, G.h, cua, 4, 1.0f / G.w, 1.0f / G.h, 1);
        cua[1] = G.acum_v[a]; gpu_pasada(G.ps_cielo, G.completa_r, G.w, G.h, cua, 2, 1.0f / G.w, 1.0f / G.h, 1);
        cua[1] = 0; gpu_pasada(G.ps_luna, G.luna_r, 1, 1, cua, 4, 1, 1, 1);
        /* cuánta luna pasa entre las nubes: se copia y se lee dos cuadros después, sin esperar a la gráfica */
        int q = (int)(G.cuadro % 3); ID3D11DeviceContext_CopyResource(G.ctx, (ID3D11Resource *)G.luna_c[q], (ID3D11Resource *)G.luna);
        if (G.cuadro >= 2) {
            D3D11_MAPPED_SUBRESOURCE ms; ID3D11Resource *rq = (ID3D11Resource *)G.luna_c[(q + 1) % 3];
            if (ID3D11DeviceContext_Map(G.ctx, rq, 0, D3D11_MAP_READ, 0x100000L /* no esperar */, &ms) == S_OK) { g_mvis_gpu = clampf(*(float *)ms.pData, 0, 1); ID3D11DeviceContext_Unmap(G.ctx, rq, 0); }
        }
        base = G.completa_v;
    }
    /* el resplandor: la escena baja de a la mitad seis veces y vuelve a subir sumando cada nivel */
    gpu_pasada(G.ps_baja, G.nivel_r[0], G.nw[0], G.nh[0], &base, 1, 1.0f / G.w, 1.0f / G.h, 1);
    for (int k = 1; k < NBLOOM; k++) gpu_pasada(G.ps_baja, G.nivel_r[k], G.nw[k], G.nh[k], &G.nivel_v[k - 1], 1, 1.0f / G.nw[k - 1], 1.0f / G.nh[k - 1], 1);
    ID3D11DeviceContext_OMSetBlendState(G.ctx, G.suma, f4, 0xffffffff);
    for (int k = NBLOOM - 1; k > 0; k--) gpu_pasada(G.ps_sube, G.nivel_r[k - 1], G.nw[k - 1], G.nh[k - 1], &G.nivel_v[k], 1, 1.0f / G.nw[k], 1.0f / G.nh[k], 1.0f / NBLOOM);
    ID3D11DeviceContext_OMSetBlendState(G.ctx, 0, f4, 0xffffffff);
    {   /* la ventana de la cabaña, enfocada (así la quiso Fran) */
        float m[16] = { (float)G.mw, (float)G.mh, CU.tv, CU.dis, lcx, CU.mdy, lcz, CU.mvis, (float)CU.t, (float)CU.marco, 0, 0,
                        (0.003f + 0.016f * CU.mvis) * 0.85f, (0.003f + 0.016f * CU.mvis) * 0.9f, (0.004f + 0.016f * CU.mvis) * 1.0f, 0 };
        ID3D11DeviceContext_UpdateSubresource(G.ctx, (ID3D11Resource *)G.cbm, 0, 0, m, 0, 0);
        ID3D11DeviceContext_PSSetConstantBuffers(G.ctx, 2, 1, &G.cbm); ID3D11DeviceContext_PSSetSamplers(G.ctx, 1, 1, &G.repite);
        ID3D11ShaderResourceView *cua[4] = { 0, 0, 0, G.ruido_v };
        gpu_pasada(G.ps_marco, G.marco_r[0], G.mw, G.mh, cua, 4, 1.0f / G.mw, 1.0f / G.mh, 1);
    }
    {   /* la tele: la Play en su menú, dibujada en su propia pantalla para después reflejarse en el vidrio */
        float tv[4] = { g_tele_t, g_tele_on, (float)CU.t, (float)G.ow / G.oh };
        ID3D11DeviceContext_UpdateSubresource(G.ctx, (ID3D11Resource *)G.cbt, 0, 0, tv, 0, 0); ID3D11DeviceContext_PSSetConstantBuffers(G.ctx, 3, 1, &G.cbt);
        ID3D11ShaderResourceView *sie[7] = { 0, 0, 0, G.ruido_v, 0, 0, G.atlas_v };
        gpu_pasada(G.ps_tele, G.tele_r, 384, 288, sie, 7, 1.0f / 384, 1.0f / 288, 1);
    }
    ID3D11ShaderResourceView *siete[7] = { base, G.nivel_v[0], G.ovl_v, G.ruido_v, G.marco_v[0], G.tele_v, G.atlas_v };
    gpu_pasada(G.ps_final, G.bb_v, G.ow, G.oh, siete, 7, 1.0f / G.w, 1.0f / G.h, 1);
    G.cuadro++;
    if (G.sc) { HRESULT r = IDXGISwapChain_Present(G.sc, 1, 0); if (r == DXGI_ERROR_DEVICE_REMOVED || r == DXGI_ERROR_DEVICE_RESET) { g_gpu = 0; return 0; } }
    return 1;
}
static int gpu_atlas(const u8 *px) {   /* los letreros de la Play (512 x 96, una fila de 24 px por letrero) */
    D3D11_TEXTURE2D_DESC d = { 512, 96, 1, 1, DXGI_FORMAT_R8_UNORM, { 1, 0 }, D3D11_USAGE_IMMUTABLE, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
    D3D11_SUBRESOURCE_DATA sd = { px, 512, 0 };
    return SUCCEEDED(ID3D11Device_CreateTexture2D(G.dev, &d, &sd, &G.atlas)) && SUCCEEDED(ID3D11Device_CreateShaderResourceView(G.dev, (ID3D11Resource *)G.atlas, 0, &G.atlas_v));
}
static void gpu_leer(u32 *dst) {   /* para las pruebas: el cuadro que la gráfica dejó, a memoria */
    ID3D11DeviceContext_CopyResource(G.ctx, (ID3D11Resource *)G.copia, (ID3D11Resource *)G.fuera);
    D3D11_MAPPED_SUBRESOURCE ms;
    if (SUCCEEDED(ID3D11DeviceContext_Map(G.ctx, (ID3D11Resource *)G.copia, 0, D3D11_MAP_READ, 0, &ms))) {
        for (int y = 0; y < G.oh; y++) CopyMemory(dst + (size_t)y * G.ow, (u8 *)ms.pData + (size_t)y * ms.RowPitch, (size_t)G.ow * 4);
        ID3D11DeviceContext_Unmap(G.ctx, (ID3D11Resource *)G.copia, 0);
    }
}
