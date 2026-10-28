# Matrox 2D: the HAL's primitives (sourced)

Written (2026-10-14) for GLOS M7 step B: the HAL's 2D drawing, hardware
cursor, CRTC display start and VRAM allocator (`hal/src/engine.c`,
`blit.c`, `cursor.c`, `vram.c`; declared in `hal/include/mga/hal.h`),
86Box local patch 0117, and TWODTEST (`tests/hal/twodtest.c`). Each rule
gives its public source; section 7 lists what only a real card can settle.

Tags: **[spec]** Matrox wrote it down; **[code]** what X.org does; **[inf]** my inference.

Sources (pages: the printed one, the PDF's in brackets):
- **SPEC**: *Matrox G400 Specification*, Jun 1999. http://www.bitsavers.org/pdf/matrox/G400SPEC_Jun1999.PDF
- **G200SPEC**: *MGA-G200 Specification*, Nov 1998. https://archive.decromancer.ca/bitsavers.org/components/matrox/_dataSheets/MGA-G200_199811.pdf
- **G100SPEC**: *MGA-G100 Specification*, Feb 1998. https://archive.decromancer.ca/bitsavers.org/components/matrox/_dataSheets/MGA-G100_199802.pdf
- **X**: https://gitlab.freedesktop.org/xorg/driver/xf86-video-mga/-/blob/master/src/ ; **X165**: the same at tag
  `xf86-video-mga-1.6.5`, the last with XAA's blits and colour expansion. MIT; read for behaviour, nothing copied.

There is no G450 specification: the G400's rules are taken to hold for it **[inf]** (X.org treats them as one chip).

## 1. Common ground

- **Opcodes.** BITBLT is `1000`, ILOAD `1001`. `1100`, the Millennium's FBITBLT, is *Reserved* on the G100,
  G200 and G400 (SPEC p.3-132 [pdf 172]; G200SPEC p.3-99 [pdf 139]; G100SPEC p.4-63 [pdf 103]) **[spec]**, and
  X.org turns its fast blit off on every G chip (X:mga_dacG.c#L2277) **[code]**. The HAL never uses it.
- **atype is RSTR** for BITBLT and ILOAD: the G200 and G400 diagrams fix it (SPEC p.4-59, p.4-65 [pdf 507, 513];
  G200SPEC p.4-51, p.4-57 [pdf 429, 435]); the G100 allows RPL too **[spec]**. With bop S the destination is not
  read (SPEC p.3-135 [pdf 175]), so RSTR costs nothing **[spec]**. (The HAL's ILOAD used RPL until 0117's work.)
- **DWGCTL before the AR registers.** "Writing to this register when the DWGCTL register's arzero bit = 1 will
  produce unpredictable results" (G100SPEC, every AR register) **[spec]**, and a fill's DWGCTL has arzero. Every
  2D path writes DWGCTL first, then its AR registers, then YDSTLEN with the start offset.
- **Colours.** FCOL and BCOL repeated over 32 bits at 8 and 16 bits a pixel (`engine_colour`): the G100 needs it,
  the G200 and G400 use the low bits (G100SPEC p.4-70 [pdf 110]; SPEC p.3-140 [pdf 180]) **[spec]**.
- **Clip.** CXBNDRY is two inclusive pixel columns; YTOP and YBOT are linear pixel addresses (line x PITCH, plus
  YDSTORG on the G100), not counting DSTORG (SPEC p.3-107..3-109 [pdf 147-149], p.3-282 [pdf 322]) **[spec, inf]**.
  The G100 cannot turn clipping off (G100SPEC p.4-69 [pdf 109]) **[spec]**.
- **Origins.** DSTORG and SRCORG are byte addresses: 8-byte units with bits 4:3 zero on the G200 (32 bytes), 64-byte
  units on the G400 (G200SPEC p.3-97, p.3-154 [pdf 137, 194]; SPEC p.3-130, p.3-187 [pdf 170, 227]) **[spec]**.
  The HAL's targets start on 64 bytes. The G100 has neither; YDSTORG (pixels) places its destination, and does not
  move a BITBLT's source (X165:mga_macros.h#L41) **[code, inf]**.

## 2. Copies (`engine_copy`, `engine_copy_from`)

BITBLT, xy source, bltmod BFCOL, bop S, shftzero: `0x040C4018` (SPEC §4.5.7 p.4-58/59 [pdf 506-507]) **[spec]**.

- AR3 is the start address and AR0 "the last pixel of the first line", both in pixels; AR5 the source pitch, negative
  to scan up; SGN `scanleft` (bit 0) right to left, `sdy` (bit 2) bottom to top; YDST the first destination row in
  scan order; FXBNDRY the destination columns either way (SPEC p.3-39..3-44 [pdf 79-84], p.3-172 [pdf 212]) **[spec]**.
- Overlap: rows bottom to top when the destination is below the source, right to left on the same rows when it is to
  the right **[inf]** (X165:mga_storm.c#L1285-1396 **[code]**). scanleft is only for BFCOL **[spec]**.
- **The source is SRCORG + AR3** on the G200 and G400 **[inf]**: the spec names SRCORG "an offset value for the position
  of the first pixel for a source surface"; X.org's EXA sets it to the source pixmap (X:mga_exa.c#L242) **[code]**.
- **AR0 has 18 bits, signed, on the G100 and G200, 22 on the G400** (SPEC p.3-39; G200SPEC p.3-36; G100SPEC p.4-31)
  **[spec]**: past line 128 of a 1024-pixel screen an absolute end address no longer fits, and no specification says
  what happens then. So on the G200 and G400 the HAL points SRCORG at the band of rows it copies (rounded down to 64
  bytes, the rest in AR3) and copies a tall rectangle a band at a time, each band's addresses within AR0's range
  (bottom band first when scanning up). The G100 has no SRCORG: its addresses are absolute, as X.org's are (§7).
- **Between other surfaces (`engine_vram_copy`, `engine_vram_move`; G200 and G400).** These copy from one surface in
  VRAM to another, neither of them the target, queued like draws.
  - **Use:** DOS-GL changes a texture that queued draws still read in a new block. The CPU writes the new texels
    there and these copies bring the rest over from the old block, so neither waits for the engine.
  - **Registers:** the same BITBLT, with sgnzero and clipdis, MACCESS at the copy's depth, and PITCH as both pitches
    (AR5 the source's).
  - **Origins:** DSTORG is rounded down to 64 bytes like SRCORG, the rest going into the columns. Past the pitch,
    those run on into the next line, as linear addresses do **[inf]**.
  - **`engine_vram_move`:** copies a byte range as lines of 32 pixels (64 at 8 bits), of the widest pixel that the
    addresses and the length allow.
  - **After them:** `engine_vram_copy_end` writes the target back (PITCH, the origins, MACCESS, the clip). PLNWT is
    left all ones.
  - **WARP triangles pending:** the first copy starts with two DWGSYNC writes, as Linux's DRM starts its blits,
    because DSTORG, PITCH, MACCESS and DWGCTL change under them. The clip's two zero-length draws did harm here
    ([g400-warp.md](g400-warp.md), "Copies among WARP triangles").

## 3. Colour expansion

**From the CPU (`engine_mono`).** ILOAD with bltmod BMONOWF: "Windows format", the first pixel in bit 7 of each byte,
bytes in order (SPEC p.4-11 [pdf 459], MONO B) **[spec]**, which is how GLOS's glyphs are stored. Its source must be
linear (SPEC §4.5.8.3 p.4-67 [pdf 515]) **[spec]**: one stream, rows not padded, `INT((w*h+31)/32)` dwords (SPEC
p.4-63 [pdf 511]) **[spec]**. To feed rows padded to dwords, the destination is made as wide as a padded row and the
clip cuts the padding off (X165:mga_storm.c#L1720-1784) **[code]**. AR0 counts the pixels of the whole load (the
linear table, SPEC p.4-64 [pdf 512]) **[spec]**, so loads go in parts of at most 2^17 pixels. `transc` makes clear bits
transparent (SPEC p.3-138 [pdf 178]) **[spec]**.

**From VRAM (`engine_mono_store`, `engine_mono_vram`).** BITBLT with BMONOWF on the G200 and G400; the G100's BITBLT
takes only BMONOLEF, bit 0 first (G100SPEC p.5-49 [pdf 349]) **[spec]**, so `engine_mono_store` mirrors each byte there.
Addresses count bits **[inf]** (X165:mga_storm.c#L2103-2164 **[code]**); AR5 is the row pitch in bits. AR3 has 24 bits
plus `spage` (26:24) for one-bit sources, which the ALU does not carry into (SPEC p.3-42 [pdf 82]) **[spec]**: on the
G100 a source row must not cross a 2 MB boundary. On the G200 and G400 SRCORG keeps the addresses small, in bands as for
copies.

## 4. Image loads (`engine_image_begin`, `engine_iload_*`)

ILOAD, xy source, BFCOL (SPEC §4.5.8.2 p.4-65 [pdf 513]) **[spec]**: pixels in the target's format, each row padded to a
dword, `AR0 = w - 1`, AR3 and AR5 zero, OPMODE in DMA blit mode (dmamod `01`) before the data, written only while the
engine is idle: it is not queued (SPEC p.3-159 [pdf 199], p.4-68 [pdf 516]) **[spec]**. The engine takes exactly the
pixels it expects: fewer hang it, more are read as register writes (SPEC p.4-63 [pdf 511]) **[spec]**.

## 5. Hardware cursor (`cursor_*`)

- XCURADDL/H (indirect 04h, 05h): the image's address bits 17:10, and 22:18 (G100) or 23:18 (G200, G400), so 1 KB
  aligned within the first 8 or 16 MB (G100SPEC p.4-220; G200SPEC p.3-319 [pdf 355]; SPEC p.3-383 [pdf 423]) **[spec]**.
- The image: 64 lines of two 64-bit slices, plane 0 then plane 1, the first pixel in bit 63 (G100SPEC p.4-220 [pdf
  260]; SPEC p.3-383) **[spec]**: an 8-byte row stored back to front (X:mga_dacG.c#L1925) **[code]**.
- XCURCTRL (06h) mode `10`, XGA: planes 1, 0 = `00` colour 0, `01` colour 1, `10` the screen, `11` the screen inverted
  (SPEC p.3-386 [pdf 426]) **[spec]**. Plane 1 as an AND mask and plane 0 as an XOR mask is the DOS cursor exactly
  **[inf]**, which is the HAL's interface.
- XCURCOL0/1 (08h-0Ah, 0Ch-0Eh): 8-bit red, green and blue (G200SPEC p.3-321 [pdf 357]) **[spec]**.
- CURPOS (3C0Ch-3C0Fh) holds the bottom right corner: "loading 001h into curposx causes the rightmost pixel of the
  cursor to be displayed on the leftmost pixel of the screen", 0 hides it; a new position counts from the next vertical
  retrace and must not be written during one (SPEC p.3-368 [pdf 408]) **[spec]**. So `x + 64`, `y + 64`, after waiting
  for STATUS.vsyncsts to clear (X:mga_dacG.c#L1964-1979) **[code]**.

## 6. Display start (`crtc_set_start`)

startadd is CRTC 0Dh (7:0), 0Ch (15:8), CRTCEXT0 3:0 (19:16) and CRTCEXT0 bit 6 (20), in units of 8 bytes at 8, 16
and 32 bits a pixel ("startadd = address of the first pixel to display / factor": 8, 4, 2 pixels) (SPEC §4.6.5 p.4-75
[pdf 523], p.3-333 [pdf 373]; G100SPEC p.5-71, p.4-174) **[spec]**. CRTCEXT0 must be written last; the new start holds
from the next horizontal retrace, "permitting page flipping at any line", so a flip between frames is the caller's,
in the vertical blank (same pages) **[spec]**. The HAL keeps CRTCEXT0's other bits.

## 7. For the bench

1. A BITBLT whose AR0 (the end of its first line) is past 2^17 pixels on a G100 or G200 (2^21 on a G400), as the
   G100's absolute copies are below line 128 of a 1024-pixel screen. X.org relies on it working.
2. SRCORG + AR3 as §2 has it, scanning up with SRCORG not zero.
3. A one-bit source crossing a 2^24-bit boundary on the G100 (does spage carry?).
4. Whether the cursor colours bypass the palette (86Box passed the XGA mode's through it; 0117 stops that).
5. Whether XCURADDL/H take effect at once (the G200's and the register pages say so) or at the retrace (SPEC §4.7.3).
6. The G100's MACCESS bit 14, jedecrst, "Always program this to '1'" (G100SPEC p.4-82): X.org sets it, the HAL does not.

## 8. 86Box

Local patch 0117 makes the model follow §2 and §5: BITBLT reads at SRCORG + AR3 on the G200 and later, CRTCEXT0 bit 6
is startadd bit 20, XCURADDH has 6 bits on the G200 and G400, cursor colours are red, green, blue (they were swapped)
and not passed through the palette, the three-colour mode draws, and the cursor is 64 lines tall (the SVGA core's
default was 32).

TWODTEST checks it all: at 8, 16 and 32 bits a pixel, fills, copies (overlapping, between surfaces of other pitches,
the whole screen a line or a column each way), expansion from the CPU and from VRAM, opaque and transparent, image
loads, clipped and partly off the surface, and a mixed run with `engine_resync` in it, each against its own software
(`tests/hal/sw2d.c`) byte for byte; then the cursor and the display start, from the emulated monitor. `make
loopa-twodtest` runs it on the four AGP cards and the PCI G200 and G450 on the 486DX2.
