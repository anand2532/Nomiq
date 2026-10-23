CC     ?= cc
CFLAGS ?= -std=gnu99 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined
.PHONY: test clean
test: test_stt_proto
	./test_stt_proto
test_stt_proto: test_stt_proto.c ../stt_proto.c ../include/stt_proto.h
	$(CC) $(CFLAGS) -I../include ../stt_proto.c test_stt_proto.c -o $@
clean:
	rm -f test_stt_proto
