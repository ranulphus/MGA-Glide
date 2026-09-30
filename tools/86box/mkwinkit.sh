#!/usr/bin/env bash
# Package the Windows build kit for MGA-Glide's 86Box (tools/86box/windows/):
#   dist/86box-mgaglide-windows-kit.zip
# holding build-windows.sh, README.md, versions.env (the pins from
# tools/setup/versions.mk), series.windows and the patches it names. The
# series is the Linux one less the Unix-only SDL monitor patches (0101, 0104), which
# the Windows (Qt) front end does not have. No ROMs or BIOSes are included:
# build-windows.sh fetches them on the Windows machine.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
out=${1:-$root/dist/86box-mgaglide-windows-kit.zip}
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
kit=$tmp/86box-mgaglide-windows-kit
mkdir -p "$kit/patches/local"
cp "$here/windows/build-windows.sh" "$here/windows/README.md" "$kit/"
sed -n 's/^\(BOX86_[A-Z]*\|ROMS_[A-Z]*\|MATROX_BIOS_[A-Z0-9]*\) *:= *\(.*\)$/\1=\2/p' \
    "$root/tools/setup/versions.mk" > "$kit/versions.env"
{
  echo "# The Windows patch series: tools/86box/series less the Unix-only SDL monitor patches."
  grep -v '^#' "$here/series" | grep -v 'sdl-monitor' | grep .
} > "$kit/series.windows"
grep -v '^#' "$kit/series.windows" | while read -r p; do cp "$here/patches/$p" "$kit/patches/$p"; done
echo "MGA-Glide $(git -C "$root" describe --always --dirty)" > "$kit/KIT-VERSION"
# Windows line endings for the README only; the script runs in bash.
sed -i 's/$/\r/' "$kit/README.md"
mkdir -p "$(dirname "$out")"
rm -f "$out"
(cd "$tmp" && zip -q -r "$out" "$(basename "$kit")")
echo "mkwinkit: $out ($(du -h "$out" | cut -f1))"
