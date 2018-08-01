# *** THIS IS NOT A LONG TERM SOLUTION ***

CFLAGS ?= -Isrc -g -Wall -Wextra -Werror

all: wasmjit

clean:
	rm -f src/wasmjit/vector.o src/wasmjit/wasmbin.o src/wasmjit/wasmbin_dump.o src/wasmjit/main.o src/wasmjit/wasmbin_parse.o src/wasmjit/wasmbin_compile.o wasmjit src/wasmjit/instantiate.o src/wasmjit/execute.o src/wasmjit/runtime.o

wasmjit: src/wasmjit/main.o src/wasmjit/vector.o src/wasmjit/wasmbin.o src/wasmjit/wasmbin_parse.o src/wasmjit/wasmbin_dump.o src/wasmjit/wasmbin_compile.o src/wasmjit/instantiate.o src/wasmjit/execute.o src/wasmjit/runtime.o
	$(CC) -o $@ $^ $(CFLAGS)

%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS)
