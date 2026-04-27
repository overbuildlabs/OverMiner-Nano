"""Convert a PNG into an LVGL v8.3 'True Color with Alpha' C source file.

Targets a build with:
  LV_COLOR_DEPTH = 16
  LV_COLOR_16_SWAP = 1

Output format per pixel (3 bytes):
  byte 0: RGB565 high byte (swapped: RRRRRGGG)
  byte 1: RGB565 low byte  (swapped: GGGBBBBB)
  byte 2: alpha (0=transparent, 255=opaque)
"""

from PIL import Image
import sys, os

INPUT  = r"L:\PoPManager\src-tauri\icons\32x32.png"
OUTPUT = r"L:\PoPMiner\PoPMiner_Nano\pop_logo.c"
NAME   = "pop_logo"

# Match TFT_SWAP_RB in PoPMiner_Nano.ino - if the panel has BGR-swapped
# channels we pre-swap R<->B in the image data so it renders correctly.
SWAP_RB = False

img = Image.open(INPUT).convert("RGBA")
w, h = img.size
print(f"Loaded {INPUT}: {w}x{h}, {len(img.getdata())} pixels")

bytes_out = bytearray()
for r, g, b, a in img.getdata():
    if SWAP_RB:
        r, b = b, r
    r5 = (r >> 3) & 0x1F
    g6 = (g >> 2) & 0x3F
    b5 = (b >> 3) & 0x1F
    rgb565 = (r5 << 11) | (g6 << 5) | b5
    # LV_COLOR_16_SWAP=0 + little-endian ESP32 -> low byte first.
    # Must match the LV_COLOR_16_SWAP setting in lv_conf.h.
    bytes_out.append(rgb565 & 0xFF)
    bytes_out.append((rgb565 >> 8) & 0xFF)
    bytes_out.append(a)

# Format as C array with 12 bytes per row for readability
lines = []
for i in range(0, len(bytes_out), 12):
    chunk = bytes_out[i:i+12]
    lines.append("  " + ", ".join(f"0x{b:02x}" for b in chunk) + ",")

with open(OUTPUT, "w") as f:
    f.write(f"""// Tell LVGL to find lv_conf.h next to the sketch (matches the .ino).
#ifndef LV_CONF_INCLUDE_SIMPLE
#define LV_CONF_INCLUDE_SIMPLE
#endif

#if defined(LV_LVGL_H_INCLUDE_SIMPLE)
#include "lvgl.h"
#else
#include <lvgl.h>
#endif

#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif

#ifndef LV_ATTRIBUTE_LARGE_CONST
#define LV_ATTRIBUTE_LARGE_CONST
#endif

#ifndef LV_ATTRIBUTE_IMG_{NAME.upper()}
#define LV_ATTRIBUTE_IMG_{NAME.upper()}
#endif

const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST LV_ATTRIBUTE_IMG_{NAME.upper()} uint8_t {NAME}_map[] = {{
""")
    f.write("\n".join(lines))
    f.write(f"""
}};

const lv_img_dsc_t {NAME} = {{
  .header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA,
  .header.always_zero = 0,
  .header.reserved = 0,
  .header.w = {w},
  .header.h = {h},
  .data_size = {w * h} * LV_IMG_PX_SIZE_ALPHA_BYTE,
  .data = {NAME}_map,
}};
""")

print(f"Wrote {OUTPUT}: {len(bytes_out)} bytes payload, {os.path.getsize(OUTPUT)} bytes total")
