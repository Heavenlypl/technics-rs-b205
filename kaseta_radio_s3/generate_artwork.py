"""Jedna maska szpuli w pamięci flash, 4 bity/piksel; bez klatek animacji."""
from pathlib import Path
import math

SIZE = 216
ART_ORIGIN = SIZE / 3.2
SLOT_WIDTH_SCALE = 1.08 * 1.10
SLOT_SLOPE_SQUARED = math.tan(math.atan(0.3) * SLOT_WIDTH_SCALE) ** 2
LOGO_SCALE = 1.35
LOGO_BOLD_RADIUS = 0.28
DOT_RADIUS = 2.5
DOT_CENTERS = [(24 * math.cos(a), 24 * math.sin(a)) for a in
               (math.pi / 3, math.pi, 5 * math.pi / 3)]
letters = [[31,4,4,4,4,4,4], [30,17,17,17,17,17,30], [17,18,20,24,20,18,17]]

def base_logo_at(x, y):
    if not (-8.5 <= x < 8.5 and -3.5 <= y < 3.5):
        return False
    col,row=int(x+8.5),int(y+3.5)
    letter,bit=divmod(col,6)
    return bit<5 and letter<3 and bool(letters[letter][row] & (1<<(4-bit)))

def classify(x, y):
    d2 = x*x+y*y
    if d2 < 19*19:
        if d2 > 18*18: return 3
        if d2 > 16*16: return 4
        if d2 > 13*13: return 5
        if d2 > 10*10:
            for i in range(3):
                a = i*math.pi/3
                cx = x*math.cos(a)+y*math.sin(a)
                cy = -x*math.sin(a)+y*math.cos(a)
                if abs(cy)<2 and abs(cx)>10: return 7
        return 6
    if d2 > 65*65: return 2
    if d2 > 64*64: return 1
    if 21*21 < d2 < 27*27 and any(
        (x-cx)**2 + (y-cy)**2 <= DOT_RADIUS**2 for cx, cy in DOT_CENTERS
    ):
        return 10
    if 25*25 < d2 < 60*60:
        for i in range(3):
            a=i*2*math.pi/3
            dot=x*math.cos(a)+y*math.sin(a)
            cross=-x*math.sin(a)+y*math.cos(a)
            if dot>0 and cross*cross<SLOT_SLOPE_SQUARED*dot*dot: return 8
    dx, dy = x-23, y+39.837169
    tx,ty = .8660254*dx+.5*dy, -.5*dx+.8660254*dy
    tx,ty=tx/LOGO_SCALE,ty/LOGO_SCALE
    if abs(tx)<8.5+LOGO_BOLD_RADIUS and abs(ty)<3.5+LOGO_BOLD_RADIUS:
        offsets=(-LOGO_BOLD_RADIUS,0,LOGO_BOLD_RADIUS)
        if any(base_logo_at(tx+ox,ty+oy) for ox in offsets for oy in offsets):
            return 9
    return 0

data=[]
for y in range(SIZE):
    for x in range(0,SIZE,2):
        a=classify((x+.5)/1.6-ART_ORIGIN,(y+.5)/1.6-ART_ORIGIN)
        b=classify((x+1.5)/1.6-ART_ORIGIN,(y+.5)/1.6-ART_ORIGIN)
        data.append(a | (b<<4))
assert len(data)==23328
out=['// Generated reel artwork. Palette 9 is the live indicator; 10 holds the three dots.',
     '#pragma once', '#include <Arduino.h>',
     'static const uint8_t REEL_ARTWORK[23328] PROGMEM = {']
for i in range(0,len(data),24):
    out.append('  '+','.join(f'0x{v:02x}' for v in data[i:i+24])+',')
out.append('};')
Path(__file__).with_name('reel_artwork.h').write_text('\n'.join(out)+'\n')
print('Maska szpuli: 23328 bajtów flash; 0 bajtów bufora RAM.')
