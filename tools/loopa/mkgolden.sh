#!/usr/bin/env bash
# Build the golden Loop A disk images (inside the dev container):
#   boot.img  FreeDOS 1.4 boot floppy with our FDCONFIG.SYS/AUTOEXEC.BAT
#   c.img     empty, partitioned, FAT16 hard disk (63 spt, 16 heads, CYL cyl)
# Cached in $MGA_CACHE/loopa/golden keyed by this script's contents.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
cache=${MGA_CACHE:-$HOME/.cache/mga-glide}
key=$(cat "$0" "$here"/dos/* | sha256sum | cut -c1-12)
gold=$cache/loopa/golden-$key
mkdir -p "$cache/loopa"
exec 9>"$cache/loopa/.golden.lock"
flock 9
if [ -s "$gold/boot.img" ] && [ -s "$gold/c.img" ]; then echo "$gold"; exit 0; fi

FD_URL=https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/distributions/1.4/FD14-FloppyEdition.zip
FD_SHA=45b1fa7c52dd996c3bfa5e352ffcd410781b952a6ad629f15a4c9ec4bbaefc5a
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
"$root/tools/setup/fetch.sh" "$FD_URL" "$FD_SHA" "$tmp/fd.zip"
unzip -q -o "$tmp/fd.zip" 144m/x86BOOT.img -d "$tmp"
mkdir -p "$gold.part"
cp "$tmp/144m/x86BOOT.img" "$gold.part/boot.img"
b=$gold.part/boot.img
mdel -i "$b" ::/FDAUTO.BAT ::/SETUP.BAT 2>/dev/null || true
mcopy -o -i "$b" "$here/dos/FDCONFIG.SYS" ::/FDCONFIG.SYS
mcopy -o -i "$b" "$here/dos/AUTOEXEC.BAT" ::/AUTOEXEC.BAT

# Hard disk: 63 x 16 x 1000 = 1008000 sectors (492 MB, under the 1024-cylinder
# BIOS limit; room for call traces), sparse, one FAT16 partition.
c=$gold.part/c.img
dd if=/dev/zero of="$c" bs=512 count=0 seek=1008000 status=none
cat > "$tmp/mtoolsrc" <<EOT
drive c: file="$c" partition=1
mtools_skip_check=1
EOT
MTOOLSRC=$tmp/mtoolsrc mpartition -I -B /dev/null c: 2>/dev/null || MTOOLSRC=$tmp/mtoolsrc mpartition -I c:
MTOOLSRC=$tmp/mtoolsrc mpartition -c -t 1000 -h 16 -s 63 -a c:
MTOOLSRC=$tmp/mtoolsrc mformat -t 1000 -h 16 -s 63 -v HX c:
MTOOLSRC=$tmp/mtoolsrc mmd c:/HX c:/OUT
rm -rf "$gold"; mv "$gold.part" "$gold"
echo "$gold"
