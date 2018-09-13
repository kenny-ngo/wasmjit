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

#include <wasmjit/emscripten_runtime_sys.h>

#include <wasmjit/ast.h>
#include <wasmjit/runtime.h>
#include <wasmjit/util.h>

#include <errno.h>

#define __KDECL(to,n,t) t _##n
#define __KA(to,n,t) _##n

#define KWSCx(x, name, ...)					\
	long sys_ ## name(__KMAP(x, __KDECL, __VA_ARGS__))	\
	{							\
		long ret;					\
		ret = name(__KMAP(x, __KA, __VA_ARGS__));	\
		if (ret == -1) {				\
			ret = -errno;				\
		}						\
		return ret;					\
	}

#define KWSC1(name, ...) KWSCx(1, name, __VA_ARGS__)
#define KWSC3(name, ...) KWSCx(3, name, __VA_ARGS__)

#include <wasmjit/emscripten_runtime_sys_def.h>

struct MemInst *wasmjit_emscripten_get_mem_inst(struct FuncInst *funcinst) {
	return funcinst->module_inst->mems.elts[0];
}

__attribute__((noreturn))
void wasmjit_emscripten_internal_abort(const char *msg)
{
	fprintf(stderr, "%s", msg);
	wasmjit_trap(WASMJIT_TRAP_ABORT);
}
