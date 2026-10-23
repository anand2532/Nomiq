CC     ?= cc
CFLAGS ?= -std=gnu99 -Wall -Wextra -Werror -Wno-unused-result -O1 -g -fsanitize=address,undefined
P      := ../../nomiq_proto
S      := ../../storage
INC    := -I../include -I$(P)/include -I$(S)/include
SRC    := ../app_core.c $(P)/nomiq_proto.c $(P)/nomiq_xfer.c $(P)/nomiq_crypto.c $(P)/crypto_openssl.c $(S)/msg_store.c
.PHONY: test clean
test: test_app_core
	./test_app_core
test_app_core: test_app_core.c $(SRC) $(wildcard ../include/*.h $(P)/include/*.h $(S)/include/*.h)
	$(CC) $(CFLAGS) $(INC) $(SRC) test_app_core.c -o $@ -lcrypto
clean:
	rm -f test_app_core
