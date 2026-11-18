# Host tests

Regression tests for the pure, hardware-independent leader-key state machine
(`src/leader.c` / `src/include/leader.h`). These compile and run on a normal
host with plain `gcc` — no ARM toolchain or pico-sdk needed. Run them with
`cd tests && make`: the default `test` target compiles `../src/leader.c`
together with `test_leader.c` (flags `-I../src/include -Wall -Wextra -O2`) into
the `test_leader` binary and runs it. The run prints per-test lines, ends with a
summary like `leader: 42/42 checks passed`, and exits non-zero if any check
fails. Use `make clean` to remove the binary. A thin `./run.sh` wrapper just
calls `make`.
