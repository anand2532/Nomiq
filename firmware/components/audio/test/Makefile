CC     ?= cc
CFLAGS ?= -std=gnu99 -Wall -Wextra -Werror -O2 -g
C2     := ../../codec2/src
C2SRC  := $(wildcard $(C2)/*.c)
.PHONY: test clean
test: test_voice_codec
	./test_voice_codec
libc2.a: $(C2SRC)
	$(CC) -O2 -w -c -I$(C2) $(C2SRC) && ar rcs $@ *.o && rm -f *.o
test_voice_codec: test_voice_codec.c ../voice_codec.c ../include/voice_codec.h libc2.a
	$(CC) $(CFLAGS) -I../include -I$(C2) ../voice_codec.c test_voice_codec.c libc2.a -o $@ -lm
clean:
	rm -f test_voice_codec libc2.a
