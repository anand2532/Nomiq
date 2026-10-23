CC     ?= cc
CFLAGS ?= -std=gnu99 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined
.PHONY: test clean
test: test_ui_model
	./test_ui_model
test_ui_model: test_ui_model.c ../ui_model.c ../include/ui_model.h
	$(CC) $(CFLAGS) -I../include ../ui_model.c test_ui_model.c -o $@
clean:
	rm -f test_ui_model
