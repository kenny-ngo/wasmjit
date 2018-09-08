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

#ifndef __WASMJIT__COMPILE_H__
#define __WASMJIT__COMPILE_H__

#include <wasmjit/ast.h>

#include <wasmjit/sys.h>

struct ModuleTypes {
	struct FuncType *functypes;
	struct TableType *tabletypes;
	struct MemoryType *memorytypes;
	struct GlobalType *globaltypes;
};

struct MemoryReferences {
	size_t n_elts;
	struct MemoryReferenceElt {
		enum {
			MEMREF_TYPE,
			MEMREF_FUNC,
			MEMREF_TABLE,
			MEMREF_MEM,
			MEMREF_GLOBAL,
			MEMREF_RESOLVE_INDIRECT_CALL,
			MEMREF_TRAP,
		} type;
		size_t code_offset;
		size_t idx;
	} *elts;
};

char *wasmjit_compile_function(const struct FuncType *func_types,
			       const struct ModuleTypes *module_types,
			       const struct FuncType *type,
			       const struct CodeSectionCode *code,
			       struct MemoryReferences *memrefs,
			       size_t *out_size);

char *wasmjit_compile_hostfunc(struct FuncType *type,
			       void *hostfunc,
			       void *funcinst_ptr,
			       size_t *out_size);

char *wasmjit_compile_invoker(struct FuncType *type,
			      void *compiled_code,
			      size_t *out_size);

#endif
