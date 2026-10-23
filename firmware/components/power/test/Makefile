CC     ?= cc
CFLAGS ?= -std=gnu99 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined
.PHONY: test clean
test: test_power
	./test_power
test_power: test_power.c ../fuel_gauge.c ../power_policy.c $(wildcard ../include/*.h)
	$(CC) $(CFLAGS) -I../include ../fuel_gauge.c ../power_policy.c test_power.c -o $@
clean:
	rm -f test_power
