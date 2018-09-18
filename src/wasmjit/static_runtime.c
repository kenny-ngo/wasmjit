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

#include <wasmjit/static_runtime.h>

#include <wasmjit/runtime.h>

#include <stdint.h>
#include <stdio.h>

int wasmjit_unmap_code_segment(void *code, size_t code_size)
{
	(void)code;
	(void)code_size;
	return 1;
}

__attribute__((noreturn))
void wasmjit_trap(int reason)
{
	fprintf(stderr, "TRAP: %s\n",
		wasmjit_trap_reason_to_string(reason));
	exit(-1);
}

void wasmjit_init_static_module(struct StaticModuleInst *smi)
{
	size_t i;

	if (smi->initted) return;

	/* type-check all imports */

	for (i = 0; i < smi->func_types.n_elts; ++i) {
		struct FuncInst *ref = smi->module.funcs.elts[i];
		struct FuncType *expected_type = &smi->func_types.elts[i];
		if (!wasmjit_typecheck_func(expected_type, ref))
			wasmjit_trap(WASMJIT_TRAP_MISMATCHED_TYPE);
	}

	for (i = 0; i < smi->table_types.n_elts; ++i) {
		struct TableInst *ref = smi->module.tables.elts[i];
		struct TableType *expected_type = &smi->table_types.elts[i];
		if (!wasmjit_typecheck_table(expected_type, ref))
			wasmjit_trap(WASMJIT_TRAP_MISMATCHED_TYPE);
	}

	for (i = 0; i < smi->mem_types.n_elts; ++i) {
		struct MemInst *ref = smi->module.mems.elts[i];
		struct MemoryType *expected_type = &smi->mem_types.elts[i];
		if (!wasmjit_typecheck_memory(expected_type, ref))
			wasmjit_trap(WASMJIT_TRAP_MISMATCHED_TYPE);
	}

	for (i = 0; i < smi->global_types.n_elts; ++i) {
		struct GlobalInst *ref = smi->module.globals.elts[i];
		struct GlobalType *expected_type = &smi->global_types.elts[i];
		if (!wasmjit_typecheck_global(expected_type, ref))
			wasmjit_trap(WASMJIT_TRAP_MISMATCHED_TYPE);
	}

	/* init non-imported globals */
	for (i = 0; i < smi->global_inits.n_elts; ++i) {
		// smi->global_types.n_elts
		struct GlobalInst *my_global = smi->module.globals.elts[smi->global_types.n_elts + i];
		struct GlobalInit *gitr = &smi->global_inits.elts[i];

		if (gitr->init_type == GLOBAL_GLOBAL_INIT) {
			/* TODO: check types */
			wasmjit_init_static_module(gitr->init.parent.module);
			my_global->value = gitr->init.parent.global->value;
		} else {
			my_global->value = gitr->init.constant;
		}
	}

	for (i = 0; i < smi->elements.n_elts; ++i) {
		struct ElementInst *element = &smi->elements.elts[i];
		struct TableInst *table = smi->module.tables.elts[element->tableidx];
		size_t j;
		struct Value offset;

		if (element->offset_source_type == GLOBAL_CONST_INIT) {
			offset = element->offset.constant;
		} else {
			offset = element->offset.global->value;
		}

		if (offset.type != VALTYPE_I32)
			wasmjit_trap(WASMJIT_TRAP_MISMATCHED_TYPE);

		if (element->n_funcidxs + offset.data.i32 > table->length)
			wasmjit_trap(WASMJIT_TRAP_TABLE_OVERFLOW);

		for (j = 0; j < element->n_funcidxs; ++j) {
			table->data[offset.data.i32 + j] = smi->module.funcs.elts[element->funcidxs[j]];
		}
	}

	for (i = 0; i < smi->datas.n_elts; ++i) {
		struct DataInst *data = &smi->datas.elts[i];
		struct MemInst *mem = smi->module.mems.elts[data->memidx];
		struct Value offset;

		if (data->offset_source_type == GLOBAL_CONST_INIT) {
			offset = data->offset.constant;
		} else {
			offset = data->offset.global->value;
		}

		if (offset.type != VALTYPE_I32)
			wasmjit_trap(WASMJIT_TRAP_MISMATCHED_TYPE);

		if (data->buf_size + offset.data.i32 > mem->size)
			wasmjit_trap(WASMJIT_TRAP_MEMORY_OVERFLOW);

		memcpy(mem->data + offset.data.i32,
		       data->buf,
		       data->buf_size);
	}

	if (smi->start_func) {
		struct FuncType expected_type;
		void (*start)(void);
		_wasmjit_create_func_type(&expected_type, 0, NULL, 0, NULL);
		if (!wasmjit_typecheck_func(&expected_type, smi->start_func))
			wasmjit_trap(WASMJIT_TRAP_MISMATCHED_TYPE);
		start = smi->start_func->compiled_code;
		start();
	}

	smi->initted = 1;
}

int wasmjit_invoke_function(struct FuncInst *funcinst,
			    union ValueUnion *values,
			    union ValueUnion *out)
{
	union ValueUnion lout;
	lout = wasmjit_invoke_function_raw(funcinst, values);
	if (out)
		*out = lout;
	return 0;
}
