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
#include <wasmjit/emscripten_runtime.h>
#include <wasmjit/util.h>
#include <wasmjit/compile.h>

#include <wasmjit/sys.h>

struct EmscriptenContext *wasmjit_emscripten_get_context(struct ModuleInst *module_inst)
{
	return module_inst->private_data;
}

struct NamedModule *wasmjit_instantiate_emscripten_runtime(size_t tablemin,
							   size_t tablemax,
							   size_t *amt)
{
	struct {
		size_t n_elts;
		struct NamedModule *elts;
	} modules = {0, NULL};
	struct FuncInst *tmp_func = NULL;
	struct FuncInst **tmp_table_buf = NULL;
	struct TableInst *tmp_table = NULL;
	char *tmp_mem_buf = NULL;
	struct MemInst *tmp_mem = NULL;
	struct GlobalInst *tmp_global = NULL;
	struct ModuleInst *module = NULL;
	struct NamedModule *ret;
	void *tmp_unmapped = NULL;

	/* TODO: add exports */

#define LVECTOR_GROW(sstack, n_elts)		      \
	do {					      \
		if (!VECTOR_GROW((sstack), (n_elts))) \
			goto error;		      \
	}					      \
	while (0)

#define START_MODULE()						\
	{							\
		module = calloc(1, sizeof(struct ModuleInst));	\
		if (!module)					\
			goto error;				\
	}

#define STR(x) #x
#define XSTR(x) STR(x)

#define END_MODULE()							\
	{								\
		if (!strcmp(XSTR(CURRENT_MODULE), "env")) {		\
			module->private_data = calloc(1, sizeof(struct EmscriptenContext)); \
			if (!module->private_data)			\
				goto error;				\
			module->free_private_data = &free;		\
		}							\
		LVECTOR_GROW(&modules, 1);				\
		modules.elts[modules.n_elts - 1].name = strdup(XSTR(CURRENT_MODULE)); \
		modules.elts[modules.n_elts - 1].module = module;	\
		module = NULL;						\
	}

#define START_TABLE_DEFS() \
	DEFINE_WASM_TABLE(table, ELEMTYPE_ANYFUNC, tablemin, tablemax)

#define END_TABLE_DEFS()
#define START_MEMORY_DEFS()
#define END_MEMORY_DEFS()
#define START_GLOBAL_DEFS()
#define END_GLOBAL_DEFS()
#define START_FUNCTION_DEFS()
#define END_FUNCTION_DEFS()

#define DEFINE_WASM_FUNCTION(_name, _fptr, _output, n, ...)	  \
	{							  \
		wasmjit_valtype_t inputs[] = { __VA_ARGS__ };		\
		tmp_func = calloc(1, sizeof(struct FuncInst));		\
		if (!tmp_func)						\
			goto error;					\
		tmp_func->module_inst = module;				\
		tmp_func->type.n_inputs = ARRAY_LEN(inputs);		\
		memcpy(tmp_func->type.input_types, inputs, ARRAY_LEN(inputs)); \
		tmp_func->type.output_type = _output;			\
		if (tmp_unmapped)					\
			free(tmp_unmapped);				\
		tmp_unmapped =						\
			wasmjit_compile_hostfunc(&tmp_func->type, _fptr, \
						 tmp_func,		\
						 &tmp_func->compiled_code_size); \
		if (!tmp_unmapped)					\
			goto error;					\
		tmp_func->compiled_code =				\
			wasmjit_map_code_segment(tmp_func->compiled_code_size);	\
		if (!tmp_func->compiled_code)				\
			goto error;					\
		memcpy(tmp_func->compiled_code, tmp_unmapped,		\
		       tmp_func->compiled_code_size);			\
		if (!wasmjit_mark_code_segment_executable(tmp_func->compiled_code,\
							  tmp_func->compiled_code_size)) \
			goto error;					\
		if (tmp_unmapped)					\
			free(tmp_unmapped);				\
		tmp_unmapped =						\
			wasmjit_compile_invoker(&tmp_func->type,	\
						tmp_func->compiled_code, \
						&tmp_func->invoker_size); \
		if (!tmp_unmapped)					\
			goto error;					\
		tmp_func->invoker =					\
			wasmjit_map_code_segment(tmp_func->invoker_size); \
		memcpy(tmp_func->invoker, tmp_unmapped,			\
		       tmp_func->invoker_size);				\
		if (!wasmjit_mark_code_segment_executable(tmp_func->invoker, \
							  tmp_func->invoker_size)) \
			goto error;					\
		LVECTOR_GROW(&module->funcs, 1);			\
		module->funcs.elts[module->funcs.n_elts - 1] = tmp_func; \
		tmp_func = NULL;					\
									\
		LVECTOR_GROW(&module->exports, 1);			\
		module->exports.elts[module->exports.n_elts - 1].name = strdup(#_name); \
		module->exports.elts[module->exports.n_elts - 1].type = IMPORT_DESC_TYPE_FUNC; \
		module->exports.elts[module->exports.n_elts - 1].value.func = module->funcs.elts[module->funcs.n_elts - 1]; \
	}

#define DEFINE_WASM_TABLE(_name, _elemtype, _min, _max)		\
	{								\
		tmp_table_buf = calloc(_min, sizeof(tmp_table_buf[0]));	\
		if ((_min) && !tmp_table_buf)				\
			goto error;					\
		tmp_table = calloc(1, sizeof(struct TableInst));	\
		if (!tmp_table)						\
			goto error;					\
		tmp_table->data = tmp_table_buf;			\
		tmp_table_buf = NULL;					\
		tmp_table->elemtype = (_elemtype);			\
		tmp_table->length = (_min);				\
		tmp_table->max = (_max);				\
		LVECTOR_GROW(&module->tables, 1);			\
		module->tables.elts[module->tables.n_elts - 1] = tmp_table; \
		tmp_table = NULL;					\
									\
		LVECTOR_GROW(&module->exports, 1);			\
		module->exports.elts[module->exports.n_elts - 1].name = strdup(#_name); \
		module->exports.elts[module->exports.n_elts - 1].type = IMPORT_DESC_TYPE_TABLE; \
		module->exports.elts[module->exports.n_elts - 1].value.table = module->tables.elts[module->tables.n_elts - 1]; \
	}

#define DEFINE_WASM_MEMORY(_name, _min, _max)	\
	{						\
		tmp_mem_buf = calloc((_min) * WASM_PAGE_SIZE, 1);	\
		if ((_min) && !tmp_mem_buf)				\
			goto error;				\
		tmp_mem = calloc(1, sizeof(struct MemInst));	\
		tmp_mem->data = tmp_mem_buf;			\
		tmp_mem_buf = NULL;				\
		tmp_mem->size = (_min) * WASM_PAGE_SIZE;	\
		tmp_mem->max = (_max) * WASM_PAGE_SIZE;		\
		LVECTOR_GROW(&module->mems, 1);			\
		module->mems.elts[module->mems.n_elts - 1] = tmp_mem; \
		tmp_mem = NULL;					\
									\
		LVECTOR_GROW(&module->exports, 1);			\
		module->exports.elts[module->exports.n_elts - 1].name = strdup(#_name); \
		module->exports.elts[module->exports.n_elts - 1].type = IMPORT_DESC_TYPE_MEM; \
		module->exports.elts[module->exports.n_elts - 1].value.mem = module->mems.elts[module->mems.n_elts - 1]; \
	}

#define DEFINE_WASM_GLOBAL(_name, _init, _type, _member, _mut)	\
	{								\
		tmp_global = calloc(1, sizeof(struct GlobalInst));	\
		if (!tmp_global)					\
			goto error;					\
		tmp_global->value.type = (_type);			\
		tmp_global->value.data._member = (_init);		\
		tmp_global->mut = (_mut);				\
		LVECTOR_GROW(&module->globals, 1);			\
		module->globals.elts[module->globals.n_elts - 1] = tmp_global; \
		tmp_global = NULL;					\
									\
		LVECTOR_GROW(&module->exports, 1);			\
		module->exports.elts[module->exports.n_elts - 1].name = strdup(#_name); \
		module->exports.elts[module->exports.n_elts - 1].type = IMPORT_DESC_TYPE_GLOBAL; \
		module->exports.elts[module->exports.n_elts - 1].value.global = module->globals.elts[module->globals.n_elts - 1]; \
	}

#include <wasmjit/emscripten_runtime_def.h>

	if (0) {
	error:
		ret = NULL;

		if (modules.elts) {
			size_t i;
			for (i = 0; i < modules.n_elts; i++) {
				struct NamedModule *nm = &modules.elts[i];
				free(nm->name);
				wasmjit_free_module_inst(nm->module);
			}
			free(modules.elts);
		}
	}
	else {
		ret = modules.elts;
		if (amt) {
			*amt = modules.n_elts;
		}
	}

	if (tmp_unmapped)
		free(tmp_unmapped);
	if (module) {
		wasmjit_free_module_inst(module);
	}
	if (tmp_func) {
		wasmjit_free_func_inst(tmp_func);
	}
	if (tmp_table_buf)
		free(tmp_table_buf);
	if (tmp_table) {
		free(tmp_table->data);
		free(tmp_table);
	}
	if (tmp_mem_buf)
		free(tmp_mem_buf);
	if (tmp_mem) {
		free(tmp_mem->data);
		free(tmp_mem);
	}
	if (tmp_global)
		free(tmp_global);

	return ret;
}
