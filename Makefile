# MGA-Glide top-level build. `make help` lists the targets.
include config.mk
-include config.local.mk
include tools/setup/versions.mk
include mk/ow.mk
include mk/host.mk
include mk/djgpp.mk
include mk/dos.mk

export WATCOM DJGPP_PREFIX MGA_CACHE OW_URL OW_SHA256 DJGPP_URL DJGPP_SHA256 CWSDPMI_URL CWSDPMI_SHA256

.DEFAULT_GOAL := runtime
.PHONY: help runtime header check-header check-exports check-clib tests-host \
        setup-ow setup-djgpp hal-ow hal-djgpp hal-host clean

# ---- Generated glue ------------------------------------------------------
API_SRCS := abi/glide2x.api tools/abi/api.py tools/abi/gen_exports.py tools/abi/gen_trace.py
build/gen/stamp: $(API_SRCS)
	@mkdir -p build/gen
	$(PYTHON) tools/abi/gen_exports.py . build/gen
	$(PYTHON) tools/abi/gen_trace.py build/gen
	@touch $@

header: include/glide/glide2.h
include/glide/glide2.h: abi/constants.tsv abi/types.h.frag abi/glide2x.api tools/abi/gen_header.py
	$(PYTHON) tools/abi/gen_header.py . $@

check-header:
	@$(PYTHON) tools/abi/gen_header.py . build/glide2.h.check && cmp -s build/glide2.h.check include/glide/glide2.h \
	  || { echo "include/glide/glide2.h is stale: run make header"; exit 1; }

# ---- HAL -----------------------------------------------------------------
HAL_COMMON := hal/src/debug/serial.c hal/src/pci.c hal/src/chip.c hal/src/vbe.c hal/src/fifo.c hal/src/engine.c hal/src/dac.c hal/src/setup/trap.c
HAL_OW     := $(HAL_COMMON) hal/port/ow_dos4g.c
HAL_DJGPP  := $(HAL_COMMON) hal/port/djgpp.c
HAL_HOST   := $(HAL_COMMON) hal/port/host.c

build/ow/mgahal.lib: $(HAL_OW:%.c=build/ow/%.obj)
	@rm -f $@
	$(Q)echo "  WLIB    $@"
	$(Q)$(WLIB) -q -b -n $@ $(addprefix +,$^)
build/ow/mgahal_exe.lib: $(HAL_OW:%.c=build/ow/exe/%.obj)
	@rm -f $@
	$(Q)echo "  WLIB    $@"
	$(Q)$(WLIB) -q -b -n $@ $(addprefix +,$^)
hal-ow: build/ow/mgahal.lib build/ow/mgahal_exe.lib

build/djgpp/libmgahal.a: $(HAL_DJGPP:%.c=build/djgpp/%.o)
	@rm -f $@
	$(DJAR) rcs $@ $^
hal-djgpp: build/djgpp/libmgahal.a

build/host/libmgahal.a: $(HAL_HOST:%.c=build/host/%.o)
	@rm -f $@
	ar rcs $@ $^
hal-host: build/host/libmgahal.a

build/gen/stubs.c build/gen/api_names.c build/gen/api_ids.h build/gen/api_names.h build/gen/thunks.c build/gen/replay_gen.c build/gen/proxy_thunks.c build/gen/glapi_names.c build/gen/glapi_static.c: build/gen/stamp

# ---- Runtime DLL ---------------------------------------------------------
RT_SRCS := $(wildcard src/rt/*.c src/dll/*.c src/glide/*.c src/lfb/*.c src/tex/*.c src/combine/*.c src/trace/*.c)
RT_OBJS := $(RT_SRCS:%.c=build/ow/%.obj) build/ow/gen/stubs.obj build/ow/gen/api_names.obj build/ow/gen/thunks.obj

build/ow/glide2x.lnk: mk/glide2x.lnk.in Makefile
	@mkdir -p $(dir $@)
	$(Q)sed -e 's|@WATCOM@|$(WATCOM)|g' -e 's|@VER@|$(shell git describe --always --dirty 2>/dev/null)|' $< > $@

build/ow/GLIDE2X.OVL: $(RT_OBJS) build/ow/mgahal.lib build/ow/glide2x.lnk
	$(Q)echo "  WLINK   $@"
	$(Q)$(WLINK) @build/ow/glide2x.lnk name $@ option map=build/ow/GLIDE2X.map \
	  $(addprefix file ,$(RT_OBJS)) library build/ow/mgahal.lib
	$(Q)$(PYTHON) tools/abi/lefix.py $@ >/dev/null
runtime: build/ow/GLIDE2X.OVL

# GLTRACE.OVL: the recording proxy in front of a retail runtime (docs/trace.md).
PX_OBJS := build/ow/gen/proxy_thunks.obj build/ow/proxy/proxy.obj build/ow/proxy/leload.obj \
           build/ow/src/trace/trace.obj build/ow/src/trace/trfmt.obj build/ow/src/tex/texfmt.obj \
           build/ow/src/dll/config.obj build/ow/src/dll/dllmain.obj \
           $(patsubst %.c,build/ow/%.obj,$(wildcard src/rt/*.c)) build/ow/gen/api_names.obj build/ow/gen/glapi_names.obj
build/ow/proxy/%.obj: src/proxy/%.c | build/gen/stamp
	@mkdir -p $(dir $@)
	$(Q)echo "  WCC     $<"
	$(Q)$(WCC) $(OW_CFLAGS) $(OW_DLLFLAGS) -i=tests/shim -ad=$(@:.obj=.d) -adt=$@ -add=$< -adfs -fo=$@ $<
build/ow/proxy/leload.obj: tests/shim/leload.c | build/gen/stamp
	@mkdir -p $(dir $@)
	$(Q)echo "  WCC     $<"
	$(Q)$(WCC) $(OW_CFLAGS) $(OW_DLLFLAGS) -i=tests/shim -ad=$(@:.obj=.d) -adt=$@ -add=$< -adfs -fo=$@ $<
build/ow/GLTRACE.OVL: $(PX_OBJS) build/ow/mgahal.lib build/ow/glide2x.lnk
	$(Q)echo "  WLINK   $@"
	$(Q)$(WLINK) @build/ow/glide2x.lnk name $@ option map=build/ow/GLTRACE.map \
	  $(addprefix file ,$(PX_OBJS)) library build/ow/mgahal.lib
	$(Q)$(PYTHON) tools/abi/lefix.py $@ >/dev/null
gltrace: build/ow/GLTRACE.OVL

check-exports: build/ow/GLIDE2X.OVL
	$(PYTHON) tools/abi/check_exports.py $< abi/glide2x.api abi/games/*.names
check-clib: build/ow/GLIDE2X.OVL
	$(PYTHON) tools/abi/check_clib.py build/ow/GLIDE2X.map

# ---- Toolchains ----------------------------------------------------------
setup-ow:
	tools/setup/setup-ow.sh
setup-djgpp:
	tools/setup/setup-djgpp.sh

# The HAL's smoke test with DJGPP (as DOS-GL builds it) and with Open Watcom.
build/djgpp/SMOKE.EXE: tests/hal/smoke.c build/djgpp/libmgahal.a
	@mkdir -p $(dir $@)
	$(Q)echo "  DJLD    $@"
	$(Q)$(DJCC) $(DJ_CFLAGS) -o $@ $< build/djgpp/libmgahal.a
smoke-djgpp: build/djgpp/SMOKE.EXE

# ---- Host trace replay (tools/hreplay) --------------------------------------
HR_SRCS := $(RT_SRCS) $(HAL_HOST) tests/unit/refrast.c tools/hreplay/hreplay.c
HR_GEN  := build/gen/thunks.c build/gen/stubs.c build/gen/api_names.c build/gen/replay_gen.c build/gen/glapi_static.c
build/host32/hreplay: $(HR_SRCS) $(HR_GEN) $(wildcard src/*/*.h hal/include/mga/*.h tests/unit/*.h tests/shim/*.h)
	@mkdir -p $(dir $@)
	$(Q)echo "  CC32    $@"
	$(Q)$(HOST_CC) $(HOST_CFLAGS32) -Wno-unused-parameter -Wno-array-parameter -Itests/shim -Itests/unit -o $@ $(HR_SRCS) $(HR_GEN) -lm
hreplay: build/host32/hreplay
hreplay-check: build/host32/hreplay dostests runtime
	$(DEV) $(PYTHON) tools/hreplay/check.py

# ---- Host tests ----------------------------------------------------------
include tests/unit/unit.mk

clean:
	rm -rf build out

help:
	@echo "runtime        build build/ow/GLIDE2X.OVL (default)"
	@echo "check-exports  verify module name and exported names against the games"
	@echo "check-clib     verify the DLL links only self-contained C-library code"
	@echo "tests-host     build and run host unit tests"
	@echo "hal-ow|hal-djgpp|hal-host   build the shared HAL for each toolchain"
	@echo "header         regenerate include/glide/glide2.h from abi/"
	@echo "setup-ow|setup-djgpp        install pinned toolchains"
	@echo "loopa TEST=..  run a DOS test in 86Box (see tools/loopa)"

-include $(shell find build -name '*.d' 2>/dev/null)

# ---- Loop A (86Box) ------------------------------------------------------
.PHONY: 86box loopa loopa-selftest
86box:
	$(DEV) tools/86box/build.sh

# make loopa TEST=hello [ARGS="--frames 10"] [OVL=build/ow/GLIDE2X.OVL]
loopa: dostests
	$(DEV) $(PYTHON) tools/loopa/run.py --name $(TEST) --exe build/ow/dos/$(shell echo $(TEST) | tr a-z A-Z).EXE \
	  $(if $(ARGS),--args="$(ARGS)") $(if $(OVL),--ovl $(OVL))

loopa-selftest: dostests runtime
	@set -e; \
	check() { $(DEV) $(PYTHON) tools/loopa/run.py --name selftest-$$1 --exe build/ow/dos/HELLO.EXE \
	            --idle 25 --boot-grace 20 $${3:+--args=$$3} >/dev/null || true; \
	          got=$$(cat out/selftest-$$1/status); \
	          if [ "$$got" = "$$2" ]; then echo "  selftest $$1: $$got (ok)"; \
	          else echo "  selftest $$1: got $$got, want $$2"; exit 1; fi; }; \
	check pass PASS; check fail FAIL --fail; check hang HANG --hang; check crash GUEST-EXC --crash; \
	hook() { $(DEV) $(PYTHON) tools/loopa/run.py --name selftest-hook-$$1 --exe build/ow/dos/CONFORM.EXE \
	           --args=$$1 --ovl build/ow/GLIDE2X.OVL --idle 30 --timeout 90 >/dev/null || true; \
	         if grep -q "$$2" out/selftest-hook-$$1/serial.log && \
	            grep -q "HX-VMODE bios=03" out/selftest-hook-$$1/serial.log; then \
	           echo "  selftest hook-$$1: $$2, text mode restored (ok)"; \
	         else echo "  selftest hook-$$1: expected $$2 and text mode"; exit 1; fi; }; \
	hook x01 MGL-EXC; hook x02 MGL-EXIT-HOOK

# Conformance suite (tools/conform/run.py). References come from a retail
# OVL on the emulated Voodoo and are committed; checks run MGA-Glide.
.PHONY: conform conform-ref conform-ci rig hreplay hreplay-check gltrace smoke-djgpp
conform: runtime dostests
	$(DEV) $(PYTHON) tools/conform/run.py check $(TESTS)
conform-ref: dostests
	$(DEV) $(PYTHON) tools/conform/run.py ref $(TESTS)
conform-ci: runtime dostests
	$(PYTHON) tools/conform/run.py check
	MGA_CARD=g200 $(PYTHON) tools/conform/run.py check
	MGA_CARD=g400 $(PYTHON) tools/conform/run.py check
	MGA_CARD=g450 $(PYTHON) tools/conform/run.py check

# Loop C rig program (static x86-64 Linux, for cuda6; docs/loop-c-results.md).
build/mgarig: tests/rig/mgarig.c $(HAL_COMMON) hal/port/linux.c $(wildcard hal/include/mga/*.h)
	$(Q)echo "  CC      $@"
	$(Q)$(HOST_CC) -static -O2 -std=gnu99 -Wall -Wextra -Werror -Ihal/include -o $@ tests/rig/mgarig.c \
	  hal/src/engine.c hal/src/fifo.c hal/src/chip.c hal/src/pci.c hal/src/setup/trap.c hal/port/linux.c -lm
rig: build/mgarig
