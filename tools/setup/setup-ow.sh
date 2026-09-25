#!/usr/bin/env bash
# Install the pinned Open Watcom v2 snapshot into $WATCOM.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
: "${WATCOM:?WATCOM not set}" "${OW_URL:?}" "${OW_SHA256:?}"
if [ -x "$WATCOM/binl64/wcc386" ]; then echo "setup-ow: $WATCOM already installed"; exit 0; fi
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
"$here/fetch.sh" "$OW_URL" "$OW_SHA256" "$tmp/ow.tar.xz"
mkdir -p "$WATCOM.part"
tar -xJf "$tmp/ow.tar.xz" -C "$WATCOM.part"
rm -rf "$WATCOM"; mv "$WATCOM.part" "$WATCOM"
echo "setup-ow: installed into $WATCOM"
