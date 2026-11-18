# Host-compiled unit tests for the pure, hardware-independent modules.
# Requires only plain gcc (no ARM toolchain, no pico-sdk).

CC      ?= gcc
CFLAGS  ?= -std=c11 -I../src/include -Wall -Wextra -O2

LEADER_SRC = ../src/leader.c test_leader.c
LEADER_BIN = test_leader

INJECT_SRC = ../src/inject.c test_inject.c
INJECT_BIN = test_inject

.PHONY: test clean

test: $(LEADER_BIN) $(INJECT_BIN)
	./$(LEADER_BIN)
	./$(INJECT_BIN)

$(LEADER_BIN): $(LEADER_SRC) ../src/include/leader.h
	$(CC) $(CFLAGS) -o $(LEADER_BIN) $(LEADER_SRC)

$(INJECT_BIN): $(INJECT_SRC) ../src/include/inject.h
	$(CC) $(CFLAGS) -o $(INJECT_BIN) $(INJECT_SRC)

clean:
	rm -f $(LEADER_BIN) $(INJECT_BIN)
