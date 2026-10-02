#!/usr/bin/env bash
# mkgolden-variant.sh VARIANT: a Loop A golden image set derived from the
# default one (mkgolden.sh, left untouched so its key and images never change
# for this). The variant's files in dos-VARIANT/ replace the boot floppy's.
#   himemx          FDCONFIG.SYS loading HIMEMX.EXE (XMS 3.0) with DOS=HIGH
#   glosshell       GLOS as the shell: SHELL=C:\TEST\GLOS.EXE /SHELL ... (the
#                   job puts GLOS.EXE there with --file)
#   glosshell-himemx  the same with HIMEMX.EXE and DOS=HIGH
# Cached in $MGA_CACHE/loopa/golden-VARIANT-KEY; prints that directory.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
variant=$1
[ -d "$here/dos-$variant" ] || { echo "mkgolden-variant: no $here/dos-$variant" >&2; exit 1; }
cache=${MGA_CACHE:-$HOME/.cache/mga-glide}
eval "$(sed -n 's/^\(HIMEMX_[A-Z0-9]*\) *:= *\(.*\)$/\1=\2/p' "$root/tools/setup/versions.mk")"
base=$("$here/mkgolden.sh" | tail -1)
key=$( { basename "$base"; cat "$0" "$here/dos-$variant"/*; echo "$HIMEMX_SHA256"; } | sha256sum | cut -c1-12)
gold=$cache/loopa/golden-$variant-$key
exec 9>"$cache/loopa/.golden.lock"
flock 9
if [ -s "$gold/boot.img" ] && [ -s "$gold/c.img" ]; then echo "$gold"; exit 0; fi
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
mkdir -p "$gold.part"
cp "$base/boot.img" "$gold.part/boot.img"
cp --sparse=always "$base/c.img" "$gold.part/c.img"
b=$gold.part/boot.img
for f in "$here/dos-$variant"/*; do
  mcopy -o -i "$b" "$f" "::/$(basename "$f")"
done
case $variant in
  himemx|*-himemx)
    "$root/tools/setup/fetch.sh" "$HIMEMX_URL" "$HIMEMX_SHA256" "$tmp/himemx.zip"
    unzip -q -o -j "$tmp/himemx.zip" BIN/HimemX.exe -d "$tmp"
    mcopy -o -i "$b" "$tmp/HimemX.exe" ::/HIMEMX.EXE ;;
esac
rm -rf "$gold"; mv "$gold.part" "$gold"
echo "$gold"
