#!/usr/bin/env bash
# Export the shared Matrox HAL, its test harness and the local 86Box
# patches as a standalone tree for their own repository (PRD M7.3, D16):
#
#   tools/hal-export.sh [DEST] [--git]
#
# DEST defaults to build/hal-export. With --git the tree becomes a git
# repository with one commit. The exported tree builds on its own:
#   make hal-djgpp hal-host tests-host smoke-djgpp   (and hal-ow with Watcom)
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
dest=${1:-$root/build/hal-export}
git_init=0
for a in "$@"; do [ "$a" = --git ] && git_init=1; done
[ "$dest" = --git ] && dest=$root/build/hal-export
rm -rf "$dest"
mkdir -p "$dest"
cd "$root"
copy() { mkdir -p "$dest/$(dirname "$1")"; cp -a "$1" "$dest/$1"; }

# The HAL itself.
for f in $(git ls-files hal); do copy "$f"; done
# Host reference rasteriser and the setup unit test.
for f in tests/unit/unit.c tests/unit/unit.h tests/unit/refrast.c tests/unit/refrast.h tests/unit/test_trap.c; do copy "$f"; done
# Smoke and bring-up programs, the guest shim and DOS helpers.
for f in tests/hal/smoke.c tests/hal/probe.c tests/hal/romdump.c tests/shim/hx.c tests/shim/hx.h tests/shim/hello.c tests/shim/stackpg.c tests/shim/mousetst.c tests/shim/joytest.c tests/shim/sbbeep.c $(git ls-files tools/dos); do copy "$f"; done
# Loop C rig (Linux port is part of hal/).
for f in tests/rig/mgarig.c $(git ls-files tools/rig); do copy "$f"; done
# Loop A harness, 86Box build and patches, setup scripts, dev container.
for f in $(git ls-files tools/loopa tools/86box tools/setup tools/docker tools/bench) tools/dev tools/imgcmp.py \
         tools/games/mkimage.sh; do copy "$f"; done      # mkimage.sh: run.py --game builds D: with it
# Documents that describe the shared parts.
for f in docs/loops.md docs/emulated-g200.md docs/emulated-g400.md docs/reference-gaps.md docs/bench.md docs/loop-c-results.md LICENSE; do copy "$f"; done

version=$(git describe --always --dirty 2>/dev/null || echo unknown)
echo "$version" > "$dest/VERSION"

cat > "$dest/Makefile" <<'MK'
# Matrox HAL: shared by MGA-Glide (Open Watcom, DOS/4GW) and DOS-GL (DJGPP).
include tools/setup/versions.mk
DJGPP_PREFIX ?= $(HOME)/.local/opt/djgpp-gcc1220
WATCOM ?= $(HOME)/.local/opt/watcom-20260901
export WATCOM DJGPP_PREFIX DJGPP_URL DJGPP_SHA256 CWSDPMI_URL CWSDPMI_SHA256 OW_URL OW_SHA256
Q ?= @

HAL_COMMON := hal/src/debug/serial.c hal/src/debug/regtrace.c hal/src/pci.c hal/src/chip.c hal/src/vbe.c hal/src/fifo.c hal/src/engine.c hal/src/dac.c hal/src/texhw.c hal/src/present.c hal/src/setup/trap.c

DJENV := env LD_LIBRARY_PATH=$(DJGPP_PREFIX)/hostlib
DJCC := $(DJENV) $(DJGPP_PREFIX)/bin/i586-pc-msdosdjgpp-gcc
DJAR := $(DJENV) $(DJGPP_PREFIX)/bin/i586-pc-msdosdjgpp-ar
DJ_CFLAGS := -std=gnu99 -O2 -march=i586 -Wall -Wextra -Werror -Ihal/include -DMGA_DJGPP=1
HOST_CFLAGS := -std=gnu99 -O1 -g -Wall -Wextra -Werror -Ihal/include -DMGA_HOST=1 -fno-strict-aliasing
OWBIN := $(WATCOM)/binl64
OWENV := env WATCOM=$(WATCOM) PATH=$(OWBIN):$(PATH) INCLUDE=$(WATCOM)/h
WCC := $(OWENV) $(OWBIN)/wcc386
OW_CFLAGS := -bt=dos -mf -3s -fp5 -fpi87 -zri -ei -j -zastd=c99 -zq -we -wx -i=hal/include -dMGA_OW=1 -oxt

.PHONY: all hal-djgpp hal-host hal-ow tests-host smoke-djgpp dostests-djgpp dostools setup-djgpp setup-ow 86box clean
all: hal-djgpp hal-host tests-host

build/djgpp/%.o: %.c
	@mkdir -p $(dir $@)
	$(Q)$(DJCC) $(DJ_CFLAGS) -c -o $@ $<
build/djgpp/libmgahal.a: $(HAL_COMMON:%.c=build/djgpp/%.o) build/djgpp/hal/port/djgpp.o
	$(Q)$(DJAR) rcs $@ $^
hal-djgpp: build/djgpp/libmgahal.a

build/djgpp/SMOKE.EXE: tests/hal/smoke.c build/djgpp/libmgahal.a
	$(Q)$(DJCC) $(DJ_CFLAGS) -o $@ $^
smoke-djgpp: build/djgpp/SMOKE.EXE

# Guest shim programs with DJGPP (Loop A and bench jobs run them with CWSDPMI).
build/djgpp/HELLO.EXE: tests/shim/hello.c tests/shim/hx.c build/djgpp/libmgahal.a
	$(Q)$(DJCC) $(DJ_CFLAGS) -Itests/shim -DHX_BUILD_ID='"mgahal"' -o $@ $^
build/djgpp/PROBE.EXE: tests/hal/probe.c tests/shim/hx.c build/djgpp/libmgahal.a
	$(Q)$(DJCC) $(DJ_CFLAGS) -Itests/shim -DHX_BUILD_ID='"mgahal"' -o $@ $^
# The emulator's DPMI stack check (docs/loops.md): must PASS in Loop A.
build/djgpp/STACKPG.EXE: tests/shim/stackpg.c tests/shim/hx.c build/djgpp/libmgahal.a
	$(Q)$(DJCC) $(DJ_CFLAGS) -Itests/shim -DHX_BUILD_ID='"mgahal"' -o $@ $^
# The --mouse check (docs/loops.md): must PASS in Loop A with --mouse ps2.
build/djgpp/MOUSETST.EXE: tests/shim/mousetst.c tests/shim/hx.c build/djgpp/libmgahal.a
	$(Q)$(DJCC) $(DJ_CFLAGS) -Itests/shim -DHX_BUILD_ID='"mgahal"' -o $@ $^
# The --keys joy check (JOYTEST, local patch 0105) and the --sound/--wav check (SBBEEP).
build/djgpp/JOYTEST.EXE: tests/shim/joytest.c tests/shim/hx.c build/djgpp/libmgahal.a
	$(Q)$(DJCC) $(DJ_CFLAGS) -Itests/shim -DHX_BUILD_ID='"mgahal"' -o $@ $^
build/djgpp/SBBEEP.EXE: tests/shim/sbbeep.c tests/shim/hx.c build/djgpp/libmgahal.a
	$(Q)$(DJCC) $(DJ_CFLAGS) -Itests/shim -DHX_BUILD_ID='"mgahal"' -o $@ $^ -lm
dostests-djgpp: build/djgpp/HELLO.EXE build/djgpp/PROBE.EXE build/djgpp/STACKPG.EXE build/djgpp/MOUSETST.EXE \
                build/djgpp/JOYTEST.EXE build/djgpp/SBBEEP.EXE

build/host/%.o: %.c
	@mkdir -p $(dir $@)
	$(Q)$(CC) $(HOST_CFLAGS) -c -o $@ $<
build/host/libmgahal.a: $(HAL_COMMON:%.c=build/host/%.o) build/host/hal/port/host.o
	$(Q)ar rcs $@ $^
hal-host: build/host/libmgahal.a

build/host/test_trap: tests/unit/test_trap.c tests/unit/unit.c tests/unit/refrast.c hal/src/setup/trap.c
	@mkdir -p $(dir $@)
	$(Q)$(CC) $(HOST_CFLAGS) -o $@ $^ -lm
tests-host: build/host/test_trap
	build/host/test_trap

build/ow/%.obj: %.c
	@mkdir -p $(dir $@)
	$(Q)$(WCC) $(OW_CFLAGS) -fo=$@ $<
hal-ow: $(HAL_COMMON:%.c=build/ow/%.obj) build/ow/hal/port/ow_dos4g.obj

# 16-bit DOS helpers for Loop A and the bench (SERSAY, UTEXIT, WAITSEC, REBOOT,
# VMODE, KEYWAIT, VECCHK, SBCHK), built with Open Watcom's 16-bit compiler (make setup-ow).
DOS_TOOLS := UTEXIT SERSAY WAITSEC REBOOT VMODE KEYWAIT VECCHK SBCHK
build/ow/dos/%.COM:
	@mkdir -p build/ow/dos/obj16
	$(Q)$(OWENV) $(OWBIN)/wcc -bt=dos -ms -0 -os -zq -we -fo=build/ow/dos/obj16/$*.obj $<
	$(Q)$(OWENV) $(OWBIN)/wlink option quiet system com name $@ file build/ow/dos/obj16/$*.obj
build/ow/dos/UTEXIT.COM: tools/dos/utexit.c
build/ow/dos/SERSAY.COM: tools/dos/sersay.c
build/ow/dos/WAITSEC.COM: tools/dos/waitsec.c
build/ow/dos/REBOOT.COM: tools/dos/reboot.c
build/ow/dos/VMODE.COM: tools/dos/vmode.c
build/ow/dos/KEYWAIT.COM: tools/dos/keywait.c
build/ow/dos/VECCHK.COM: tools/dos/vecchk.c
build/ow/dos/SBCHK.COM: tools/dos/sbchk.c
dostools: $(DOS_TOOLS:%=build/ow/dos/%.COM)

setup-djgpp:
	tools/setup/setup-djgpp.sh
setup-ow:
	tools/setup/setup-ow.sh
86box:
	tools/dev tools/86box/build.sh
clean:
	rm -rf build
MK

cat > "$dest/README.md" <<EOF2
# Matrox HAL and test harness

Exported from MGA-Glide $version by \`tools/hal-export.sh\`. Shared by
MGA-Glide (Open Watcom, DOS/4GW) and DOS-GL (DJGPP):

| Path | Contents |
|---|---|
| \`hal/\` | PCI, capabilities, VBE, FIFO pacing, engine, DAC, trapezoid setup; ports for DOS/4GW, DJGPP and the host |
| \`tests/unit/\` | host reference rasteriser and the setup test |
| \`tests/hal/\` | \`smoke.c\` (any toolchain), \`probe.c\`, \`romdump.c\` |
| \`tools/loopa/\`, \`tools/86box/\` | Loop A harness and the pinned 86Box with the local patches (never upstreamed): emulated G200, G400 and G450 on Matrox's own BIOSes; \`tools/games/mkimage.sh\` builds the game disk for \`run.py --game\` (the caller supplies the games list with \`--games-file\`) |
| \`tools/bench/\` | Loop B: bench job runner, upload sink, capture helper, the 86Box virtual bench PC |
| \`tests/rig/\`, \`tools/rig/\` | Loop C rig for a Matrox card under Linux (\`hal/port/linux.c\`) |

\`\`\`
make setup-djgpp && make hal-djgpp smoke-djgpp
make hal-host tests-host
\`\`\`

See \`docs/loops.md\`, \`docs/bench.md\`, \`docs/emulated-g200.md\` and \`docs/emulated-g400.md\`.
\`MANIFEST\` lists every file's sha256 so a vendored copy can be checked for local edits.
EOF2

(cd "$dest" && find . -type f ! -name MANIFEST ! -path './.git/*' | LC_ALL=C sort | xargs sha256sum > MANIFEST)

if [ "$git_init" = 1 ]; then
  (cd "$dest" && git init -q && git add -A && git -c user.name="$(git -C "$root" config user.name)" \
     -c user.email="$(git -C "$root" config user.email)" commit -q -m "Matrox HAL $version (exported from MGA-Glide)")
fi
echo "hal-export: $dest ($version)"
