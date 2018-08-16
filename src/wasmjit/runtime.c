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

#include <wasmjit/runtime.h>

#include <wasmjit/ast.h>
#include <wasmjit/util.h>

#include <assert.h>
#include <stdlib.h>

DEFINE_VECTOR_GROW(func_types, struct FuncTypeVector);

/* platform specific */

#include <sys/mman.h>

void *wasmjit_map_code_segment(size_t code_size)
{
	void *newcode;
	newcode = mmap(NULL, code_size, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (newcode == MAP_FAILED)
		return NULL;
	return newcode;
}

int wasmjit_mark_code_segment_executable(void *code, size_t code_size)
{
	return !mprotect(code, code_size, PROT_READ | PROT_EXEC);
}


int wasmjit_unmap_code_segment(void *code, size_t code_size)
{
	return !munmap(code, code_size);
}

/* end platform specific */

void wasmjit_free_module_inst(struct ModuleInst *module)
{
	size_t i;
	for (i = 0; i < module->funcs.n_elts; ++i) {
		if (module->funcs.elts[i]->compiled_code)
			wasmjit_unmap_code_segment(module->funcs.elts[i]->compiled_code,
						   module->funcs.elts[i]->compiled_code_size);
		free(module->funcs.elts[i]);
	}
	for (i = 0; i < module->tables.n_elts; ++i) {
		free(module->tables.elts[i]->data);
		free(module->tables.elts[i]);
	}
	for (i = 0; i < module->mems.n_elts; ++i) {
		free(module->mems.elts[i]->data);
		free(module->mems.elts[i]);
	}
	for (i = 0; i < module->globals.n_elts; ++i) {
		free(module->globals.elts[i]);
	}
	for (i = 0; i < module->exports.n_elts; ++i) {
		if (module->exports.elts[i].name)
			free(module->exports.elts[i].name);
	}
	free(module);
}

int wasmjit_typecheck_func(const struct FuncType *type,
			   const struct FuncInst *funcinst)
{
	return wasmjit_typelist_equal(type->n_inputs, type->input_types,
				      funcinst->type.n_inputs,
				      funcinst->type.input_types) &&
		wasmjit_typelist_equal(FUNC_TYPE_N_OUTPUTS(type),
				       FUNC_TYPE_OUTPUT_TYPES(type),
				       FUNC_TYPE_N_OUTPUTS(&funcinst->type),
				       FUNC_TYPE_OUTPUT_TYPES(&funcinst->type));
}

int wasmjit_typecheck_table(const struct TableType *type,
			    const struct TableInst *tableinst)
{
	return (tableinst->elemtype == type->elemtype &&
		tableinst->length >= type->limits.min &&
		(!type->limits.max ||
		 (type->limits.max && tableinst->max &&
		  tableinst->max <= type->limits.max)));
}

int wasmjit_typecheck_memory(const struct MemoryType *type,
			     const struct MemInst *meminst)
{
	size_t msize = meminst->size / WASM_PAGE_SIZE;
	size_t mmax = meminst->max / WASM_PAGE_SIZE;
	return (msize >= type->limits.min &&
		(!type->limits.max ||
		 (type->limits.max && mmax &&
		  mmax <= type->limits.max)));
}

int wasmjit_typecheck_global(const struct GlobalType *globaltype,
			     const struct GlobalInst *globalinst)
{
	return globalinst->value.type == globaltype->valtype &&
		globalinst->mut == globaltype->mut;
}

int _wasmjit_create_func_type(struct FuncType *ft,
			      size_t n_inputs,
			      wasmjit_valtype_t *input_types,
			      size_t n_outputs,
			      wasmjit_valtype_t *output_types)
{
	assert(n_outputs <= 1);
	assert(n_inputs <= sizeof(ft->input_types) / sizeof(ft->input_types[0]));
	memset(ft, 0, sizeof(*ft));

	ft->n_inputs = n_inputs;
	memcpy(ft->input_types, input_types, sizeof(input_types[0]) * n_inputs);

	if (n_outputs) {
		ft->output_type = output_types[0];
	} else {
		ft->output_type = VALTYPE_NULL;
	}

	return 1;
}

__attribute__((noreturn))
static void trap(void)
{
	asm("int $4");
	__builtin_unreachable();
}

void *wasmjit_resolve_indirect_call(const struct TableInst *tableinst,
				    const struct FuncType *expected_type,
				    uint32_t idx)
{
	struct FuncInst *funcinst;

	if (idx >= tableinst->length)
		trap();

	funcinst = tableinst->data[idx];
	if (!funcinst)
		trap();

	if (!wasmjit_typecheck_func(expected_type, funcinst))
		trap();

	return funcinst->compiled_code;
}
