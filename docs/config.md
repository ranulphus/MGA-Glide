# Configuration

MGA-Glide reads options from `MGAGLIDE.CFG` in the game's directory and then
from the `MGAGLIDE` environment variable (which wins), as `key=value` pairs
separated by spaces or new lines:

```
SET MGAGLIDE=res=1024x768 bpp=32 trilinear=1
```

Everything defaults to fidelity: with no options set, MGA-Glide aims to
draw what a Voodoo Graphics board would.

## Enhancements (off by default)

| Option | Values | Effect |
|---|---|---|
| `res` | `WxH`, e.g. `1024x768` | Render at W x H while the game keeps its own resolution. Textures use finer mip levels; LFB access is scaled |
| `bpp` | `16` (default), `32` | Render in 32-bit colour (no dithering); falls back to 16 when no 32-bit mode fits |
| `hwmip` | `0`, `1` | G200 and later: let the chip pick the mip level per pixel (it rounds where the Voodoo floors, so textures are a little softer; no 8-row bands) |
| `trilinear` | `0`, `1` | G200 and later: blend between mip levels (implies `hwmip`) |
| `bilinear` | `0`, `1` | Filter every texture bilinearly |
| `z32` | `-1` auto (default), `0`, `1` | 32-bit depth buffer. Auto uses it when VRAM allows (it makes W-buffering more precise than the Voodoo's) |

## What the game is told

| Option | Default | Effect |
|---|---|---|
| `version` | `2.43` | Version string from `grGlideGetVersion` |
| `fb_mb`, `tmu_mb`, `tmus` | `2`, `2`, `1` | Reported frame-buffer and texture memory, TMU count |
| `voodoo2` | `0` | Report a Voodoo 2 (also enables iterated-Z fog) |

## Behaviour on the G100

| Option | Default | Effect |
|---|---|---|
| `g100_additive` | `0` | Additive blending, which the G100 cannot do: `0` draws it as 50% stipple, `1` skips it |
| `gamma` | `1` | `0` keeps a linear display ramp whatever the game asks |
| `strict` | `0` | `1` skips every approximated draw state (to find them; see `docs/combine-coverage.md`) |
| `hooks` | `1` | Fault and exit hooks that put the display back in text mode |

## Diagnostics

| Option | Effect |
|---|---|
| `log` | Log level 0 (errors) to 4 (trace); lines go to COM1 |
| `stats=N` | `MGL-STAT` line every N frames |
| `census=1` | Log each distinct draw state once (`MGL-CENSUS`) |
| `exit_after=N` | End the run after N frames (the harness uses it) |
| `snap=a,b,...` | Mark frames for the harness to capture (`MGL-SNAP`) |
| `trace=1`, `trace_path`, `trace_from`, `trace_to` | Record a call trace (`docs/trace.md`) |
| `retail=PATH` | GLTRACE.OVL only: the retail runtime to forward to |
