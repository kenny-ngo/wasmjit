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
#include <wasmjit/sys.h>

struct Value {
	wasmjit_valtype_t type;
	union ValueUnion {
		uint32_t i32;
		uint64_t i64;
		float f32;
		double f64;
		struct {} null;
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
	union ValueUnion (*invoker)(union ValueUnion *);
	size_t invoker_size;
	size_t stack_usage;
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
	union ExportPtr {
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
	size_t n_imported_funcs, n_imported_tables,
		n_imported_mems, n_imported_globals;
};

DECLARE_VECTOR_GROW(func_types, struct FuncTypeVector);

struct NamedModule {
	char *name;
	struct ModuleInst *module;
};

#define IS_HOST(funcinst) ((funcinst)->host_function)

#define WASM_PAGE_SIZE ((size_t) (64 * 1024))

void _wasmjit_create_func_type(struct FuncType *ft,
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

enum {
	WASMJIT_TRAP_UNREACHABLE,
	WASMJIT_TRAP_TABLE_OVERFLOW,
	WASMJIT_TRAP_UNINITIALIZED_TABLE_ENTRY,
	WASMJIT_TRAP_MISMATCHED_TYPE,
	WASMJIT_TRAP_MEMORY_OVERFLOW,
	WASMJIT_TRAP_ABORT,
	WASMJIT_TRAP_STACK_OVERFLOW,
};

__attribute__ ((unused))
static const char *wasmjit_trap_reason_to_string(int reason) {
	const char *msg;
	switch (reason) {
	case WASMJIT_TRAP_UNREACHABLE:
		msg = "unreachable instruction hit";
		break;
	case WASMJIT_TRAP_TABLE_OVERFLOW:
		msg = "table overflow";
		break;
	case WASMJIT_TRAP_UNINITIALIZED_TABLE_ENTRY:
		msg = "uninitialized table entry";
		break;
	case WASMJIT_TRAP_MISMATCHED_TYPE:
		msg = "mismatched type";
		break;
	case WASMJIT_TRAP_MEMORY_OVERFLOW:
		msg = "memory overflow";
		break;
	case WASMJIT_TRAP_STACK_OVERFLOW:
		msg = "stack overflow";
		break;
	default:
		assert(0);
		__builtin_unreachable();
	}
	return msg;
}

struct FuncInst *wasmjit_resolve_indirect_call(const struct TableInst *tableinst,
					       const struct FuncType *expected_type,
					       uint32_t idx);
void wasmjit_trap(int reason) __attribute__((noreturn));
void *wasmjit_stack_top(void);

void wasmjit_free_func_inst(struct FuncInst *funcinst);
void wasmjit_free_module_inst(struct ModuleInst *module);

void *wasmjit_map_code_segment(size_t code_size);
int wasmjit_mark_code_segment_executable(void *code, size_t code_size);
int wasmjit_unmap_code_segment(void *code, size_t code_size);

int wasmjit_set_stack_top(void *stack_top);
int wasmjit_set_jmp_buf(jmp_buf *jmpbuf);
jmp_buf *wasmjit_get_jmp_buf(void);

union ExportPtr wasmjit_get_export(const struct ModuleInst *, const char *name, wasmjit_desc_t type);

int wasmjit_invoke_function(struct FuncInst *funcinst, union ValueUnion *values,
			    union ValueUnion *out);

#endif
