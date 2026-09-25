#!/usr/bin/env bash
# mkimage.sh KEY SRC_DIR DOS_DIR -> path of a cached FAT16 hard-disk image
# (63 spt, 16 heads, 406 cylinders, ~200 MB) holding SRC_DIR as D:\DOS_DIR.
# Runs inside the dev container. Keyed by the file list and sizes.
set -euo pipefail
key=$1 src=$2 dosdir=$3
cache=${MGA_CACHE:-$HOME/.cache/mga-glide}/games
mkdir -p "$cache"
h=$( (cd "$src" && find . -type f -printf '%p %s\n' | sort; cat "$0") | sha256sum | cut -c1-12)
img=$cache/$key-$h.img
exec 9>"$cache/.$key.lock"
flock 9
if [ -s "$img" ]; then echo "$img"; exit 0; fi
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
dd if=/dev/zero of="$img.part" bs=512 count=$((63*16*406)) status=none
printf 'drive d: file="%s" partition=1\nmtools_skip_check=1\n' "$img.part" > "$tmp/rc"
export MTOOLSRC=$tmp/rc
mpartition -I d: 2>/dev/null || true
mpartition -c -t 406 -h 16 -s 63 -a d:
mformat -t 406 -h 16 -s 63 -v GAMES d:
mmd "d:/$dosdir"
mcopy -s -Q "$src"/* "d:/$dosdir/"
mv "$img.part" "$img"
echo "$img"
