# MGA-Glide top-level build. `make help` lists the targets.
include config.mk
-include config.local.mk
include tools/setup/versions.mk
include mk/ow.mk
include mk/host.mk
include mk/djgpp.mk

export WATCOM DJGPP_PREFIX MGA_CACHE OW_URL OW_SHA256 DJGPP_URL DJGPP_SHA256

.DEFAULT_GOAL := runtime
.PHONY: help runtime header check-header check-exports check-clib tests-host \
        setup-ow setup-djgpp hal-ow hal-djgpp hal-host clean

# ---- Generated glue ------------------------------------------------------
API_SRCS := abi/glide2x.api tools/abi/api.py tools/abi/gen_exports.py
build/gen/stamp: $(API_SRCS)
	@mkdir -p build/gen
	$(PYTHON) tools/abi/gen_exports.py . build/gen
	@touch $@

header: include/glide/glide2.h
include/glide/glide2.h: abi/constants.tsv abi/types.h.frag abi/glide2x.api tools/abi/gen_header.py
	$(PYTHON) tools/abi/gen_header.py . $@

check-header:
	@$(PYTHON) tools/abi/gen_header.py . build/glide2.h.check && cmp -s build/glide2.h.check include/glide/glide2.h \
	  || { echo "include/glide/glide2.h is stale: run make header"; exit 1; }

# ---- HAL -----------------------------------------------------------------
HAL_COMMON := hal/src/debug/serial.c
HAL_OW     := $(HAL_COMMON) hal/port/ow_dos4g.c
HAL_DJGPP  := $(HAL_COMMON) hal/port/djgpp.c
HAL_HOST   := $(HAL_COMMON) hal/port/host.c

build/ow/mgahal.lib: $(HAL_OW:%.c=build/ow/%.obj)
	@rm -f $@
	$(Q)echo "  WLIB    $@"
	$(Q)$(WLIB) -q -b -n $@ $(addprefix +,$^)
hal-ow: build/ow/mgahal.lib

build/djgpp/libmgahal.a: $(HAL_DJGPP:%.c=build/djgpp/%.o)
	@rm -f $@
	$(DJAR) rcs $@ $^
hal-djgpp: build/djgpp/libmgahal.a

build/host/libmgahal.a: $(HAL_HOST:%.c=build/host/%.o)
	@rm -f $@
	ar rcs $@ $^
hal-host: build/host/libmgahal.a

build/gen/stubs.c build/gen/api_names.c build/gen/api_ids.h build/gen/api_names.h: build/gen/stamp

# ---- Runtime DLL ---------------------------------------------------------
RT_SRCS := $(wildcard src/rt/*.c src/dll/*.c src/glide/*.c)
RT_OBJS := $(RT_SRCS:%.c=build/ow/%.obj) build/ow/gen/stubs.obj build/ow/gen/api_names.obj

build/ow/glide2x.lnk: mk/glide2x.lnk.in Makefile
	@mkdir -p $(dir $@)
	$(Q)sed -e 's|@WATCOM@|$(WATCOM)|g' -e 's|@VER@|$(shell git describe --always --dirty 2>/dev/null)|' $< > $@

build/ow/GLIDE2X.OVL: $(RT_OBJS) build/ow/mgahal.lib build/ow/glide2x.lnk
	$(Q)echo "  WLINK   $@"
	$(Q)$(WLINK) @build/ow/glide2x.lnk name $@ option map=build/ow/GLIDE2X.map \
	  $(addprefix file ,$(RT_OBJS)) library build/ow/mgahal.lib
	$(Q)$(PYTHON) tools/abi/lefix.py $@ >/dev/null
runtime: build/ow/GLIDE2X.OVL

check-exports: build/ow/GLIDE2X.OVL
	$(PYTHON) tools/abi/check_exports.py $< abi/glide2x.api abi/games/*.names
check-clib: build/ow/GLIDE2X.OVL
	$(PYTHON) tools/abi/check_clib.py build/ow/GLIDE2X.map

# ---- Toolchains ----------------------------------------------------------
setup-ow:
	tools/setup/setup-ow.sh
setup-djgpp:
	tools/setup/setup-djgpp.sh

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
