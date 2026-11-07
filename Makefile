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

# Unity C unit test configuration
TEST_TARGET := test_runner
TEST_SRCS   := tests/test_senkuvim.c tests/unity.c terminal.c buffer.c ide_features.c syntax.c ui_rice.c
TEST_OBJS   := $(filter-out main.o, $(OBJS)) tests/test_senkuvim.o tests/unity.o

PREFIX   ?= /usr/local
USER_BIN := $(HOME)/.local/bin

.PHONY: all clean run valgrind asan test unit-test unit-test-asan test-all install uninstall

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $(TARGET) $(LDFLAGS)

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

# Pattern rule for compiling tests with access to project headers and Unity
tests/%.o: tests/%.c $(HEADERS) tests/unity.h tests/unity_internals.h
	$(CC) $(CFLAGS) -I. -Itests -c $< -o $@

run: $(TARGET)
	./$(TARGET)

test: $(TARGET)
	python3 test_senkuvim.py

# Native C Unit Tests (Unity)
unit-test: $(TEST_OBJS)
	$(CC) $(CFLAGS) $(TEST_OBJS) -o $(TEST_TARGET) $(LDFLAGS)
	./$(TEST_TARGET)

# Native C Unit Tests under AddressSanitizer and UndefinedBehaviorSanitizer
unit-test-asan:
	$(CC) -fsanitize=address,undefined -g -Wall -Wextra -Wpedantic -std=c17 \
		-D_DEFAULT_SOURCE -D_BSD_SOURCE -D_GNU_SOURCE -I. -Itests \
		$(TEST_SRCS) -o $(TEST_TARGET)_asan
	./$(TEST_TARGET)_asan

# Combined test run: native unit tests followed by PTY integration tests
test-all: unit-test-asan test

asan:
	$(CC) -fsanitize=address,undefined -g -Wall -Wextra -Wpedantic -std=c17 -D_DEFAULT_SOURCE -D_BSD_SOURCE -D_GNU_SOURCE $(SRCS) -o $(TARGET)_asan

clean:
	rm -f $(OBJS) $(TARGET) $(TARGET)_asan $(TEST_OBJS) $(TEST_TARGET) $(TEST_TARGET)_asan test_out.txt test_c.c test_py.py valgrind_test.txt valgrind.log

valgrind: $(TARGET)
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./$(TARGET)

install: $(TARGET)
	@if [ -w $(DESTDIR)$(PREFIX)/bin ] 2>/dev/null || [ "$$(id -u)" = "0" ]; then \
		install -d $(DESTDIR)$(PREFIX)/bin && \
		install -m 755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/$(TARGET); \
	fi
	mkdir -p $(USER_BIN)
	cp $(TARGET) $(USER_BIN)/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	rm -f $(USER_BIN)/$(TARGET)
