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

#ifndef __WASMJIT__RUNTIME_H__
#define __WASMJIT__RUNTIME_H__

#include <wasmjit/ast.h>
#include <wasmjit/vector.h>

#include <stddef.h>
#include <stdint.h>

struct Value {
	wasmjit_valtype_t type;
	union ValueUnion {
		uint32_t i32;
		uint64_t i64;
		float f32;
		double f64;
	} data;
};

struct FuncInst {
	struct ModuleInst *module_inst;
	/*
	  the function signature of compiled_code
	  pointers mirrors that of the WASM input
	  types.
	*/
	void *compiled_code;
	size_t compiled_code_size;
	/*
	  host_function pointers are like
	  compiled_code pointers except their
	  argument list is followed by a struct
	  FuncInst *, the runtime is expected to be
	  able to translate between the two ABIs
	*/
	void *host_function;
	struct FuncType type;
};

struct TableInst {
	struct FuncInst **data;
	unsigned elemtype;
	size_t length;
	size_t max;
};

struct MemInst {
	char *data;
	size_t size;
	size_t max; /* max of 0 means no max */
};

struct GlobalInst {
	struct Value value;
	unsigned mut;
};

struct Export {
	char *name;
	wasmjit_desc_t type;
	union {
		struct FuncInst *func;
		struct TableInst *table;
		struct MemInst *mem;
		struct GlobalInst *global;
	} value;
};

struct ModuleInst {
	struct FuncTypeVector {
		size_t n_elts;
		struct FuncType *elts;
	} types;
	DEFINE_ANON_VECTOR(struct FuncInst *) funcs;
	DEFINE_ANON_VECTOR(struct TableInst *) tables;
	DEFINE_ANON_VECTOR(struct MemInst *) mems;
	DEFINE_ANON_VECTOR(struct GlobalInst *) globals;
	DEFINE_ANON_VECTOR(struct Export) exports;
};

DECLARE_VECTOR_GROW(func_types, struct FuncTypeVector);

struct NamedModule {
	char *name;
	struct ModuleInst *module;
};

#define IS_HOST(funcinst) ((funcinst)->host_function)

#define WASM_PAGE_SIZE ((size_t) (64 * 1024))

int _wasmjit_create_func_type(struct FuncType *ft,
			      size_t n_inputs,
			      wasmjit_valtype_t *input_types,
			      size_t n_outputs, wasmjit_valtype_t *output_types);

int wasmjit_typecheck_func(const struct FuncType *expected_type,
			   const struct FuncInst *func);

int wasmjit_typecheck_table(const struct TableType *expected_type,
			    const struct TableInst *table);

int wasmjit_typecheck_memory(const struct MemoryType *expected_type,
			     const struct MemInst *mem);

int wasmjit_typecheck_global(const struct GlobalType *expected_type,
			     const struct GlobalInst *mem);

__attribute__ ((unused))
static int wasmjit_typelist_equal(size_t nelts, const wasmjit_valtype_t *elts,
				  size_t onelts, const wasmjit_valtype_t *oelts)
{
	size_t i;
	if (nelts != onelts) return 0;
	for (i = 0; i < nelts; ++i) {
		if (elts[i] != oelts[i]) return 0;
	}
	return 1;
}

void *wasmjit_resolve_indirect_call(const struct TableInst *tableinst,
				    const struct FuncType *expected_type,
				    uint32_t idx);

void wasmjit_free_module_inst(struct ModuleInst *module);

int wasmjit_unmap_code_segment(void *code, size_t code_size);

#endif
