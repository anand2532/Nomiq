CC     ?= cc
CFLAGS ?= -std=gnu99 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined
.PHONY: test clean
test: test_ui_model test_buttons test_ui_view
	./test_ui_model && ./test_buttons && ./test_ui_view
test_ui_model: test_ui_model.c ../ui_model.c ../include/ui_model.h
	$(CC) $(CFLAGS) -I../include ../ui_model.c test_ui_model.c -o $@
test_buttons: test_buttons.c ../buttons.c ../include/buttons.h
	$(CC) $(CFLAGS) -I../include ../buttons.c test_buttons.c -o $@
test_ui_view: test_ui_view.c ../ui_view.c ../ui_model.c $(wildcard ../include/*.h)
	$(CC) $(CFLAGS) -I../include ../ui_view.c ../ui_model.c test_ui_view.c -o $@
clean:
	rm -f test_ui_model test_buttons test_ui_view
