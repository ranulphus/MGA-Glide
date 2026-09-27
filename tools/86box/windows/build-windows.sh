#!/usr/bin/env bash
# Build MGA-Glide's locally patched 86Box for Windows, in the MSYS2 UCRT64
# shell, from the kit that tools/86box/mkwinkit.sh makes (README.md).
#
#   ./build-windows.sh [--no-deps] [--roms full|minimal] [--out DIR] [--jobs N]
#
#   --no-deps       skip installing the MSYS2 packages
#   --roms minimal  only the ROMs Loop A uses (the Pentium II board and the
#                   Matrox cards) instead of 86Box's whole ROM set (~100 MB)
#   --out DIR       where to put 86Box (default ./86Box-MGA-Glide)
#
# It installs the packages 86Box's own Windows builds use, fetches 86Box at
# the pinned commit, applies the patch series (less the Unix-only monitor
# patch), builds the "development" preset (DEV_BRANCH on: the G100, and our
# G200, G400 and G450, depend on it) with Qt 5 linked statically, installs
# it, fetches the ROM set at its pinned commit, and unpacks Matrox's
# genuine G200/G400/G450 BIOSes from Matrox's public setup257.exe (checked
# against its sha256; never redistribute them).
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
deps=1 roms=full out=$here/86Box-MGA-Glide jobs=$(nproc 2>/dev/null || echo 4)
while [ $# -gt 0 ]; do
  case $1 in
    --no-deps) deps=0 ;;
    --roms) roms=$2; shift ;;
    --out) out=$2; shift ;;
    --jobs) jobs=$2; shift ;;
    -h|--help) sed -n '2,19p' "$0"; exit 0 ;;
    *) echo "build-windows: unknown option $1" >&2; exit 2 ;;
  esac
  shift
done
case $roms in full|minimal) ;; *) echo "build-windows: --roms full or minimal" >&2; exit 2 ;; esac
if [ "${MSYSTEM:-}" != UCRT64 ]; then
  echo "build-windows: run this in the 'MSYS2 UCRT64' shell (MSYSTEM is '${MSYSTEM:-unset}')" >&2
  exit 1
fi
# shellcheck source=/dev/null
. "$here/versions.env"            # BOX86_REPO/COMMIT, ROMS_REPO/COMMIT, MATROX_BIOS_URL/SHA256

step() { printf '\n== %s\n' "$*"; }

if [ $deps = 1 ]; then
  step "MSYS2 packages (as 86Box's Windows CI at this commit)"
  pacman -S --needed --noconfirm git unzip patch \
    mingw-w64-ucrt-x86_64-{ninja,cmake,gcc,pkgconf,freetype,sdl3,zlib,libpng,openal,rtmidi,libslirp,fluidsynth,libserialport,qt5-static,vulkan-headers,llvm-openmp,zstd}
fi

src=$here/src-$BOX86_COMMIT
if [ ! -d "$src/.git" ]; then
  step "86Box $BOX86_COMMIT"
  rm -rf "$src"
  git init -q "$src"
  git -C "$src" remote add origin "$BOX86_REPO"
  git -C "$src" fetch -q --depth 1 origin "$BOX86_COMMIT"
  git -C "$src" checkout -q FETCH_HEAD
fi

step "Patches"
work=$here/work
rm -rf "$work"; mkdir -p "$work"
git -C "$src" archive "$BOX86_COMMIT" | tar -x -C "$work"
grep -v '^#' "$here/series.windows" | while read -r p; do
  [ -n "$p" ] || continue
  echo "applying $p"
  (cd "$work" && patch -p1 --no-backup-if-mismatch -s < "$here/patches/$p")
done

step "Configure and build (this takes a while)"
cmake -S "$work" -B "$work/build" --preset development -D NEW_DYNAREC=OFF -D QT=ON \
      -D CMAKE_INSTALL_PREFIX="$work/artifacts" > "$work/cmake.log" || { tail -40 "$work/cmake.log"; exit 1; }
cmake --build "$work/build" -j "$jobs" > "$work/build.log" 2>&1 || { tail -60 "$work/build.log"; exit 1; }
cmake --install "$work/build" > "$work/install.log"
exe=$(find "$work/artifacts" -iname 86Box.exe | head -1)
[ -n "$exe" ] || { echo "build-windows: no 86Box.exe under $work/artifacts" >&2; exit 1; }
mkdir -p "$out"
cp -r "$(dirname "$exe")"/. "$out"/

step "ROMs ($roms) at $ROMS_COMMIT"
r=$out/roms
if [ "$(cat "$r/.commit" 2>/dev/null)" != "$ROMS_COMMIT-$roms" ]; then
  rm -rf "$r"
  git init -q "$r"
  git -C "$r" remote add origin "$ROMS_REPO"
  if [ $roms = minimal ]; then
    git -C "$r" config core.sparseCheckout true
    printf '%s\n' /machines/bf6/ /video/matrox/ > "$r/.git/info/sparse-checkout"
  fi
  git -C "$r" fetch -q --depth 1 --filter=blob:none origin "$ROMS_COMMIT"
  git -C "$r" checkout -q FETCH_HEAD
  rm -rf "$r/.git"
  echo "$ROMS_COMMIT-$roms" > "$r/.commit"
fi

step "Matrox BIOSes (G200 900-33, G400 897-21, G450 935-20)"
m=$r/video/matrox/mgaglide
if [ ! -s "$m/900-33.bin" ] || [ ! -s "$m/897-21.bin" ] || [ ! -s "$m/935-20.bin" ]; then
  mkdir -p "$m"
  curl -fsSL --retry 3 -o "$m/setup257.exe" "$MATROX_BIOS_URL"
  echo "$MATROX_BIOS_SHA256  $m/setup257.exe" | sha256sum -c --quiet - \
    || { echo "build-windows: setup257.exe does not match its pinned sha256" >&2; rm -f "$m/setup257.exe"; exit 1; }
  unzip -o -q "$m/setup257.exe" 900-33.bin 897-21.bin 935-20.bin -d "$m"
  rm -f "$m/setup257.exe"
fi
for f in machines/bf6/Beh_70.bin video/matrox/productiva8mbsdr.BIN video/matrox/mgaglide/900-33.bin \
         video/matrox/mgaglide/897-21.bin video/matrox/mgaglide/935-20.bin; do
  [ -s "$r/$f" ] || { echo "build-windows: missing ROM $f" >&2; exit 1; }
done
cp "$here/README.md" "$out/README-MGA-Glide.md"
rm -rf "$work"

step "Done"
echo "86Box: $(cygpath -w "$out")\\86Box.exe"
echo "Open a VM folder with it (86Box.exe -P <folder>), or create a machine in its manager."
