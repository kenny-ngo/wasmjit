# *** THIS IS NOT A LONG TERM SOLUTION ***

CFLAGS ?= -Isrc -g -Wall -Wextra -Werror

all: wasmjit

clean:
	rm -f src/wasmjit/vector.o src/wasmjit/ast.o src/wasmjit/ast_dump.o src/wasmjit/main.o src/wasmjit/parse.o src/wasmjit/compile.o wasmjit src/wasmjit/instantiate.o src/wasmjit/execute.o src/wasmjit/runtime.o

wasmjit: src/wasmjit/main.o src/wasmjit/vector.o src/wasmjit/ast.o src/wasmjit/parse.o src/wasmjit/ast_dump.o src/wasmjit/compile.o src/wasmjit/instantiate.o src/wasmjit/execute.o src/wasmjit/runtime.o
	$(CC) -o $@ $^ $(CFLAGS)

%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS)
