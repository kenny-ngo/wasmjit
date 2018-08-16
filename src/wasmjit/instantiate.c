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

#include <wasmjit/instantiate.h>

#include <wasmjit/parse.h>
#include <wasmjit/runtime.h>
#include <wasmjit/compile.h>
#include <wasmjit/util.h>
#include <wasmjit/execute.h>

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

static int func_sig_repr(char *why, size_t why_size, struct FuncType *type)
{
	int ret ;
	size_t k;

	ret = snprintf(why, why_size, "[");
	for (k = 0; k < type->n_inputs; ++k) {
		ret += snprintf(why + ret, why_size - ret, "%s,",
				wasmjit_valtype_repr(type->input_types[k]));
	}
	ret += snprintf(why + ret, why_size - ret, "] -> [");
	for (k = 0; k < FUNC_TYPE_N_OUTPUTS(type); ++k) {
		ret += snprintf(why + ret, why_size - ret, "%s,",
				wasmjit_valtype_repr(FUNC_TYPE_OUTPUT_IDX(type, k)));
	}
	ret += snprintf(why + ret, why_size - ret, "]");

	return ret;
}

static int read_constant_expression(struct ModuleInst *module_inst,
				    unsigned valtype, struct Value *value,
				    size_t n_instructions, struct Instr *instructions)
{
	if (n_instructions != 1)
		goto error;

	if (instructions[0].opcode == OPCODE_GET_GLOBAL) {
		struct GlobalInst *global =
			module_inst->globals.elts[instructions[0].data.get_global.globalidx];

		if (global->mut)
			goto error;

		if (global->value.type != valtype)
			goto error;

		*value = global->value;

		return 1;
	}

	value->type = valtype;
	switch (valtype) {
	case VALTYPE_I32:
		if (instructions[0].opcode != OPCODE_I32_CONST)
			goto error;
		value->data.i32 = instructions[0].data.i32_const.value;
		break;
	case VALTYPE_I64:
		if (instructions[0].opcode != OPCODE_I64_CONST)
			goto error;
		value->data.i64 = instructions[0].data.i64_const.value;
		break;
	case VALTYPE_F32:
		if (instructions[0].opcode != OPCODE_F32_CONST)
			goto error;
		value->data.f32 = instructions[0].data.f32_const.value;
		break;
	case VALTYPE_F64:
		if (instructions[0].opcode != OPCODE_F64_CONST)
			goto error;
		value->data.f64 = instructions[0].data.f64_const.value;
		break;
	default:
		assert(0);
		break;
	}

	return 1;

 error:
	return 0;
}

static int fill_module_types(struct ModuleInst *module_inst,
			     struct ModuleTypes *module_types)
{
	size_t i;

	module_types->functypes =
		calloc(module_inst->funcs.n_elts,
		       sizeof(module_types->functypes[0]));
	if (!module_types->functypes)
		goto error;

	module_types->tabletypes =
		calloc(module_inst->tables.n_elts,
		       sizeof(module_types->tabletypes[0]));
	if (!module_types->tabletypes)
		goto error;

	module_types->memorytypes =
		calloc(module_inst->mems.n_elts,
		       sizeof(module_types->memorytypes[0]));
	if (!module_types->memorytypes)
		goto error;

	module_types->globaltypes =
		calloc(module_inst->globals.n_elts,
		       sizeof(module_types->globaltypes[0]));
	if (!module_types->globaltypes)
		goto error;


	for (i = 0; i < module_inst->funcs.n_elts; ++i) {
		module_types->functypes[i] = module_inst->funcs.elts[i]->type;
	}

	for (i = 0; i < module_inst->tables.n_elts; ++i) {
		module_types->tabletypes[i].elemtype =
			module_inst->tables.elts[i]->elemtype;
		module_types->tabletypes[i].limits.min =
			module_inst->tables.elts[i]->length;
		module_types->tabletypes[i].limits.max =
			module_inst->tables.elts[i]->max;
	}

	for (i = 0; i < module_inst->mems.n_elts; ++i) {
		module_types->memorytypes[i].limits.min =
			module_inst->mems.elts[i]->size / WASM_PAGE_SIZE;
		module_types->memorytypes[i].limits.max =
			module_inst->mems.elts[i]->max / WASM_PAGE_SIZE;
	}

	for (i = 0; i < module_inst->globals.n_elts; ++i) {
		module_types->globaltypes[i].valtype =
			module_inst->globals.elts[i]->value.type;
		module_types->globaltypes[i].mut =
			module_inst->globals.elts[i]->mut;
	}

	return 1;

 error:
	return 0;
}

struct ModuleInst *wasmjit_instantiate(const struct Module *module,
				       size_t n_imports,
				       const struct NamedModule *imports,
				       char *why, size_t why_size)
{
	uint32_t i;
	struct ModuleInst *module_inst = NULL;
	size_t internal_func_idx;
	struct ModuleTypes module_types;
	struct FuncInst *tmp_func = NULL;
	struct TableInst *tmp_table = NULL;
	struct MemInst *tmp_mem = NULL;
	struct GlobalInst *tmp_global = NULL;
	void *unmapped = NULL, *mapped = NULL;
	struct MemoryReferences memrefs = {0, NULL};
	size_t code_size;

	memset(&module_types, 0, sizeof(module_types));
	module_inst = calloc(1, sizeof(*module_inst));

#define LVECTOR_GROW(sstack, n_elts)		      \
	do {					      \
		if (!VECTOR_GROW((sstack), (n_elts))) \
			goto error;		      \
	}					      \
	while (0)

	for (i = 0; i < module->type_section.n_types; ++i) {
		LVECTOR_GROW(&module_inst->types, 1);

		module_inst->types.elts[module_inst->types.n_elts - 1] =
			module->type_section.types[i];
	}

	/* load imports */
	for (i = 0; i < module->import_section.n_imports; ++i) {
		size_t j;
		struct ImportSectionImport *import =
		    &module->import_section.imports[i];
		struct ModuleInst *import_module = NULL;


		/* look for import module */
		for (j = 0; j < n_imports; ++j) {
			if (strcmp(imports[j].name, import->module))
				continue;
			import_module = imports[j].module;
		}

		if (!import_module) {
			if (why)
				snprintf(why, why_size,
					 "Couldn't find module: %s", import->module);
			goto error;
		}

		for (j = 0; j < import_module->exports.n_elts; ++j) {
			struct Export *export = &import_module->exports.elts[j];

			if (strcmp(export->name, import->name))
				continue;

			if (export->type != import->desc_type) {
				/* bad import type */
				if (why)
					snprintf(why, why_size, "bad import type");
				goto error;
			}

			switch (export->type) {
			case IMPORT_DESC_TYPE_FUNC: {
				struct FuncInst *funcinst = export->value.func;
				struct TypeSectionType *type = &module->type_section.types[import->desc.functypeidx];
				if (!wasmjit_typecheck_func(type, funcinst)) {
					int ret;
					ret = snprintf(why, why_size,
						       "Mismatched types for %s.%s: ",
						       import->module,
						       import->name);
					ret += func_sig_repr(why + ret, why_size - ret,
							     &funcinst->type);
					ret += snprintf(why + ret, why_size - ret,
							" vs ");
					ret += func_sig_repr(why + ret, why_size - ret,
							     type);
					goto error;
				}

				/* add funcinst to func table */
				LVECTOR_GROW(&module_inst->funcs, 1);
				module_inst->funcs.elts[module_inst->funcs.n_elts - 1] = funcinst;
				break;
			}
			case IMPORT_DESC_TYPE_TABLE: {
				struct TableInst *tableinst = export->value.table;

				if (!wasmjit_typecheck_table(&import->desc.tabletype,
							    tableinst)) {
					if (why)
						snprintf(why, why_size,
							 "Mismatched table import for import "
							 "%s.%s: {%u, %zu,%zu} vs {%u, %" PRIu32 ",%" PRIu32 "}",
							 import->module, import->name,
							 tableinst->elemtype,
							 tableinst->length,
							 tableinst->max,
							 import->desc.tabletype.elemtype,
							 import->desc.tabletype.limits.min,
							 import->desc.tabletype.limits.max);
					goto error;
				}

				/* add tableinst to table table */
				LVECTOR_GROW(&module_inst->tables, 1);
				module_inst->tables.elts[module_inst->tables.n_elts - 1] = tableinst;

				break;
			}
			case IMPORT_DESC_TYPE_MEM: {
				struct MemInst *meminst = export->value.mem;

				if (!wasmjit_typecheck_memory(&import->desc.memtype,
							      meminst)) {
					if (why)
						snprintf(why, why_size,
							 "Mismatched memory size for import "
							 "%s.%s: {%zu,%zu} vs {%" PRIu32 ",%" PRIu32 "}",
							 import->module, import->name,
							 meminst->size / WASM_PAGE_SIZE,
							 meminst->max / WASM_PAGE_SIZE,
							 import->desc.memtype.limits.min,
							 import->desc.memtype.limits.max);
					goto error;
				}

				/* add meminst to mems table */
				LVECTOR_GROW(&module_inst->mems, 1);
				module_inst->mems.elts[module_inst->mems.n_elts - 1] = meminst;

				break;
			}
			case IMPORT_DESC_TYPE_GLOBAL: {
				struct GlobalInst *globalinst = export->value.global;

				if (!wasmjit_typecheck_global(&import->desc.globaltype,
							      globalinst)) {
					if (why)
						snprintf(why, why_size,
							 "Mismatched global for import "
							 "%s.%s: %s%s vs %s%s",
							 import->module,
							 import->name,
							 wasmjit_valtype_repr(globalinst->value.type),
							 globalinst->mut ? " mut" : "",
							 wasmjit_valtype_repr(import->desc.globaltype.valtype),
							 import->desc.globaltype.mut ? " mut" : "");
					goto error;
				}

				/* add globalinst to globals tables */
				LVECTOR_GROW(&module_inst->globals, 1);
				module_inst->globals.elts[module_inst->globals.n_elts - 1] = globalinst;

				break;
			}
			default:
				assert(0);
				break;
			}

			break;
		}

		if (j == import_module->exports.n_elts) {
			/* couldn't find import */
			if (why)
				switch (import->desc_type) {
				case IMPORT_DESC_TYPE_FUNC: {
					int ret;
					struct TypeSectionType *type = &module->type_section.types[import->desc.functypeidx];
					ret = snprintf(why, why_size,
						       "couldn't find func import: %s.%s ",
						       import->module,
						       import->name);

					func_sig_repr(why + ret, why_size - ret,
						      type);

					break;
				}
				case IMPORT_DESC_TYPE_TABLE:
					snprintf(why, why_size,
						 "couldn't find table import: %s.%s",
						 import->module,
						 import->name);
					break;
				case IMPORT_DESC_TYPE_MEM:
					snprintf(why, why_size,
						 "couldn't find memory import: %s.%s "
						 "{%" PRIu32 ", %" PRIu32 "}",
						 import->module, import->name,
						 import->desc.memtype.limits.min,
						 import->desc.memtype.limits.max);
					break;
				case IMPORT_DESC_TYPE_GLOBAL:
					snprintf(why, why_size,
						 "couldn't find global import: %s.%s %s%s",
						 import->module, import->name,
						 wasmjit_valtype_repr(import->desc.globaltype.valtype),
						 import->desc.globaltype.mut
						 ? " mut" : "");
					break;
				default:
					assert(0);
					break;
				}
			goto error;
		}
	}

	internal_func_idx = module_inst->funcs.n_elts;
	for (i = 0; i < module->function_section.n_typeidxs; ++i) {
		assert(tmp_func == NULL);
		tmp_func = calloc(1, sizeof(*tmp_func));
		if (!tmp_func)
			goto error;

		tmp_func->module_inst = module_inst;
		tmp_func->compiled_code = NULL;
		tmp_func->type =
			module->type_section.types[module->
						   function_section.typeidxs[i]];

		LVECTOR_GROW(&module_inst->funcs, 1);
		module_inst->funcs.elts[module_inst->funcs.n_elts - 1] = tmp_func;
		tmp_func = NULL;
	}

	for (i = 0; i < module->table_section.n_tables; ++i) {
		struct TableSectionTable *table =
		    &module->table_section.tables[i];

		assert(!table->limits.max
		       || table->limits.min <= table->limits.max);


		assert(tmp_table == NULL);
		tmp_table = calloc(1, sizeof(*tmp_table));
		if (!tmp_table)
			goto error;

		tmp_table->data = calloc(tmp_table->length,
					 sizeof(tmp_table->data[0]));
		if (!tmp_table->data)
			goto error;

		tmp_table->elemtype = table->elemtype;
		tmp_table->length = table->limits.min;
		tmp_table->max = table->limits.max;

		LVECTOR_GROW(&module_inst->tables, 1);
		module_inst->tables.elts[module_inst->tables.n_elts - 1] = tmp_table;
		tmp_table = NULL;
	}

	for (i = 0; i < module->memory_section.n_memories; ++i) {
		struct MemorySectionMemory *memory =
		    &module->memory_section.memories[i];
		size_t size, max;

		assert(!memory->memtype.limits.max
		       || memory->memtype.limits.min <= memory->memtype.limits.max);

		if (__builtin_umull_overflow(memory->memtype.limits.min, WASM_PAGE_SIZE,
					     &size)) {
			goto error;
		}

		if (__builtin_umull_overflow(memory->memtype.limits.max, WASM_PAGE_SIZE,
					     &max)) {
			goto error;
		}

		assert(tmp_mem == NULL);
		tmp_mem = calloc(1, sizeof(*tmp_mem));
		if (!tmp_mem)
			goto error;

		if (size) {
			tmp_mem->data = calloc(size, 1);
			if (!tmp_mem->data) {
				free(tmp_mem);
				goto error;
			}
		}

		tmp_mem->size = size;
		tmp_mem->max = max;

		LVECTOR_GROW(&module_inst->mems, 1);
		module_inst->mems.elts[module_inst->mems.n_elts - 1] = tmp_mem;
		tmp_mem = NULL;
	}

	for (i = 0; i < module->global_section.n_globals; ++i) {
		struct GlobalSectionGlobal *global =
			&module->global_section.globals[i];
		struct Value value;
		int rrr;

		rrr = read_constant_expression(module_inst,
					       global->type.valtype, &value,
					       global->n_instructions,
					       global->instructions);
		if (!rrr)
			goto error;

		assert(tmp_global == NULL);
		tmp_global = calloc(1, sizeof(*tmp_global));
		if (!tmp_global)
			goto error;

		tmp_global->value = value;
		tmp_global->mut = global->type.mut;

		LVECTOR_GROW(&module_inst->globals, 1);
		module_inst->globals.elts[module_inst->globals.n_elts - 1] = tmp_global;
		tmp_global = NULL;
	}


	for (i = 0; i < module->export_section.n_exports; ++i) {
		struct Export *exportinst;
		struct ExportSectionExport *export =
		    &module->export_section.exports[i];

		LVECTOR_GROW(&module_inst->exports, 1);
		exportinst = &module_inst->exports.elts[module_inst->exports.n_elts - 1];

		exportinst->name = strdup(export->name);
		if (!exportinst->name)
			goto error;

		exportinst->type = export->idx_type;

		switch (export->idx_type) {
		case IMPORT_DESC_TYPE_FUNC:
			exportinst->value.func =
				module_inst->funcs.elts[export->idx_type];
			break;
		case IMPORT_DESC_TYPE_TABLE:
			exportinst->value.table =
				module_inst->tables.elts[export->idx_type];
			break;
		case IMPORT_DESC_TYPE_MEM:
			exportinst->value.mem =
				module_inst->mems.elts[export->idx_type];
			break;
		case IMPORT_DESC_TYPE_GLOBAL:
			exportinst->value.global =
				module_inst->globals.elts[export->idx_type];
			break;
		default:
			assert(0);
			break;
		}
	}

	for (i = 0; i < module->element_section.n_elements; ++i) {
		struct ElementSectionElement *element = &module->element_section.elements[i];
		struct TableInst *tableinst;
		int rrr;
		struct Value value;
		size_t j;

		rrr = read_constant_expression(module_inst,
					       VALTYPE_I32, &value,
					       element->n_instructions,
					       element->instructions);
		if (!rrr)
			goto error;

		assert(element->tableidx < module_inst->tables.n_elts);
		tableinst = module_inst->tables.elts[element->tableidx];

		if (value.data.i32 + element->n_funcidxs > tableinst->length)
			goto error;

		for (j = 0; j < element->n_funcidxs; ++j) {
			assert(element->funcidxs[j] < module_inst->funcs.n_elts);
			tableinst->data[value.data.i32 + j] = module_inst->funcs.elts[element->funcidxs[j]];
		}
	}

	if (!fill_module_types(module_inst, &module_types))
		goto error;

	for (i = 0; i < module->code_section.n_codes; ++i) {
		struct CodeSectionCode *code = &module->code_section.codes[i];
		struct FuncInst *funcinst;
		size_t j;

		funcinst = module_inst->funcs.elts[i + internal_func_idx];

		if (memrefs.elts) {
			free(memrefs.elts);
			memrefs.n_elts = 0;
			memrefs.elts = NULL;
		}

		if (unmapped)
			free(unmapped);

		assert(mapped == NULL);
		unmapped = wasmjit_compile_function(module_inst->types.elts,
						    &module_types,
						    &funcinst->type,
						    code,
						    &memrefs,
						    &code_size);
		if (!unmapped)
			goto error;

		mapped = wasmjit_map_code_segment(code_size);
		if (!mapped)
			goto error;

		memcpy(mapped, unmapped, code_size);

		/* resolve code references */
		for (j = 0; j < memrefs.n_elts; ++j) {
			uint64_t val;

			switch (memrefs.elts[j].type) {
			case MEMREF_TYPE:
				val = (uintptr_t) &module_inst->types.elts[memrefs.elts[j].idx];
				break;
			case MEMREF_FUNC:
				val = (uintptr_t) module_inst->funcs.elts[memrefs.elts[j].idx];
				break;
			case MEMREF_TABLE:
				val = (uintptr_t) module_inst->tables.elts[memrefs.elts[j].idx];
				break;
			case MEMREF_MEM:
				val = (uintptr_t) module_inst->mems.elts[memrefs.elts[j].idx];
				break;
			case MEMREF_GLOBAL:
				val = (uintptr_t) module_inst->globals.elts[memrefs.elts[j].idx];
				break;
			case MEMREF_RESOLVE_INDIRECT_CALL:
				val = (uintptr_t) &wasmjit_resolve_indirect_call;
				break;
			}

			encode_le_uint64_t(val, &((char *) mapped)[memrefs.elts[j].code_offset]);
		}


		if (!wasmjit_mark_code_segment_executable(mapped, code_size)) {
			goto error;
		}

		funcinst->compiled_code = mapped;
		funcinst->compiled_code_size = code_size;
		mapped = NULL;
	}

	for (i = 0; i < module->data_section.n_datas; ++i) {
		struct DataSectionData *data = &module->data_section.datas[i];
		struct MemInst *meminst =
		    module_inst->mems.elts[data->memidx];
		struct Value value;
		int rrr;

		rrr = read_constant_expression(module_inst,
					       VALTYPE_I32, &value,
					       data->n_instructions,
					       data->instructions);
		if (!rrr)
			goto error;

		if (data->buf_size > meminst->size)
			goto error;

		if (value.data.i32 > meminst->size - data->buf_size)
			goto error;

		memcpy(meminst->data +
		       value.data.i32, data->buf,
		       data->buf_size);
	}

	/* add start function */
	if (module->start_section.has_start) {
		wasmjit_invoke_function(module_inst->funcs.elts[module->start_section.funcidx],
					NULL, NULL);
	}

	if (0) {
	error:
		if (module_inst)
			wasmjit_free_module_inst(module_inst);
		module_inst = NULL;
	}

	if (tmp_func)
		free(tmp_func);
	if (tmp_table) {
		if (tmp_table->data)
			free(tmp_table->data);
		free(tmp_table);
	}
	if (tmp_mem) {
		if (tmp_mem->data)
			free(tmp_mem->data);
		free(tmp_mem);
	}
	if (tmp_global)
		free(tmp_global);
	if (mapped)
		wasmjit_unmap_code_segment(mapped, code_size);
	if (unmapped)
		free(unmapped);
	if (memrefs.elts)
		free(memrefs.elts);
	if (module_types.functypes)
		free(module_types.functypes);
	if (module_types.tabletypes)
		free(module_types.tabletypes);
	if (module_types.memorytypes)
		free(module_types.memorytypes);
	if (module_types.globaltypes)
		free(module_types.globaltypes);


	return module_inst;
}
