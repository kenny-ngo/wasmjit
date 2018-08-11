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

__attribute__((noreturn))
static void ltrap(void)
{
	__builtin_trap();
	__builtin_unreachable();
}

void wasmjit_init_static_module(struct StaticModuleInst *smi)
{
	size_t i;

	/* type-check all module objects */

	for (i = 0; i < smi->n_funcs; ++i) {
		struct FuncReference *ref = &smi->funcs[i];
		if (ref->inst->type != IMPORT_DESC_TYPE_FUNC)
			ltrap();
		if (!wasmjit_typecheck_func(&ref->expected_type, &ref->inst->u.func))
			ltrap();
	}

	for (i = 0; i < smi->n_tables; ++i) {
		struct TableReference *ref = &smi->tables[i];
		if (ref->inst->type != IMPORT_DESC_TYPE_TABLE)
			ltrap();
		if (!wasmjit_typecheck_table(&ref->expected_type, &ref->inst->u.table))
			ltrap();
	}

	for (i = 0; i < smi->n_mems; ++i) {
		struct MemReference *ref = &smi->mems[i];
		if (ref->inst->type != IMPORT_DESC_TYPE_MEM)
			ltrap();
		if (!wasmjit_typecheck_memory(&ref->expected_type, &ref->inst->u.mem))
			ltrap();
	}

	for (i = 0; i < smi->n_mems; ++i) {
		struct GlobalReference *ref = &smi->globals[i];
		if (ref->inst->type != IMPORT_DESC_TYPE_GLOBAL)
			ltrap();
		if (!wasmjit_typecheck_global(&ref->expected_type, &ref->inst->u.global.global))
			ltrap();
	}

	/* init globals */
	for (i = smi->n_imported_globals; i < smi->n_globals; ++i) {
		struct StaticGlobalInst *my_global = &smi->globals[i].inst->u.global;
		struct StaticGlobalInst *gitr = my_global;
		while (gitr->init_type == GLOBAL_GLOBAL_INIT) {
			gitr = &gitr->init.global->u.global;
		}
		my_global->global.value = gitr->init.constant;
	}

	for (i = 0; i < smi->n_elements; ++i) {
		struct ElementInst *element = &smi->elements[i];
		struct TableReference *ref = &smi->tables[element->tableidx];
		struct TableInst *table = &ref->inst->u.table;
		size_t j;
		uint32_t offset;

		if (element->offset_source_type == GLOBAL_CONST_INIT) {
			if (element->offset.constant.type != VALTYPE_I32)
				ltrap();
			offset = element->offset.constant.data.i32;
		} else {
			struct StaticGlobalInst *gitr = &element->offset.global->u.global;
			while (gitr->init_type == GLOBAL_GLOBAL_INIT) {
				gitr = &gitr->init.global->u.global;
			}
			if (gitr->init.constant.type != VALTYPE_I32)
				ltrap();
			offset = gitr->init.constant.data.i32;
		}

		if (element->n_funcidxs + offset > table->length)
			ltrap();

		for (j = 0; j < element->n_funcidxs; ++j) {
			table->data[offset + j] = &smi->funcs[element->funcidxs[j]].inst->u.func;
		}
	}

	for (i = 0; i < smi->n_datas; ++i) {
		struct DataInst *data = &smi->datas[i];
		struct MemReference *ref = &smi->mems[data->memidx];
		struct MemInst *mem = &ref->inst->u.mem;

		uint32_t offset;

		if (data->offset_source_type == GLOBAL_CONST_INIT) {
			if (data->offset.constant.type != VALTYPE_I32)
				ltrap();
			offset = data->offset.constant.data.i32;
		} else {
			struct StaticGlobalInst *gitr = &data->offset.global->u.global;
			while (gitr->init_type == GLOBAL_GLOBAL_INIT) {
				gitr = &gitr->init.global->u.global;
			}
			if (gitr->init.constant.type != VALTYPE_I32)
				ltrap();
			offset = gitr->init.constant.data.i32;
		}

		if (data->buf_size + offset > mem->size)
			ltrap();

		memcpy(mem->data + offset,
		       data->buf,
		       data->buf_size);
	}

	if (smi->start_func) {
		smi->start_func();
	}
}
