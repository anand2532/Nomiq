CC     ?= cc
CFLAGS ?= -std=gnu99 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined
.PHONY: test clean
test: test_image_fit
	./test_image_fit
test_image_fit: test_image_fit.c ../image_fit.c ../include/image_fit.h
	$(CC) $(CFLAGS) -I../include ../image_fit.c test_image_fit.c -o $@
clean:
	rm -f test_image_fit
