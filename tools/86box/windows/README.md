# MGA-Glide's 86Box for Windows

This kit builds the 86Box that MGA-Glide, DOS-GL and DOSBench test with:
86Box at a pinned commit plus MGA-Glide's local patches. The patches fix the
Matrox G100's 3D engine and add three cards that stock 86Box doesn't have:

- Matrox Millennium G200 (MGA-Glide emulation)
- Matrox Millennium G400 (MGA-Glide emulation)
- Matrox Millennium G450 (MGA-Glide emulation)

They boot Matrox's own video BIOSes (G200 900-33, G400 897-21, G450 935-20),
which the build script unpacks from Matrox's public `setup257.exe`. Keep
those BIOS files to yourself: they are Matrox's.

The patches are local to MGA-Glide and not part of upstream 86Box, so treat
this as a test build: the emulated cards stand in for the real ones while
the real ones are being characterised.

## Building

1. Install MSYS2 from https://www.msys2.org/ (the default `C:\msys64` is
   fine), open **MSYS2 UCRT64** from the Start menu, and update it:
   `pacman -Syu` (it may ask you to close the window and run it again).
2. Unzip this kit somewhere without spaces in the path, for example
   `C:\mgaglide\86box-mgaglide-windows-kit`, and in the UCRT64 shell:

   ```
   cd /c/mgaglide/86box-mgaglide-windows-kit
   ./build-windows.sh
   ```

   It installs the packages 86Box's own Windows builds use (compiler,
   CMake, Ninja, a static Qt 5, SDL3, OpenAL, FluidSynth and so on), fetches
   86Box and its ROM set at their pinned commits, applies the patches,
   builds, and collects everything in `86Box-MGA-Glide\`. The first run
   downloads roughly 1.5 GB of packages and takes a while to compile.

   Options: `--no-deps` (packages already installed), `--roms minimal` (only
   the Pentium II board and the Matrox cards instead of the whole ~100 MB ROM
   set), `--out DIR`, `--jobs N`.

`86Box-MGA-Glide\86Box.exe` then runs from Explorer like any 86Box: the DLLs
it needs are next to it, and the ROMs are in its `roms\` folder.

## Using it

- **The test machine** (`dosbench-g450-vm.zip`, if you were given it):
  unzip it, then `86Box.exe -P C:\path\to\dosbench-g450`, or add the folder
  in the 86Box manager. It is Loop A's machine: an ABIT BF6 (440BX) board,
  a Pentium II 350, 64 MB, the emulated G450 plus a Voodoo Graphics, and a
  Sound Blaster 16, booting FreeDOS. Its `README.txt` says what is on C:.
- **Your own machines**: in Settings > Display choose one of the Matrox
  Millennium G200/G400/G450 (MGA-Glide emulation) cards (or the Productiva
  G100), and tick Voodoo Graphics to have a 3dfx card alongside. The
  G400/G450 want 16 MB of video memory. Use a Pentium II class board (the
  bf6 is the tested one); the cards are AGP and PCI.
- **Environment variable**: `MGAGLIDE_G200_BIOS=g100` makes the emulated
  G200 boot the G100's BIOS instead of its own (the old fallback).

## What differs from the Linux build

Patch 0101 (a `key` command for the Unix SDL monitor console, which the test
harness types into games with) is left out: Windows builds use the Qt user
interface, which has no such console. Everything else is the same series, and
the test harness itself (Loop A, the bench tools) stays on Linux.

If the build fails, the logs are in `work\cmake.log` and `work\build.log`
(the script prints the tail). The kit's version is in `KIT-VERSION`.
