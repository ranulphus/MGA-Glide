#!/usr/bin/env bash
# fetch.sh URL SHA256 DEST — download once into the cache, verify, copy to DEST.
# SHA256 "auto" records the hash on first download in tools/setup/hashes.txt.
set -euo pipefail
url=$1 sha=$2 dest=$3
cache=${MGA_CACHE:-$HOME/.cache/mga-glide}/dl
mkdir -p "$cache" "$(dirname "$dest")"
f=$cache/$(basename "$url")
if [ ! -s "$f" ]; then
  echo "fetch: $url" >&2
  curl -fL --retry 3 -o "$f.part" "$url"
  mv "$f.part" "$f"
fi
got=$(sha256sum "$f" | cut -d' ' -f1)
hashes=$(dirname "$0")/hashes.txt
if [ "$sha" = auto ]; then
  known=$(awk -v u="$url" '$2==u {print $1}' "$hashes" 2>/dev/null || true)
  if [ -z "$known" ]; then echo "$got $url" >> "$hashes"; known=$got; fi
  sha=$known
fi
if [ "$got" != "$sha" ]; then
  echo "fetch: sha256 mismatch for $url: got $got want $sha" >&2; exit 1
fi
[ "$f" = "$dest" ] || cp -f "$f" "$dest"
