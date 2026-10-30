"""Genera src/fuente.h: una fuente de mapa de bits 10x20 con alfa de 8 bits, sacada de Cascadia Mono (Microsoft, licencia
SIL Open Font License 1.1). Queda embebida en el programa para que el texto se dibuje píxel a píxel igual en cualquier equipo.
Uso: python gen_fuente.py <salida.h> [ruta de la fuente .ttf] [tamaño en px]"""
import sys
from PIL import Image, ImageDraw, ImageFont

ANCHO, ALTO = 10, 20
RUTA = sys.argv[2] if len(sys.argv) > 2 else 'C:/Windows/Fonts/CascadiaMono.ttf'
TAM = int(sys.argv[3]) if len(sys.argv) > 3 else 16
fuente = ImageFont.truetype(RUTA, TAM)
caracteres = [chr(c) for c in range(32, 127)] + list('áéíóúÁÉÍÓÚñÑüÜ¿¡·—…←↑→↓%')
caracteres = list(dict.fromkeys(caracteres))
ascenso, _ = fuente.getmetrics()
base = (ALTO - (ascenso + 4)) // 2 + ascenso

lineas = ['/* Generado por herramientas/gen_fuente.py desde Cascadia Mono %d px (SIL Open Font License 1.1). No editar a mano. */' % TAM,
          '#pragma once', f'#define FUENTE_ANCHO {ANCHO}', f'#define FUENTE_ALTO {ALTO}', f'#define FUENTE_N {len(caracteres)}',
          'static const unsigned int fuente_cp[FUENTE_N] = {' + ','.join(str(ord(c)) for c in caracteres) + '};',
          'static const unsigned char fuente_alfa[FUENTE_N][FUENTE_ALTO * FUENTE_ANCHO] = {']
for c in caracteres:
    img = Image.new('L', (ANCHO, ALTO), 0)
    ImageDraw.Draw(img).text((0, base), c, font=fuente, fill=255, anchor='ls')
    lineas.append('{' + ','.join(str(v) for v in img.tobytes()) + '},')
lineas.append('};')
open(sys.argv[1], 'w', encoding='utf-8', newline='\n').write('\n'.join(lineas) + '\n')
print('ok', len(caracteres), 'glifos')
