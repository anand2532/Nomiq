CC     ?= cc
CFLAGS ?= -std=gnu99 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined
.PHONY: test clean
test: test_sx126x
	./test_sx126x
test_sx126x: test_sx126x.c ../sx126x.c ../include/sx126x.h
	$(CC) $(CFLAGS) -I../include ../sx126x.c test_sx126x.c -o $@
clean:
	rm -f test_sx126x
