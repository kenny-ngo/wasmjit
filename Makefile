# *** THIS IS NOT A LONG TERM SOLUTION ***

CFLAGS ?= -Isrc -g -Wall -Wextra -Werror

all: wasmjit

clean:
	rm -f src/wasmjit/vector.o src/wasmjit/wasmbin.o src/wasmjit/wasmbin_dump.o src/wasmjit/main.o src/wasmjit/wasmbin_parse.o src/wasmjit/wasmbin_compile.o wasmjit

wasmjit: src/wasmjit/main.o src/wasmjit/vector.o src/wasmjit/wasmbin.o src/wasmjit/wasmbin_parse.o src/wasmjit/wasmbin_dump.o src/wasmjit/wasmbin_compile.o
	$(CC) -o $@ $^ $(CFLAGS)

%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS)
