# Host-compiled unit tests for the pure leader-key module.
# Requires only plain gcc (no ARM toolchain, no pico-sdk).

CC      ?= gcc
CFLAGS  ?= -std=c11 -I../src/include -Wall -Wextra -O2
SRC      = ../src/leader.c test_leader.c
BIN      = test_leader

.PHONY: test clean

test: $(BIN)
	./$(BIN)

$(BIN): $(SRC) ../src/include/leader.h
	$(CC) $(CFLAGS) -o $(BIN) $(SRC)

clean:
	rm -f $(BIN)
