/* -*-mode:c; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
  Copyright (c) 2018 Rian Hunter

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
 */

#include <wasmjit/ast.h>
#include <wasmjit/static_runtime.h>

#include <wasmjit/emscripten_runtime.h>

#include <math.h>

#define staticAlloc(_top , s)                   \
	(((_top) + (s) + 15) & ((uint32_t) -16))

#define alignMemory(size, factor) \
	(((size) % (factor)) ? ((size) - ((size) % (factor)) + (factor)) : (size))

struct FuncInst *table_buffer[10];

struct WasmInst WASM_SYMBOL(env, table) = {
	.type = IMPORT_DESC_TYPE_TABLE,
	.u = {
		.table = {
			.data = table_buffer,
			.elemtype = ELEMTYPE_ANYFUNC,
			.length = sizeof(table_buffer) / sizeof(table_buffer[0]),
			.max = sizeof(table_buffer) / sizeof(table_buffer[0]),
		},
	},
};

char mem_buffer[256 * WASM_PAGE_SIZE];

struct WasmInst WASM_SYMBOL(env, memory) = {
	.type = IMPORT_DESC_TYPE_MEM,
	.u = {
		.mem = {
			.data = mem_buffer,
			.size = sizeof(mem_buffer),
			.max = sizeof(mem_buffer),
		},
	},
};

enum {
	TOTAL_STACK = 5242880,
	STACK_ALIGN = 16,
	GLOBAL_BASE = 1024,
	STATIC_BASE = GLOBAL_BASE,
	STATICTOP = STATIC_BASE + 5472 - 16,
	tempDoublePtr = staticAlloc(STATICTOP, 16),
	DYNAMICTOP_PTR = staticAlloc(tempDoublePtr, 4),
	STACKTOP = alignMemory(DYNAMICTOP_PTR, STACK_ALIGN),
	STACK_BASE = STACKTOP,
	STACK_MAX = STACK_BASE + TOTAL_STACK,
};

DEFINE_WASM_I32_GLOBAL(env, memoryBase, STATIC_BASE, 0);
DEFINE_WASM_I32_GLOBAL(env, tableBase, 0, 0);
DEFINE_WASM_I32_GLOBAL(env, DYNAMICTOP_PTR, DYNAMICTOP_PTR, 0);
DEFINE_WASM_I32_GLOBAL(env, tempDoublePtr, tempDoublePtr, 0);
DEFINE_WASM_I32_GLOBAL(env, ABORT, 0, 0);
DEFINE_WASM_I32_GLOBAL(env, STACKTOP, STACKTOP, 0);
DEFINE_WASM_I32_GLOBAL(env, STACK_MAX, STACK_MAX, 0);
DEFINE_WASM_F64_GLOBAL(global, NaN, NAN, 0);
DEFINE_WASM_F64_GLOBAL(global, Infinity, INFINITY, 0);

#define DEFINE_EMSCRIPTEN_FUNCTION(_name, ...) \
	DEFINE_WASM_FUNCTION(env, _name, &(wasmjit_emscripten_ ## _name), __VA_ARGS__)

DEFINE_EMSCRIPTEN_FUNCTION(enlargeMemory, VALTYPE_I32);
DEFINE_EMSCRIPTEN_FUNCTION(getTotalMemory, VALTYPE_I32);
DEFINE_EMSCRIPTEN_FUNCTION(abortOnCannotGrowMemory, VALTYPE_I32);
DEFINE_EMSCRIPTEN_FUNCTION(abortStackOverflow, VALTYPE_NULL, VALTYPE_I32);
DEFINE_EMSCRIPTEN_FUNCTION(nullFunc_ii, VALTYPE_NULL, VALTYPE_I32);
DEFINE_EMSCRIPTEN_FUNCTION(nullFunc_iiii, VALTYPE_NULL, VALTYPE_I32);
DEFINE_EMSCRIPTEN_FUNCTION(___lock, VALTYPE_NULL, VALTYPE_I32);
DEFINE_EMSCRIPTEN_FUNCTION(___setErrNo, VALTYPE_NULL, VALTYPE_I32);
DEFINE_EMSCRIPTEN_FUNCTION(___syscall140, VALTYPE_I32, VALTYPE_I32, VALTYPE_I32);
DEFINE_EMSCRIPTEN_FUNCTION(___syscall146, VALTYPE_I32, VALTYPE_I32, VALTYPE_I32);
DEFINE_EMSCRIPTEN_FUNCTION(___syscall54, VALTYPE_I32, VALTYPE_I32, VALTYPE_I32);
DEFINE_EMSCRIPTEN_FUNCTION(___syscall6, VALTYPE_I32, VALTYPE_I32, VALTYPE_I32);
DEFINE_EMSCRIPTEN_FUNCTION(___unlock, VALTYPE_NULL, VALTYPE_I32);
DEFINE_EMSCRIPTEN_FUNCTION(_emscripten_memcpy_big, VALTYPE_I32, VALTYPE_I32, VALTYPE_I32, VALTYPE_I32);

char *wasmjit_emscripten_get_base_address(void) {
	return WASM_SYMBOL(env, memoryBase).u.mem.data;
}

extern struct WasmInst WASM_SYMBOL(env, _main);


int main(int argc, char *argv[]) {
	/* TODO: put argv into memory */
	int (*_main)(int, char *[]);
	_main = WASM_SYMBOL(env, _main).u.func.compiled_code;
	return _main(argc, argv);
}
