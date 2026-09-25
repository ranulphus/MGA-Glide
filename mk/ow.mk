# V=1 shows full command lines.
Q := $(if $(filter 1,$(V)),,@)
# Open Watcom v2 toolchain (runtime DLL, DOS/4GW test programs, HAL).
OWBIN   := $(WATCOM)/binl64
OWENV   := env WATCOM=$(WATCOM) INCLUDE=$(WATCOM)/h PATH=$(OWBIN):$(PATH)
WCC     := $(OWENV) $(OWBIN)/wcc386
WLINK   := $(OWENV) $(OWBIN)/wlink
WLIB    := $(OWENV) $(OWBIN)/wlib
WDUMP   := $(OWENV) $(OWBIN)/wdump

OW_CFLAGS := -bt=dos -mf -3s -fp5 -fpi87 -zri -ei -j -zastd=c99 -zq -we -wx \
             -i=include -i=hal/include -i=src -i=build/gen -dMGA_OW=1
ifeq ($(DEBUG),1)
OW_CFLAGS += -od -d1 -dMG_DEBUG=1
else
OW_CFLAGS += -oxt
endif
# DLL and HAL code: no stack probes, no default-library references.
OW_DLLFLAGS := -bd -s -zl

build/ow/%.obj: %.c | build/gen/stamp
	@mkdir -p $(dir $@)
	$(Q)echo "  WCC     $<"
	$(Q)$(WCC) $(OW_CFLAGS) $(if $(filter src/% hal/%,$<),$(OW_DLLFLAGS)) -ad=$(@:.obj=.d) -fo=$@ $<

build/ow/gen/%.obj: build/gen/%.c | build/gen/stamp
	@mkdir -p $(dir $@)
	$(Q)echo "  WCC     $<"
	$(Q)$(WCC) $(OW_CFLAGS) $(OW_DLLFLAGS) -ad=$(@:.obj=.d) -fo=$@ $<
