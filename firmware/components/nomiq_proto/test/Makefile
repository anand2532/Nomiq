CC     ?= cc
CFLAGS ?= -std=c99 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined
SRC    := ../nomiq_proto.c test_nomiq_proto.c

.PHONY: test clean
test: test_nomiq_proto
	./test_nomiq_proto

test_nomiq_proto: $(SRC) ../include/nomiq_proto.h
	$(CC) $(CFLAGS) -I../include $(SRC) -o $@

clean:
	rm -f test_nomiq_proto
