#!/usr/bin/env bash
# Install the pinned DJGPP cross compiler into $DJGPP_PREFIX (HAL builds only).
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
: "${DJGPP_PREFIX:?}" "${DJGPP_URL:?}" "${DJGPP_SHA256:?}"
if [ -x "$DJGPP_PREFIX/bin/i586-pc-msdosdjgpp-gcc" ]; then
  echo "setup-djgpp: already installed"
else
  tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
  "$here/fetch.sh" "$DJGPP_URL" "$DJGPP_SHA256" "$tmp/djgpp.tar.bz2"
  mkdir -p "$tmp/x"; tar -xjf "$tmp/djgpp.tar.bz2" -C "$tmp/x"
  src=$(find "$tmp/x" -maxdepth 2 -type d -name djgpp | head -1); [ -n "$src" ] || src=$tmp/x
  rm -rf "$DJGPP_PREFIX"; mkdir -p "$(dirname "$DJGPP_PREFIX")"; mv "$src" "$DJGPP_PREFIX"
  echo "setup-djgpp: installed into $DJGPP_PREFIX"
fi

# Some DJGPP binutils link against libfl.so.2 (flex). If the host lacks it,
# unpack the Ubuntu package beside the toolchain (no root needed).
if ! LD_LIBRARY_PATH="$DJGPP_PREFIX/hostlib" "$DJGPP_PREFIX/bin/i586-pc-msdosdjgpp-ar" --version >/dev/null 2>&1; then
  tmp2=$(mktemp -d)
  (cd "$tmp2" && apt-get download libfl2 >/dev/null 2>&1) || { echo "setup-djgpp: install libfl2" >&2; exit 1; }
  dpkg-deb -x "$tmp2"/libfl2_*.deb "$tmp2/x"
  mkdir -p "$DJGPP_PREFIX/hostlib"
  cp -a "$tmp2"/x/usr/lib/*/libfl.so.2* "$DJGPP_PREFIX/hostlib/"
  rm -rf "$tmp2"
  echo "setup-djgpp: bundled libfl.so.2 in $DJGPP_PREFIX/hostlib"
fi

# CWSDPMI, so DJGPP programs can run in Loop A.
if [ -n "${CWSDPMI_URL:-}" ] && [ ! -f "$DJGPP_PREFIX/dos/CWSDPMI.EXE" ]; then
  tmp3=$(mktemp -d)
  "$here/fetch.sh" "$CWSDPMI_URL" "$CWSDPMI_SHA256" "$tmp3/csdpmi.zip"
  mkdir -p "$DJGPP_PREFIX/dos"
  unzip -q -j -o "$tmp3/csdpmi.zip" bin/CWSDPMI.EXE -d "$DJGPP_PREFIX/dos"
  rm -rf "$tmp3"
  echo "setup-djgpp: CWSDPMI in $DJGPP_PREFIX/dos"
fi
