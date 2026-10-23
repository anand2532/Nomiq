CC     ?= cc
CFLAGS ?= -std=gnu99 -Wall -Wextra -Werror -Wno-unused-result -O1 -g -fsanitize=address,undefined
.PHONY: test clean
test: test_msg_store
	./test_msg_store
test_msg_store: test_msg_store.c ../msg_store.c ../include/msg_store.h
	$(CC) $(CFLAGS) -I../include ../msg_store.c test_msg_store.c -o $@
clean:
	rm -f test_msg_store
