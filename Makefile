# ==============================================================================
# SENKUVIM - Makefile
# Production Modal Terminal Editor in C17 (Arch Linux / POSIX)
# ==============================================================================

CC      := gcc
CFLAGS  := -Wall -Wextra -Wpedantic -std=c17 -O2 -D_DEFAULT_SOURCE -D_BSD_SOURCE -D_GNU_SOURCE
LDFLAGS :=

TARGET  := senkuvim
SRCS    := main.c terminal.c buffer.c ide_features.c syntax.c ui_rice.c
OBJS    := $(SRCS:.c=.o)
HEADERS := senkuvim.h

PREFIX  ?= /usr/local
USER_BIN   := $(HOME)/.local/bin

.PHONY: all clean run valgrind asan test install uninstall

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $(TARGET) $(LDFLAGS)

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

test: $(TARGET)
	python3 test_senkuvim.py

asan:
	$(CC) -fsanitize=address,undefined -g -Wall -Wextra -Wpedantic -std=c17 -D_DEFAULT_SOURCE -D_BSD_SOURCE -D_GNU_SOURCE $(SRCS) -o $(TARGET)_asan

clean:
	rm -f $(OBJS) $(TARGET) $(TARGET)_asan test_out.txt test_c.c test_py.py valgrind_test.txt valgrind.log

valgrind: $(TARGET)
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./$(TARGET)

install: $(TARGET)
	@if [ -w $(DESTDIR)$(PREFIX)/bin ] 2>/dev/null || [ "$$(id -u)" = "0" ]; then \
		install -d $(DESTDIR)$(PREFIX)/bin && \
		install -m 755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/$(TARGET); \
	fi
	mkdir -p $(HOME)/.local/bin
	cp $(TARGET) $(HOME)/.local/bin/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	rm -f $(USER_BIN)/$(TARGET)
