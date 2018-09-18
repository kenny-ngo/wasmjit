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

#ifndef __WASMJIT__EMSCRIPTEN_RUNTIME_H__
#define __WASMJIT__EMSCRIPTEN_RUNTIME_H__

#include <wasmjit/runtime.h>

#include <wasmjit/sys.h>

enum {
	WASMJIT_EMSCRIPTEN_TOTAL_MEMORY = 16777216,
};

struct EmscriptenContext {
	struct FuncInst *errno_location_inst;
	char **environ;
};

void wasmjit_emscripten_abortStackOverflow(uint32_t allocSize, struct FuncInst *funcinst);
uint32_t wasmjit_emscripten_abortOnCannotGrowMemory(struct FuncInst *funcinst);
uint32_t wasmjit_emscripten_enlargeMemory(struct FuncInst *funcinst);
uint32_t wasmjit_emscripten_getTotalMemory(struct FuncInst *funcinst);
void wasmjit_emscripten_nullFunc_ii(uint32_t x, struct FuncInst *funcinst);
void wasmjit_emscripten_nullFunc_iiii(uint32_t x, struct FuncInst *funcinst);
void wasmjit_emscripten____lock(uint32_t x, struct FuncInst *funcinst);
void wasmjit_emscripten____setErrNo(uint32_t value, struct FuncInst *funcinst);
uint32_t wasmjit_emscripten____syscall140(uint32_t which, uint32_t varargs, struct FuncInst *funcinst);
uint32_t wasmjit_emscripten____syscall146(uint32_t which, uint32_t varargs, struct FuncInst *funcinst);
uint32_t wasmjit_emscripten____syscall4(uint32_t which, uint32_t varargs, struct FuncInst *funcinst);
uint32_t wasmjit_emscripten____syscall54(uint32_t which, uint32_t varargs, struct FuncInst *funcinst);
uint32_t wasmjit_emscripten____syscall6(uint32_t which, uint32_t varargs, struct FuncInst *funcinst);
void wasmjit_emscripten____unlock(uint32_t x, struct FuncInst *funcinst);
uint32_t wasmjit_emscripten__emscripten_memcpy_big(uint32_t dest, uint32_t src, uint32_t num, struct FuncInst *funcinst);
void wasmjit_emscripten_abort(uint32_t, struct FuncInst *) __attribute__((noreturn));
void wasmjit_emscripten____buildEnvironment(uint32_t, struct FuncInst *);
uint32_t wasmjit_emscripten____syscall10(uint32_t which, uint32_t varargs, struct FuncInst *funcinst);
uint32_t wasmjit_emscripten____syscall102(uint32_t which, uint32_t varargs, struct FuncInst *funcinst);
uint32_t wasmjit_emscripten____syscall221(uint32_t which, uint32_t varargs, struct FuncInst *funcinst);
void wasmjit_emscripten_start_func(struct FuncInst *funcinst);

struct EmscriptenContext *wasmjit_emscripten_get_context(struct ModuleInst *);
void wasmjit_emscripten_cleanup(struct ModuleInst *);

void wasmjit_emscripten_internal_abort(const char *msg) __attribute__((noreturn));
struct MemInst *wasmjit_emscripten_get_mem_inst(struct FuncInst *funcinst);


int wasmjit_emscripten_init(struct EmscriptenContext *ctx,
			    struct FuncInst *errno_location_inst,
			    char *envp[]);

int wasmjit_emscripten_invoke_main(struct MemInst *meminst,
				   struct FuncInst *stack_alloc_inst,
				   struct FuncInst *main_inst,
				   int argc,
				   char *argv[]);

struct WasmJITEmscriptenMemoryGlobals {
	uint32_t memoryBase;
	uint32_t tempDoublePtr;
	uint32_t DYNAMICTOP_PTR;
	uint32_t STACKTOP;
	uint32_t STACK_MAX;
};

void wasmjit_emscripten_derive_memory_globals(uint32_t static_bump,
					      struct WasmJITEmscriptenMemoryGlobals *out);

#endif
