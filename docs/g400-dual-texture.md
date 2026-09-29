# Matrox G400 dual texturing: register reference (sourced)

Collected (2026-09-29) for the Quake plan's Q5: 86Box local patch 0009
(dual texturing), the HAL's G400 combiner path and DOS-GL's
GL_ARB_multitexture. Facts only, each with its public source; section 7
lists what only the silicon can settle (bench experiments E1-E7).

Tags: **[spec]** Matrox wrote it down; **[code]** what open drivers do; **[inf]** my inference; **[?]** unknown.

Source bases (the anchors below are appended to these):
- **SPEC**: *Matrox G400 Specification*, Jun 1999. http://www.bitsavers.org/pdf/matrox/G400SPEC_Jun1999.PDF
  (a curl-friendly mirror: https://www.mirrorservice.org/sites/www.bitsavers.org/pdf/matrox/G400SPEC_Jun1999.PDF).
  Cited as the printed page, with the PDF page in brackets.
- **G200SPEC**: *MGA-G200 Specification*, Nov 1998. https://archive.decromancer.ca/bitsavers.org/components/matrox/_dataSheets/MGA-G200_199811.pdf
- **M**: https://gitlab.freedesktop.org/mesa/mesa/-/blob/mesa-7.11/src/mesa/drivers/dri/mga/
- **K**: https://github.com/torvalds/linux/blob/v6.2/ (drm/mga was removed after v6.2)
- **X**: https://gitlab.freedesktop.org/xorg/driver/xf86-video-mga/-/blob/master/src/

## 1. Registers

| Reg | MMIO | DMA idx | Per-map? |
|---|---|---|---|
| TMR0..TMR8 | 2C00..2C20 | 80h..88h | yes |
| TEXORG / TEXWIDTH / TEXHEIGHT | 2C24 / 2C28 / 2C2C | 89h / 8Ah / 8Bh | yes (W, H carry map1 at bit 31) |
| TEXCTL / TEXTRANS / TEXTRANSHIGH / TEXCTL2 | 2C30 / 2C34 / 2C38 / 2C3C | 8Ch..8Fh | yes (TEXCTL2 carries map1 at bit 31) |
| TEXFILTER / TEXBORDERCOL (32-bit ARGB) | 2C58 / 2C5C | 96h / 97h | yes |
| ALPHACTRL | 2C7C | 9Fh | **yes** |
| TEXORG1..TEXORG4 | 2CA4..2CB0 | A9h..ACh | yes |
| TBUMPMAT / TBUMPFMT | 2CF0 / 2CF4 | BCh / BDh | not listed as per-map |
| TDUALSTAGE0 / TDUALSTAGE1 | 2CF8 / 2CFC | BEh / BFh | no (one copy of each) |
| FCOL (the combiner's "FCOL"/"Fact" input) | 1C24 | 09h | no |

- Addresses and indices come from the register map in SPEC p.2-12 [pdf 34], and match M:mgaregs.h#L805, #L899 and #L1041-1214 and K:drivers/gpu/drm/mga/mga_drv.h#L513-536. **[spec]**
- The per-map list comes from the TEXCTL2.map1 table in SPEC p.3-216 [pdf 256], repeated at p.3-219 and p.3-228. For each register, `tmap0dis=0` writes TMAP0 and TMAP1, and `tmap0dis=1` writes TMAP1 only. **[spec]**
  - The prose "broadcast" note beside that table leaves out TEXWIDTH, TEXTRANS and TEXTRANSHIGH, but the table includes them.

**TEXCTL2 bits** (SPEC p.3-215/216 [pdf 255-256]; M:mgaregs.h#L1093-1124; X:mga_reg.h#L561-572):
- Bit 0 `decalblend`, bit 1 `idecal`, bit 2 `decaldis`, bit 3 reserved.
- Bit 4 `ckstransdis`, bit 5 `borderen`, bit 6 `specen`, bit 7 `dualtex`.
- Bit 8 `tablefog`, bit 9 `bumpmapping`, bits 30:10 reserved ("must be 0"), bit 31 `map1`.
- Spec rules:
  - `dualtex` "must be set to the same value in both tmaps" (the same rule applies to `bumpmapping`).
  - `tablefog` must be 0 in single texturing.
  - "When in Single Texturing mode, map1 of TEXCTL2, TEXWIDTH, and TEXHEIGHT must be set to '0'".
- There is no "tmap0dis"-like field inside TEXCTL2. `tmap0dis` is an internal state, described next.
- **Bit 15, `MGA_G400_TC2_MAGIC` (0x8000):** the kernel ORs it into every G400 TEXCTL2 write (K:drivers/gpu/drm/mga/mga_drv.h#L519, mga_state.c#L155, #L195-218, #L283-289), and X EXA does too (X:mga_reg.h#L562, mga_exa.c#L408). The spec shows bit 15 as reserved in both its Rev A and its Rev B diagrams, which are otherwise identical. No public explanation found. **[code]** **[?]**

**How map 1 is selected** (SPEC p.3-216, p.3-219, p.3-228): the spec gives the rule `tmap0dis <= TEXCTL2.map1 or TEXWIDTH.map1 or TEXHEIGHT.map1`, and says "tmap0dis is effective on the next access". **[spec]**
- **[inf]** The write that sets map1 is therefore still routed with the old `tmap0dis=0`, so it reaches both maps. The write that clears map1 reaches TMAP1 only, and then broadcast resumes.
  - X EXA depends on this. Its tmu0 TEXCTL2 write has no `dualtex`, and only the map1 write carries `DUALTEX|MAP1` (X:mga_exa.c#L408-438). With this reading, TMAP0 still ends up with `dualtex=1`.
  - Emulator model: keep the last-written bit 31 of each of the three registers. Route each write using the OR of those three bits as they stood *before* that write.

**Kernel `mga_g400_emit_tex1`, in order** (K:drivers/gpu/drm/mga/mga_state.c#L184-221). Every write goes through the DMA stream. **[code]**
1. `TEXCTL2 = tex1.texctl2 | MAP1(1<<31) | MAGIC(1<<15)`
2. `TEXCTL`, `TEXFILTER`, `TEXBORDERCOL`
3. `TEXORG`, `TEXORG1`, `TEXORG2`, `TEXORG3`, `TEXORG4`
4. `TEXWIDTH`, `TEXHEIGHT` (bit 31 clear; Mesa's values never set it)
5. WARP scratch registers, which are not per-map: `WR49=0`, `WR57=0`, `WR53=0`, `WR61=0`, `WR52 = texwidth|0x40`, `WR60 = texheight|0x40`
6. `TEXTRANS = 0x0000FFFF`, `TEXTRANSHIGH = 0x0000FFFF`
7. `TEXCTL2 = tex1.texctl2 | MAGIC`. Map1 is now clear, which returns the next access to broadcast mode.

When the kernel calls it (`mga_g400_emit_state`, #L348-373):
- The order is pipe, then context (which writes TDUALSTAGE0/1 and FCOL, broadcast; #L89-113), then tex0, then tex1.
- tex1 is sent only when `warp_pipe & MGA_T2` (MGA_T2=0x8, K:include/uapi/drm/mga_drm.h#L56).
- `emit_tex0` (#L144-181) follows the same pattern, broadcast and without map1. It writes `WR54/WR62 = tex0 w/h | 0x40` and `WR52/WR60 = 0x40`.

## 2. TDUALSTAGE0/1 field layout

Sources: SPEC p.3-196..3-203 [pdf 236-243]; M:mgaregs.h#L805-897; X:mga_reg.h#L609ff. Both registers use the same layout. Only the meaning of the sources differs between stage 0 and stage 1.

| Bits | Spec name | Meaning |
|---|---|---|
| 1:0 | colorNarg2sel | 00 diffuse, 01 specular, 10 FCOL, 11 previous stage. In **stage 0, "previous stage" is texture 1**; in stage 1 it is the stage-0 output. |
| 4:2 | colorNalphasel | 000 diffuse, 001 FCOL, 010 current texture (s0: tex0, s1: tex1), 011 previous texture (s0: bump luminance L, s1: tex0), 100 previous stage (s0: tex1, s1: stage-0 output) |
| 5 / 7 | colorNarg1alpha / arg2alpha | replicate the argument's alpha into RGB ("A to RGB") |
| 6 / 8 | colorNarg1inv / arg2inv | argument = 1 − argument |
| 9 / 10 | colorNalpha1inv / alpha2inv | ALPHA1 / ALPHA2 = 1 − (alphasel value) |
| 11 | colorNarg1mul | multiplier input 1: 0 = ARG1, 1 = ALPHA1 |
| 12 | colorNarg2mul | multiplier input 2: 0 = ARG2, 1 = ALPHA2 |
| 13 | colorNarg1add | adder input 1: 0 = ARG1, 1 = MULOUT |
| 14 | colorNarg2add | adder input 2: 0 = ARG2, 1 = MULOUT (TMP when blend is set) |
| 16:15 | colorNmodbright | 00 none, 01 multiply result <<1, 10 <<2 |
| 17 / 18 / 19 | colorNadd / add2x / addbias | 0 = SUB, 1 = ADD / "add and double" / "add with −0.5 bias" |
| 20 | colorNblend | linear two-pass blend (see the datapath below) |
| 22:21 | colorNsel | 00 ARG1, 01 ARG2, 10 ADDOUT, 11 MULOUT |
| 23 | alphaNarg1inv | the alpha unit's ARG1 is always the current texel's alpha |
| 25:24 | alphaNarg2sel | 00 diffuse, 01 FCOL, 10 previous texture (**s0: constant 0x00**, s1: tex0), 11 previous stage (s0: tex1, s1: stage-0 output) |
| 26 / 27 | alphaNarg2inv / alphaNadd | invert / 0 = SUB, 1 = ADD |
| 28 / 29 | alphaNaddbias / alphaNadd2x | When alphaNsel=MUL these two bits become alphaNmodbright (01 ×2, 10 ×4). Mesa defines both names on the same bits. |
| 31:30 | alphaNsel | 00 ARG1, 01 ARG2, 10 ADD, 11 MUL |

- The Mesa header names value 2 of TD1 bits 4:2 `TD1_color_alpha_tex0`, but the spec says "current texture (texture1)". Mesa's code uses the `TD0_*` names for both registers anyway.
- The spec leaves the description text for bits 5-10 blank. The meanings above come from Fig. 3-2 and Mesa's names.

**Datapath** (Fig. 3-1 / 3-2, SPEC p.3-199/200 [pdf 239-240]). **[spec]**
- Colour unit:
  - ARG1 = Tn, then optional A→RGB, then optional 1−x.
  - ARG2 = mux(arg2sel), then optional A→RGB, then optional 1−x.
  - A = mux(alphasel) gives ALPHA1 and ALPHA2, each optionally inverted.
  - MUL = (arg1mul ? ALPHA1 : ARG1) × (arg2mul ? ALPHA2 : ARG2), then modbright, at 16:16:16.
  - ADD = in1 ± in2, with add2x and addbias applied.
  - OUT = sel(ARG1, ARG2, ADD, MUL), 8:8:8, "saturate, rup".
- **Blend (bit 20):** "pass 1 uses the programmed arg1mul/arg2mul; the result is kept in TMP. On pass 2, arg1mul/arg2mul are forced to NOT(...). TMP is used instead of the multiplier output."
  - With Mesa's DECAL word: TMP = Cf·(1−As), MUL = Cs·As, OUT = Cs·As + Cf·(1−As).
- Alpha unit: ARG1 = Tn.a (optional inversion), ARG2 = mux (optional inversion). ADD and MUL run in parallel, then sel picks the output. There is no alpha blend mode.
- **[inf]** SUB computes in1 − in2: Mesa's GL_SUBTRACT swaps the order by inverting both inputs (M:mga_texcombine.c#L420-438).
- **[?]** The exact rounding, and the order of add2x vs. addbias.

**Spec restrictions**
- **Single texturing (`dualtex=0`)** (SPEC p.4-45 [pdf 493]): "only one stage … is used". TDUALSTAGE0 must satisfy:
  - `colorblend=0`
  - color0alphasel ∈ {000, 001, 010}
  - (misprinted as "coloralphasel") arg2sel ∈ {00, 01, 10}
  - alpha0arg2sel ∈ {00, 01}
  - **"TDUALSTAGE1 must be programmed with the same values as TDUALSTAGE0."**
- **Rev A only** (same page): "TDUALSTAGE0 and TDUALSTAGE1 must be programmed to '0' to work properly, otherwise unexpected results may occur". There is also a Rev A bump-map workaround: luminance ×4 (color0modbright=10).
- **Dual texturing** (SPEC p.3-203, p.4-50): stage 1 may use diffuse or specular (color1alphasel=000, color1arg2sel=00/01, alpha1arg2sel=00) only when stage-0 colour sel or alpha sel is ADD or MUL.
  - Mesa follows this: M:mga_texcombine.c#L159-163 and #L571.
- Blend needs `dualtex=1`. Mesa: "Linear blending mode needs dual texturing enabled" (M:mga_texstate.c#L666, mga_texcombine.c#L412).

## 3. Mesa G400 TDUALSTAGE values (literal constants)

These come from the tables in M:mga_texstate.c#L270-525, with the hex worked out from mgaregs.h. The unit-0 table goes to TDUALSTAGE0 and the unit-1 table to TDUALSTAGE1. The selection code is at #L625-719. **[code]**

| Env / base format | unit 0 | unit 1 |
|---|---|---|
| REPLACE RGB,L | 0x40000000 | 0x43000000 |
| REPLACE RGBA,LA,I | 0x00000000 | 0x00000000 |
| REPLACE A | 0x00200000 | 0x00200003 |
| **MODULATE RGB,L** | 0x40600000 | **0x43600003** |
| **MODULATE RGBA,LA,I** | 0xC0600000 | **0xC3600003** |
| MODULATE A | 0xC0200000 | 0xC3200003 |
| DECAL RGB | 0x40000000 | 0x43000000 |
| DECAL RGBA (blend) | 0x40526A08 | 0x43526A0B |
| DECAL other (undefined in GL) | 0x40200000 | 0x43200003 |
| ADD RGB,L / RGBA,LA / A / I | 0x40420000 / 0xC0420000 / 0xC0200000 / 0x88420000 | 0x43420003 / 0xC3420003 / 0xC3200003 / 0x8B420003 |

- **DECAL RGBA with only one unit** (#L660-672): the driver also sets TDUALSTAGE1 = **0x43200003** (colour and alpha pass through from the previous stage) and `force_dualtex`, so `TEXCTL2.dualtex=1`.
- **GL_BLEND** (`mgaUpdateTextureEnvBlend`, #L529-623): the unit's own stage computes Cf·(1−Cs).
  - Values: RGB,L **0x40600040**; RGBA,LA 0xC0600040; I 0xC0E00040; A 0xC0200000.
  - **Black environment colour** (RGB=0, and A=0 for I): that one word is the whole setup, and Mesa returns at #L572-574.
  - Otherwise, if both GL units are enabled, Mesa falls back to software. If not, it sets force_dualtex and writes stage 1:
    - Cc=white: 0x43420003 (C1 + Cs)
    - Cc grey with R=G=B=A: **0x43423007**, i.e. C1 + Cs·Ac, with FCOL = env colour.
    - For I with Ac=1: the alpha bits become 0x8B000000 instead of 0x43000000.
  - **Mesa quirk:** it uses `TD0_color_arg2_diffuse` even for unit 1, so it computes Cf·(1−Cs) rather than GL's Cp·(1−Cs). That also breaks the spec's diffuse-in-stage-1 rule.
  - **[inf]** The correct stage-1 GL_BLEND with black Cc is **0x43600043** (RGB) or **0xC3600043** (RGBA). These values are derived, not taken from Mesa.
- **GL_COMBINE** (M:mga_texcombine.c#L44-672), source mapping:

  | GL source | Mapped to |
  |---|---|
  | TEXTURE | ARG1 / currtex |
  | CONSTANT | FCOL (a single FCOL shared by both stages; fallback if the stages need different colours, #L146, #L559) |
  | PRIMARY_COLOR | diffuse |
  | PREVIOUS | diffuse (unit 0) / prevstage (unit 1) |
  | TEXTURE1 used on unit 0 | arg2 = prevstage, commented "G400 specs (TDUALSTAGE0)" (#L136) |
  | TEXTURE0 used on unit 1 | prevtex |

  Scale factors map as MODULATE → modbright, ADD → add2x (no ×4), ADD_SIGNED → addbias, INTERPOLATE → blend.
- **Other state the G400 path sets:**
  - Texture objects start with `texctl = takey_1|tamask_0` (tmodulate=0) and `texctl2 = ckstransdis` (M:mgatex.c#L321-322). The G400 path never sets tmodulate or the decal bits, which only the G200 path uses (#L199-259).
  - `ALPHACTRL.alphasel = fromtex` (00), because the alpha "will have already been modulated" (#L821-826).
  - `dualtex` is set when both units are enabled or force_dualtex is set (#L833-836). `specen` is ORed into both maps' texctl2 (M:mgastate.c#L993-996).
  - If only GL unit 1 is enabled, it is routed to hardware map 0: "hardware TEXTURE1 can ONLY be used when hardware TEXTURE0 is also used" (#L877-889).
  - FCOL is packed as ARGB8888 (M:mgatex.c#L351).

## 4. Single-unit behaviour, dualtex=0, silicon quirks

- **Yes, TDUALSTAGE is programmed with a single texture.**
  - Mesa always writes stage 0. When unit 1 is disabled it copies stage 0 into stage 1 (`tdualstage1 = tdualstage0` unless force_dualtex, M:mga_texstate.c#L736-739), which is what the spec requires.
  - The kernel re-sends both words on every context upload (K:drivers/gpu/drm/mga/mga_state.c#L104-107).
  - With no texturing, DWGCTL goes back to TRAP and the stale values are ignored.
- **`dualtex=0` does not bypass TDUALSTAGE0 on Rev B and later.** The spec says stage 0 is used in single texturing, with the restrictions in §2. **[spec]**
  - On Rev A both words must be 0, which makes colour and alpha plain ARG1 (texel) pass-through. **[spec]**
  - **[inf]** On Rev A the legacy "Lighting Module" does the lighting: TEXCTL.tmodulate bit 29 plus the TEXCTL2 decal bits (Fig. 3-4, SPEC p.3-213).
  - **[?]** Where the legacy module sits relative to the stage combiner.
    - Mesa and X keep the legacy module in pass-through: tmodulate=0 with an opaque texel, so the texel is selected.
    - An emulator should apply TDUALSTAGE after a pass-through legacy stage and log any other legacy configuration seen alongside non-zero TDUALSTAGE.
- **Dual-mode rules** (SPEC p.3-214 [pdf 254], p.4-49/50 [pdf 497-498]):
  - The transparency and lighting control bits of the two maps are ORed (modsel = modsel0 | modsel1, and so on).
  - These fields must match in both passes: tmodulate, bumpmapping, decalblend, dualtex, specen, tablefog.
  - For the **second** alpha test and for blending, ALPHACTRL's alphasel, atref, atmode, astipple, alphamode, srcblendf and dstblendf "take the value of the second pass" (map 1).
  - The first alpha test (right after filtering, `aten`) can differ per map (SPEC p.3-35 [pdf 75]).
- **Leaving the dual-texture WARP pipe**, kernel `mga_g400_emit_pipe` (K:drivers/gpu/drm/mga/mga_state.c#L274-291), with no explanatory comment: **[code]**
  1. `YDST=0`, `FXLEFT=0`, `FXRIGHT=1`
  2. `DWGCTL=0x00FC3076` (texture_trap, atype I, trans=15, bop SRC, arzero, sgnzero)
  3. `LEN|EXEC=1`
  4. `DWGSYNC=0x7000`
  5. `TEXCTL2=MAGIC`, `LEN|EXEC=0`
  6. `TEXCTL2=DUALTEX|MAGIC`, `LEN|EXEC=0`
  7. `TEXCTL2=MAGIC`

  After that it sets up the single-texture WARP pipe.
- Other WARP-only quirks: when both units are on, Mesa flips the cull sign, marked `/* warp bug? */` (M:mgastate.c#L358).
- Mip levels:
  - The G400 "can't handle any < 32 byte mipmaps" (M:mga_texstate.c#L139-148).
  - It supports up to 11 levels, with mapnb split across TEXFILTER bits 31:29 and bit 18 (#L171-178).

## 5. Map-1 texture coordinates: WARP or direct TMR

- **The spec documents a direct, non-WARP path.** SPEC §4.5.5.5 "Texture mapping (dual texturing)", p.4-49 [pdf 497]. **[spec]**
  1. Set `dualtex=1`.
  2. Step 1, `tmap0dis=0`: program all registers, broadcast.
  3. Step 2, `tmap0dis=1`: program only the registers that differ for texture 1: TMR0-8, TEXORG-TEXORG4, TEXWIDTH, TEXHEIGHT, TEXCTL, TEXCTL2, TEXFILTER, TEXTRANS(HIGH), TEXBORDERCOL, ALPHACTRL.
  4. "Start the drawing engine at the end of the second programming step only."
- **TMR registers** (SPEC p.3-230..3-238 [pdf 270-278], p.4-44/45):

  | Reg | Holds | Format |
  |---|---|---|
  | TMR0 / TMR1 | s/wc increment in x / in y | 12.20 |
  | TMR2 / TMR3 | t/wc increment in x / in y | 12.20 |
  | TMR4 / TMR5 | q/wc increment in x / in y | 16.16 |
  | TMR6 / TMR7 | s/wc / t/wc start | 12.20 |
  | TMR8 | q/wc start | 16.16 |

  - TEXWIDTH `tw = log2(w) + 4 − s_frac + q_frac`, `rfw = 8 − log2(w) − (q_frac − 16)`, `twmask = w − 1` (p.3-228). TEXHEIGHT works the same way.
- **The same thing done in plain MMIO:** X.org EXA composite. **[code]**
  - `PrepareSourceTexture(1, …)` (X:mga_exa.c#L397-440) and `mgaComposite` (#L622-695) do:
    1. TMR0-8 for the source, broadcast.
    2. `TEXCTL2 = MAGIC|CKSTRANSDIS|DUALTEX|MAP1`.
    3. TMR0-8 for the mask.
    4. `TEXCTL2` without map1.
    5. `FXBNDRY`, then `YDSTLEN|EXEC`.
  - Its conventions: s and t are normalised with 20 fraction bits, q is 16.16, `tw = log2 w`, `rfw = (8 − log2 w) & 63` (#L398-440, #L455-500).
- **Mesa and the kernel use WARP only.**
  - Mesa never writes TMRs. The vertex format `MGA_A|MGA_S|MGA_F|MGA_T2` selects the T2 microcode (M:mgavb.c#L67, #L401-411).
  - For T2 the kernel sets `WVRTXSZ=0x1E09` and `WACCEPTSEQ=0x1E000000`, versus 0x1807 and 0x18000000 without T2 (K:drivers/gpu/drm/mga/mga_state.c#L262-300).
  - It also sets `WR56 = 0x46480000` (12800.0f), and the WARP size registers WR54/WR62 (map 0) and WR52/WR60 (map 1) as TEXWIDTH/TEXHEIGHT | 0x40.
  - Mesa's WARP `tw` is `log2 w + 11` on G400 and `+ 28` on G200 (M:mga_texstate.c#L180-189).
- **force_dualtex with a single texture:** the vertex format stays TEX0, so the kernel never sends tex1. **[inf]** TMAP1 still holds broadcast copies of map 0, so stage 1's "current texture" is the same texel.
  - This fits the palette-expansion recipe in SPEC p.4-69, which sets dualtex=1, map1=0 and color1sel=ARG1.

## 6. Texture LUT (TW4/TW8)

- **Load bit:** MACCESS (1C04) bit 29, `tlutload`: "When this bit is set to '1' during an ILOAD or BITBLT operation, the destination becomes the texture LUT rather than the frame buffer." (SPEC p.3-153 [pdf 193]; the same text is in G200SPEC; M:mgaregs.h#L514 `0x20000000`.) **[spec]**
- **LUT size:** "256 x 16 bpp LUT" (SPEC p.4-68 [pdf 516]); "256 x 1 x 16 bpp" (G200SPEC §4.5.8, p.4-60).
  - TEXCTL tformat 0 (TW4) and 1 (TW8) "Goes through the LUT".
  - TW4 has 16 palettes, chosen by TEXCTL.palsel <7:4>. **[inf]** The index is palsel·16 + nibble.
- **Load recipe** (both specs):
  - DWGCTL: BITBLT `0x0E0C6098` or ILOAD `0x040C6099`. Both decode to atype=**RSTR**, linear=1, bop=SRC, sgnzero=1, shftzero=1; bltmod is 7 for the blit and 2 (BFCOL) for ILOAD.
  - AR0 = source end, AR3 = source start, `PITCH iy=1024`.
  - `YDSTLEN = (start 0-255) << 16 | count 1-256`.
  - SRCORG (blit only), DSTORG = 0 (YDSTORG on G200), FXBNDRY = 0.
  - `MACCESS pwidth=PW16, tlutload=1`; `OPMODE dmamod=01` for ILOAD.
  - Afterwards, restore PITCH and MACCESS, and send DWGSYNC before the next texture trap.
- **Entry format: not stated in either spec.** **[?]**
  - pwidth=PW16 with dit555=0 means 5:6:5, which points to RGB565. **[inf]**
  - The G400 feature list says "Alpha in Texture Palettes" (SPEC p.1-6) but never explains how alpha is carried.
  - Upstream 86Box `vid_mga.c` stores RGB565 without alpha, and **handles tlutload only when atype=RPL**, not the RSTR that the spec recipe uses. Worth checking in the emulator.
- **G400-only note** (SPEC p.4-69 [pdf 517]): "The texture in TW4 and TW8 **should be expanded** prior to mapping". The spec does this by drawing an intermediate texture trap with:
  - ALPHACTRL: alphamode=01, alphasel=00, aten=0, astipple=0, src ONE, dst ZERO
  - MACCESS: PW16, nodither=1, dit555=0, fogen=0
  - TDUALSTAGE1: color1sel=ARG1, alpha1sel=ARG1, arg1alpha=0, arg1inv=0
  - TEXCTL: tmodulate=0
  - TEXCTL2: dualtex=1, map1=0, decalblend=0, decaldis=0, specen=0
  - TEXFILTER: NRST/NRST, filteralpha=0

  The spec says "should", not "must".
- **No open driver loads the TLUT.** **[code]**
  - Mesa maps CI8 to TW8 (M:mga_texstate.c#L54) but says "paletted_textures currently doesn't work" (M:mga_xmesa.c#L389), and always clears tlutload (M:mgastate.c#L1084).
  - Neither the kernel nor X ever sets tlutload.

## 7. Still uncertain (verify on silicon)

1. What TEXCTL2 bit 15 does. It is written by every Linux/X G400 path, but the spec marks it reserved.
2. Whether a write that toggles map1 is itself broadcast (the "effective on the next access" reading). The X EXA code relies on it.
3. Where the legacy TEXCTL/TEXCTL2 lighting module sits relative to TDUALSTAGE, and what dualtex=0 does on Rev A.
4. Combiner rounding: the order of modbright, add2x and addbias; how "saturate, rup" is applied; the SUB operand order is inferred.
5. The TLUT entry format (RGB565?), how palette alpha works, and whether atype matters during tlutload.
6. Whether stage-1 inputs honour the diffuse/specular restriction in hardware or just produce garbage.
