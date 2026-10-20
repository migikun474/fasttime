CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Werror

all: tbench ft_test

tbench: src/tbench.c include/fasttime.h
	$(CC) $(CFLAGS) -Iinclude -o $@ src/tbench.c -lpthread

ft_test: tests/test_fasttime.c include/fasttime.h
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_fasttime.c

check: ft_test asm
	./ft_test

# The conversion must be a mul and a shift: no division, no call.
asm: ft_test
	objdump -d --no-show-raw-insn ft_test | awk '/<to_ns>:/,/ret/'
	! objdump -d --no-show-raw-insn ft_test | awk '/<to_ns>:/,/ret/' | grep -Eq 'div|call'

clean:
	rm -f tbench ft_test

.PHONY: all check asm clean
