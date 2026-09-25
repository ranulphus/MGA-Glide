# Host unit tests. Each tests/unit/test_*.c is a standalone program built
# with the files listed in its UNIT_<name> variable.
UNIT_TESTS := $(patsubst tests/unit/%.c,%,$(wildcard tests/unit/test_*.c))
UNIT_COMMON := tests/unit/unit.c
UNIT_test_fmt := src/rt/fmt.c

tests-host: $(UNIT_TESTS:%=build/host/tests/%)
	@set -e; for t in $^; do echo "== $$t"; $$t; done; echo "tests-host: all passed"

build/host/tests/%: tests/unit/%.c $(UNIT_COMMON) build/gen/stamp
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_CFLAGS) -o $@ $< $(UNIT_COMMON) $(UNIT_$*) -lm
