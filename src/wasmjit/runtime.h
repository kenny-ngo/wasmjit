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

struct NamedModule {
	char *name;
	struct ModuleInst *module;
};

typedef size_t wasmjit_addr_t;
#define INVALID_ADDR ((wasmjit_addr_t) -1)

struct Addrs {
	size_t n_elts;
	wasmjit_addr_t *elts;
};

DECLARE_VECTOR_GROW(addrs, struct Addrs);

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

struct Value {
	wasmjit_valtype_t type;
	union ValueUnion {
		uint32_t i32;
		uint64_t i64;
		float f32;
		double f64;
	} data;
};

#define IS_HOST(funcinst) ((funcinst)->host_function)

struct Store {
	struct ModuleInstances {
		size_t n_elts;
		struct ModuleInst *elts;
	} modules;
	struct Namespace {
		size_t n_elts;
		struct NamespaceEntry {
			char *module_name;
			char *name;
			unsigned type;
			wasmjit_addr_t addr;
		} *elts;
	} names;
	struct StoreFuncs {
		wasmjit_addr_t n_elts;
		struct FuncInst {
			struct ModuleInst *module_inst;
			size_t code_length;
			struct Instr *code;
			size_t n_locals;
			struct {
				uint32_t count;
				uint8_t valtype;
			} *locals;
			/*
			  the function signature of compiled_code
			  pointers mirrors that of the WASM input
			  types.
			 */
			void *compiled_code;
			/*
			   host_function pointers are like
			   compiled_code pointers except their
			   argument list is followed by a struct
			   FuncInst *, the runtime is expected to be
			   able to translate between the two ABIs
			*/
			void *host_function;
			struct FuncType type;
		} *elts;
	} funcs;
	struct TableFuncs {
		wasmjit_addr_t n_elts;
		struct TableInst {
			struct FuncInst **data;
			unsigned elemtype;
			size_t length;
			size_t max;
		} *elts;
	} tables;
	struct StoreMems {
		wasmjit_addr_t n_elts;
		struct MemInst {
			char *data;
			size_t size;
			size_t max; /* max of 0 means no max */
		} *elts;
	} mems;
	struct StoreGlobals {
		wasmjit_addr_t n_elts;
		struct GlobalInst {
			struct Value value;
			unsigned mut;
		} *elts;
	} globals;
	struct Addrs startfuncs;
};

DECLARE_VECTOR_GROW(store_module_insts, struct ModuleInstances);
DECLARE_VECTOR_GROW(store_names, struct Namespace);
DECLARE_VECTOR_GROW(store_funcs, struct StoreFuncs);
DECLARE_VECTOR_GROW(store_tables, struct TableFuncs);
DECLARE_VECTOR_GROW(store_mems, struct StoreMems);
DECLARE_VECTOR_GROW(store_globals, struct StoreGlobals);

#define WASM_PAGE_SIZE ((size_t) (64 * 1024))

int _wasmjit_create_func_type(struct FuncType *ft,
			      size_t n_inputs,
			      wasmjit_valtype_t *input_types,
			      size_t n_outputs, wasmjit_valtype_t *output_types);

wasmjit_addr_t _wasmjit_add_memory_to_store(struct Store *store,
					    size_t size, size_t max);
wasmjit_addr_t _wasmjit_add_function_to_store(struct Store *store,
					      struct ModuleInst *module_inst,
					      void *code,
					      size_t n_inputs,
					      wasmjit_valtype_t *input_types,
					      size_t n_outputs,
					      wasmjit_valtype_t *output_types);
wasmjit_addr_t _wasmjit_add_table_to_store(struct Store *store,
					   unsigned elemtype,
					   size_t length,
					   size_t max);
wasmjit_addr_t _wasmjit_add_global_to_store(struct Store *store,
					    struct Value value,
					    unsigned mut);
int _wasmjit_add_to_namespace(struct Store *store,
			      const char *module_name,
			      const char *name,
			      unsigned type,
			      wasmjit_addr_t addr);

int wasmjit_import_function(struct Store *store,
			    const char *module_name,
			    const char *name,
			    void *funcaddr,
			    size_t n_inputs,
			    wasmjit_valtype_t *input_types,
			    size_t n_outputs, wasmjit_valtype_t *output_types);

wasmjit_addr_t wasmjit_import_memory(struct Store *store,
				     const char *module_name,
				     const char *name,
				     size_t size, size_t max);

int wasmjit_import_table(struct Store *store,
			 const char *module_name,
			 const char *name,
			 unsigned elemtype,
			 size_t length,
			 size_t max);

int wasmjit_import_global(struct Store *store,
			  const char *module_name,
			  const char *name,
			  struct Value value,
			  unsigned mut);

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

#endif
