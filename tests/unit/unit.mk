# Host unit tests. Each tests/unit/test_*.c is a standalone program built
# with the files listed in its UNIT_<name> variable.
UNIT_TESTS := $(patsubst tests/unit/%.c,%,$(wildcard tests/unit/test_*.c))
UNIT_COMMON := tests/unit/unit.c
UNIT_test_fmt := src/rt/fmt.c
UNIT_test_leload := tests/shim/leload.c build/gen/glapi_names.c
UNIT_test_trap := hal/src/setup/trap.c tests/unit/refrast.c

tests-host: $(UNIT_TESTS:%=build/host/tests/%) build/ow/GLIDE2X.OVL
	@set -e; for t in $(UNIT_TESTS:%=build/host/tests/%); do echo "== $$t"; FIXTURES_DIR=$(FIXTURES_DIR) $$t; done; echo "tests-host: all passed"

build/gen/glapi_names.c build/gen/glapi.h: build/gen/stamp

.SECONDEXPANSION:
build/host/tests/%: tests/unit/%.c $(UNIT_COMMON) $$(UNIT_$$*) build/gen/stamp
	@mkdir -p $(dir $@)
	$(Q)echo "  CC      $@"
	$(Q)$(HOST_CC) $(HOST_CFLAGS) -Itests/shim -o $@ $< $(UNIT_COMMON) $(UNIT_$*) -lm
