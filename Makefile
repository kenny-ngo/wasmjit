# *** THIS IS NOT A LONG TERM SOLUTION ***

CFLAGS ?= -Isrc -g

all: wasmjit

wasmjit: src/wasmjit/main.o src/wasmjit/wasmbin.o src/wasmjit/wasmbin_parse.o src/wasmjit/wasmbin_dump.o src/wasmjit/wasmbin_compile.o
	$(CC) -o $@ $^ $(CFLAGS)

src/wasmjit/wasmbin.o: src/wasmjit/wasmbin.h

src/wasmjit/wasmbin_dump.o: src/wasmjit/wasmbin.h

src/wasmjit/main.o: src/wasmjit/wasmbin.h src/wasmjit/wasmbin_parse.h src/wasmjit/wasmbin_compile.h src/wasmjit/wasmbin_dump.h

src/wasmjit/wasmbin_parse.o: src/wasmjit/wasmbin.h src/wasmjit/wasmbin_parse.h src/wasmjit/util.h

src/wasmjit/wasmbin_compile.o: src/wasmjit/wasmbin.h src/wasmjit/util.h

%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS)
