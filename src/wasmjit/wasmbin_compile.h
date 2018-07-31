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

#ifndef __WASMJIT__WASMBIN_COMPILE_H__
#define __WASMJIT__WASMBIN_COMPILE_H__

#include <wasmjit/wasmbin.h>

#include <stddef.h>

struct ModuleInst {
	size_t *funcaddrs;
	size_t *tableaddrs;
	size_t *memaddrs;
	size_t *globaladdrs;
};

struct Store {
	size_t n_funcs;
	struct FuncInst {
		void *code;
	} *funcs;
	size_t n_mems;
	struct MemInst {
		char *data;
		uint32_t max;
	} *mems;
};

void wasmjit_compile_code(const struct Store *store,
			  const struct ModuleInst *module,
			  const struct TypeSectionType *type,
			  const struct CodeSectionCode *code);

#endif
