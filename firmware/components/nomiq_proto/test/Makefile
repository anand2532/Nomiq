CC     ?= cc
CFLAGS ?= -std=c99 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined
INC    := -I../include
CORE   := ../nomiq_proto.c ../nomiq_xfer.c
HDRS   := $(wildcard ../include/*.h)
TESTS  := test_nomiq_proto test_nomiq_xfer

.PHONY: test clean
test: $(TESTS)
	@for t in $(TESTS); do echo "== $$t"; ./$$t || exit 1; done

test_%: test_%.c $(CORE) $(HDRS)
	$(CC) $(CFLAGS) $(INC) $(CORE) $< -o $@

clean:
	rm -f $(TESTS)
