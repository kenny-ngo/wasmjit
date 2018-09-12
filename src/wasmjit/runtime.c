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

#include <wasmjit/sys.h>

DEFINE_VECTOR_GROW(func_types, struct FuncTypeVector);


/* end platform specific */

union ExportPtr wasmjit_get_export(const struct ModuleInst *module_inst,
				   const char *name,
				   wasmjit_desc_t type) {
	size_t i;
	union ExportPtr ret;

	for (i = 0; i < module_inst->exports.n_elts; ++i) {
		if (strcmp(module_inst->exports.elts[i].name, name))
			continue;

		if (module_inst->exports.elts[i].type != type)
			break;

		return module_inst->exports.elts[i].value;
	}

	switch (type) {
	case IMPORT_DESC_TYPE_FUNC:
		ret.func = NULL;
		break;
	case IMPORT_DESC_TYPE_TABLE:
		ret.table = NULL;
		break;
	case IMPORT_DESC_TYPE_MEM:
		ret.mem = NULL;
		break;
	case IMPORT_DESC_TYPE_GLOBAL:
		ret.global = NULL;
		break;
	}

	return ret;
}

void wasmjit_free_func_inst(struct FuncInst *funcinst)
{
	if (funcinst->invoker)
		wasmjit_unmap_code_segment(funcinst->invoker,
					   funcinst->invoker_size);
	if (funcinst->compiled_code)
		wasmjit_unmap_code_segment(funcinst->compiled_code,
					   funcinst->compiled_code_size);
	free(funcinst);
}

void wasmjit_free_module_inst(struct ModuleInst *module)
{
	size_t i;
	if (module->free_private_data)
		module->free_private_data(module->private_data);
	free(module->types.elts);
	for (i = module->n_imported_funcs; i < module->funcs.n_elts; ++i) {
		wasmjit_free_func_inst(module->funcs.elts[i]);
	}
	free(module->funcs.elts);
	for (i = module->n_imported_tables; i < module->tables.n_elts; ++i) {
		free(module->tables.elts[i]->data);
		free(module->tables.elts[i]);
	}
	free(module->tables.elts);
	for (i = module->n_imported_mems; i < module->mems.n_elts; ++i) {
		free(module->mems.elts[i]->data);
		free(module->mems.elts[i]);
	}
	free(module->mems.elts);
	for (i = module->n_imported_globals; i < module->globals.n_elts; ++i) {
		free(module->globals.elts[i]);
	}
	free(module->globals.elts);
	for (i = 0; i < module->exports.n_elts; ++i) {
		if (module->exports.elts[i].name)
			free(module->exports.elts[i].name);
	}
	free(module->exports.elts);
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

void _wasmjit_create_func_type(struct FuncType *ft,
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
}

struct FuncInst *wasmjit_resolve_indirect_call(const struct TableInst *tableinst,
					       const struct FuncType *expected_type,
					       uint32_t idx)
{
	struct FuncInst *funcinst;

	if (idx >= tableinst->length)
		wasmjit_trap(WASMJIT_TRAP_TABLE_OVERFLOW);

	funcinst = tableinst->data[idx];
	if (!funcinst)
		wasmjit_trap(WASMJIT_TRAP_UNINITIALIZED_TABLE_ENTRY);

	if (!wasmjit_typecheck_func(expected_type, funcinst))
		wasmjit_trap(WASMJIT_TRAP_MISMATCHED_TYPE);

	return funcinst;
}

int wasmjit_invoke_function(struct FuncInst *funcinst,
			    union ValueUnion *values,
			    union ValueUnion *out)
{
	union ValueUnion lout;
#ifndef __x86_64__
#error Only works on x86_64
#endif
	lout = funcinst->invoker(values);
	if (out)
		*out = lout;
	return 0;
}
