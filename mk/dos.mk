# DOS test programs (32-bit, DOS/4GW) and 16-bit batch helpers.
BUILD_ID := $(shell git describe --always --dirty 2>/dev/null || echo unknown)
OW_EXEFLAGS := $(OW_CFLAGS) -i=tests/shim -dHX_BUILD_ID="\"$(BUILD_ID)\""

SHIM_SRCS := tests/shim/hx.c tests/shim/leload.c tests/shim/glbind.c build/gen/glapi_names.c
SHIM_OBJS := $(SHIM_SRCS:%.c=build/ow/exe/%.obj)

build/ow/exe/build/gen/%.obj: build/gen/%.c | build/gen/stamp
	@mkdir -p $(dir $@)
	$(Q)echo "  WCC     $<"
	$(Q)$(WCC) $(OW_EXEFLAGS) -ad=$(@:.obj=.d) -adt=$@ -add=$< -adfs -fo=$@ $<

build/ow/exe/%.obj: %.c | build/gen/stamp
	@mkdir -p $(dir $@)
	$(Q)echo "  WCC     $<"
	$(Q)$(WCC) $(OW_EXEFLAGS) -ad=$(@:.obj=.d) -adt=$@ -add=$< -adfs -fo=$@ $<

# build/ow/dos/<NAME>.EXE from a list of objects.
define dos_exe
build/ow/dos/$(1).EXE: $(2) $(SHIM_OBJS) build/ow/mgahal_exe.lib
	@mkdir -p build/ow/dos
	$$(Q)echo "  WLINK   $$@"
	$$(Q)$$(WLINK) system dos4g option quiet option stack=64k name $$@ \
	  $$(addprefix file ,$(2) $$(SHIM_OBJS)) library build/ow/mgahal_exe.lib option map=$$(@:.EXE=.map)
DOS_EXES += build/ow/dos/$(1).EXE
endef

$(eval $(call dos_exe,HELLO,build/ow/exe/tests/shim/hello.obj))
$(eval $(call dos_exe,LESPIKE,build/ow/exe/tests/spike/lespike.obj))
$(eval $(call dos_exe,PROBE,build/ow/exe/tests/hal/probe.obj))
$(eval $(call dos_exe,ROMDUMP,build/ow/exe/tests/hal/romdump.obj))
$(eval $(call dos_exe,SMOKE,build/ow/exe/tests/hal/smoke.obj))
$(eval $(call dos_exe,SCALE,build/ow/exe/tests/hal/scale.obj))
$(eval $(call dos_exe,GLRES,build/ow/exe/tests/glres/glres.obj))
$(eval $(call dos_exe,TEXPROBE,build/ow/exe/tests/spike/texprobe.obj))
CONFORM_OBJS := $(patsubst %.c,build/ow/exe/%.obj,$(wildcard tests/conform/*.c))
GLPLAY_OBJS := build/ow/exe/tests/replay/glplay.obj build/ow/exe/build/gen/replay_gen.obj build/ow/exe/src/trace/trfmt.obj
$(eval $(call dos_exe,GLPLAY,$(GLPLAY_OBJS)))
$(eval $(call dos_exe,CONFORM,$(CONFORM_OBJS)))

# 16-bit .COM helpers.
DOS_TOOLS := UTEXIT SERSAY WAITSEC REBOOT VMODE KEYWAIT VECCHK SBCHK
define dos_com
build/ow/dos/$(1).COM: tools/dos/$(2).c
	@mkdir -p build/ow/dos/obj16
	$$(Q)echo "  WCC16   $$<"
	$$(Q)$$(OWENV) $$(OWBIN)/wcc -bt=dos -ms -0 -os -zq -we -fo=build/ow/dos/obj16/$(2).obj $$<
	$$(Q)$$(WLINK) system com option quiet name $$@ file build/ow/dos/obj16/$(2).obj
endef
$(eval $(call dos_com,UTEXIT,utexit))
$(eval $(call dos_com,SERSAY,sersay))
$(eval $(call dos_com,WAITSEC,waitsec))
$(eval $(call dos_com,REBOOT,reboot))
$(eval $(call dos_com,VMODE,vmode))
$(eval $(call dos_com,KEYWAIT,keywait))
$(eval $(call dos_com,VECCHK,vecchk))
$(eval $(call dos_com,SBCHK,sbchk))

dostools: $(DOS_TOOLS:%=build/ow/dos/%.COM)
dostests: $(DOS_EXES) dostools
.PHONY: dostools dostests
