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

#include <stdio.h>

#define WASM_PAGE_SIZE ((size_t) (64 * 1024))

static int typelist_equal(size_t nelts, unsigned *elts,
			  size_t onelts, unsigned *oelts) {
	size_t i;
	if (nelts != onelts) return 0;
	for (i = 0; i < nelts; ++i) {
		if (elts[i] != oelts[i]) return 0;
	}
	return 1;
}

static struct Addrs *addrs_for_section(struct ModuleInst *module_inst, unsigned section) {
	switch (section) {
	case IMPORT_DESC_TYPE_FUNC: return &module_inst->funcaddrs;
	case IMPORT_DESC_TYPE_TABLE: return &module_inst->tableaddrs;
	case IMPORT_DESC_TYPE_MEM: return &module_inst->memaddrs;
	case IMPORT_DESC_TYPE_GLOBAL: return &module_inst->globaladdrs;
	default: assert(0); return NULL;
	}
}

static int func_sig_repr(char *why, size_t why_size,
			 size_t n_inputs, unsigned *input_types,
			 size_t n_outputs, unsigned *output_types) {
	int ret ;
	size_t k;

	ret = snprintf(why, why_size, "[");
	for (k = 0; k < n_inputs; ++k) {
		ret += snprintf(why + ret, why_size - ret, "%s,",
				wasmjit_valtype_repr(input_types[k]));
	}
	ret += snprintf(why + ret, why_size - ret, "] -> [");
	for (k = 0; k < n_outputs; ++k) {
		ret += snprintf(why + ret, why_size - ret, "%s,",
				wasmjit_valtype_repr(output_types[k]));
	}
	ret += snprintf(why + ret, why_size - ret, "]");

	return ret;
}

static int read_constant_expression(struct Store *store,
				    struct ModuleInst *module_inst,
				    unsigned valtype, struct Value *value,
				    size_t n_instructions, struct Instr *instructions)
{
	if (n_instructions != 1)
		goto error;

	if (instructions[0].opcode == OPCODE_GET_GLOBAL) {
		wasmjit_addr_t globaladdr =
			module_inst->globaladdrs.elts[instructions[0].data.get_global.globalidx];
		struct GlobalInst *global =
			&store->globals.elts[globaladdr];

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

int wasmjit_instantiate(const char *module_name,
			const struct Module *module,
			struct Store *store,
			char *why, size_t why_size)
{
	uint32_t i;
	struct ModuleInst module_inst_v;
	struct ModuleInst *module_inst = &module_inst_v;
	struct Addrs *addrs;

	memset(module_inst, 0, sizeof(module_inst_v));

	/* load imports */
	for (i = 0; i < module->import_section.n_imports; ++i) {
		size_t j;
		struct ImportSectionImport *import =
		    &module->import_section.imports[i];

		/* look for import */
		for (j = 0; j < store->names.n_elts; ++j) {
			struct NamespaceEntry *entry = &store->names.elts[j];
			if (strcmp(entry->module_name, import->module) ||
			    strcmp(entry->name, import->name))
				continue;

			if (entry->type != import->desc_type) {
				/* bad import type */
				if (why)
					snprintf(why, why_size, "bad import type");
				goto error;
			}

			switch (entry->type) {
			case IMPORT_DESC_TYPE_FUNC: {
				assert(entry->addr < store->funcs.n_elts);
				struct FuncInst *funcinst = &store->funcs.elts[entry->addr];
				struct TypeSectionType *type = &module->type_section.types[import->desc.typeidx];
				if (!typelist_equal(type->n_inputs, type->input_types,
						    funcinst->type.n_inputs,
						    funcinst->type.input_types) ||
				    !typelist_equal(type->n_outputs, type->output_types,
						    funcinst->type.n_outputs,
						    funcinst->type.output_types)) {
					int ret;
					ret = snprintf(why, why_size,
						       "Mismatched types for %s.%s: ",
						       import->module,
						       import->name);
					ret += func_sig_repr(why + ret, why_size - ret,
							     funcinst->type.n_inputs,
							     funcinst->type.input_types,
							     funcinst->type.n_outputs,
							     funcinst->type.output_types);
					ret += snprintf(why + ret, why_size - ret,
							" vs ");
					ret += func_sig_repr(why + ret, why_size - ret,
							     type->n_inputs,
							     type->input_types,
							     type->n_outputs,
							     type->output_types);
					goto error;
				}
				break;
			}
			case IMPORT_DESC_TYPE_TABLE:
				assert(entry->addr < store->tables.n_elts);
				struct TableInst *tableinst = &store->tables.elts[entry->addr];

				if (!(tableinst->elemtype == import->desc.tabletype.elemtype &&
				      tableinst->length >= import->desc.tabletype.limits.min &&
				      (!import->desc.tabletype.limits.max ||
				       (import->desc.tabletype.limits.max && tableinst->max &&
					tableinst->max <= import->desc.tabletype.limits.max)))) {
					if (why)
						snprintf(why, why_size,
							 "Mismatched table import for import "
							 "%s.%s: {%u, %zu,%zu} vs {%u, %" PRIu32 ",%" PRIu32 "}",
							 entry->module_name, entry->name,
							 tableinst->elemtype,
							 tableinst->length,
							 tableinst->max,
							 import->desc.tabletype.elemtype,
							 import->desc.tabletype.limits.min,
							 import->desc.tabletype.limits.max);
					goto error;
				}
				break;
			case IMPORT_DESC_TYPE_MEM: {
				assert(entry->addr < store->mems.n_elts);
				struct MemInst *meminst = &store->mems.elts[entry->addr];
				size_t msize = meminst->size / WASM_PAGE_SIZE;
				size_t mmax = meminst->max / WASM_PAGE_SIZE;

				if (!(msize >= import->desc.memtype.min &&
				      (!import->desc.memtype.max ||
				       (import->desc.memtype.max && mmax &&
					mmax <= import->desc.memtype.max)))) {
					if (why)
						snprintf(why, why_size,
							 "Mismatched memory size for import "
							 "%s.%s: {%zu,%zu} vs {%" PRIu32 ",%" PRIu32 "}",
							 entry->module_name, entry->name,
							 meminst->size / WASM_PAGE_SIZE,
							 meminst->max / WASM_PAGE_SIZE,
							 import->desc.memtype.min,
							 import->desc.memtype.max);
					goto error;
				}
				break;
			}
			case IMPORT_DESC_TYPE_GLOBAL: {
				assert(entry->addr < store->globals.n_elts);
				struct GlobalInst *globalinst = &store->globals.elts[entry->addr];

				if (globalinst->value.type != import->desc.globaltype.valtype ||
				    globalinst->mut != import->desc.globaltype.mut) {
					if (why)
						snprintf(why, why_size,
							 "Mismatched global for import "
							 "%s.%s: %s%s vs %s%s",
							 entry->module_name,
							 entry->name,
							 wasmjit_valtype_repr(globalinst->value.type),
							 globalinst->mut ? " mut" : "",
							 wasmjit_valtype_repr(import->desc.globaltype.valtype),
							 import->desc.globaltype.mut ? " mut" : "");
					goto error;
				}
				break;
			}
			default:
				assert(0);
				break;
			}

			addrs = addrs_for_section(module_inst, entry->type);

			if (!addrs_grow(addrs, 1))
				goto error;
			addrs->elts[addrs->n_elts - 1] = entry->addr;
			break;
		}

		if (j == store->names.n_elts) {
			/* couldn't find import */
			if (why)
				switch (import->desc_type) {
				case IMPORT_DESC_TYPE_FUNC: {
					int ret;
					struct TypeSectionType *type = &module->type_section.types[import->desc.typeidx];
					ret = snprintf(why, why_size,
						       "couldn't find func import: %s.%s ",
						       import->module,
						       import->name);

					func_sig_repr(why + ret, why_size - ret,
						      type->n_inputs,
						      type->input_types,
						      type->n_outputs,
						      type->output_types);

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
						 import->desc.memtype.min,
						 import->desc.memtype.max);
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

	for (i = 0; i < module->table_section.n_tables; ++i) {
		struct TableSectionTable *table =
		    &module->table_section.tables[i];

		size_t tableaddr;
		addrs = &module_inst->tableaddrs;

		assert(!table->limits.max
		       || table->limits.min <= table->limits.max);

		tableaddr =
			_wasmjit_add_table_to_store(store,
						    table->elemtype,
						    table->limits.min,
						    table->limits.max);

		if (!addrs_grow(addrs, 1))
			goto error;
		addrs->elts[addrs->n_elts - 1] = tableaddr;
	}

	for (i = 0; i < module->memory_section.n_memories; ++i) {
		struct MemorySectionMemory *memory =
		    &module->memory_section.memories[i];
		addrs = &module_inst->memaddrs;
		wasmjit_addr_t memaddr;
		size_t size, max;

		assert(!memory->memtype.max
		       || memory->memtype.min <= memory->memtype.max);

		if (__builtin_umull_overflow(memory->memtype.min, WASM_PAGE_SIZE,
					     &size)) {
			goto error;
		}

		if (__builtin_umull_overflow(memory->memtype.max, WASM_PAGE_SIZE,
					     &max)) {
			goto error;
		}

		memaddr = _wasmjit_add_memory_to_store(store, size, max);

		if (!addrs_grow(addrs, 1))
			goto error;
		addrs->elts[addrs->n_elts - 1] = memaddr;
	}

	for (i = 0; i < module->global_section.n_globals; ++i) {
		struct GlobalSectionGlobal *global =
			&module->global_section.globals[i];
		wasmjit_addr_t globaladdr;
		struct Value value;
		int rrr;

		rrr = read_constant_expression(store,
					       module_inst,
					       global->type.valtype, &value,
					       global->n_instructions,
					       global->instructions);
		if (!rrr)
			goto error;
		globaladdr = _wasmjit_add_global_to_store(store,
							  value,
							  global->type.mut);

		{
			struct Addrs *addrs = &module_inst->globaladdrs;
			if (!addrs_grow(addrs, 1))
				goto error;
			addrs->elts[addrs->n_elts - 1] = globaladdr;
		}
	}

	if (module->element_section.n_elements) {
		/* don't currenly handle elements */
		if (why)
			snprintf(why, why_size, "don't handle elements");
		goto error;
	}

	for (i = 0; i < module->code_section.n_codes; ++i) {
		struct TypeSectionType *type;
		size_t funcaddr;
		addrs = &module_inst->funcaddrs;
		void *code;
		size_t code_size;
		struct MemoryReferences memrefs = {0, NULL};

		type =
		    &module->type_section.types[module->
						function_section.typeidxs[i]];

		code = wasmjit_compile_code(store,
					    module_inst,
					    type,
					    &module->
					    code_section.codes[i],
					    &memrefs,
					    &code_size);
		if (!code) {
			if (why)
				snprintf(why, why_size, "compile failed");
			goto error;
		}

		funcaddr = _wasmjit_add_function_to_store(store, code, code_size,
							  type->n_inputs,
							  type->input_types,
							  type->n_outputs,
							  type->output_types,
							  memrefs);
		if (funcaddr == INVALID_ADDR)
			goto error;

		if (!addrs_grow(addrs, 1))
			goto error;

		addrs->elts[addrs->n_elts - 1] = funcaddr;
	}

	for (i = 0; i < module->export_section.n_exports; ++i) {
		struct ExportSectionExport *export =
		    &module->export_section.exports[i];

		addrs = addrs_for_section(module_inst, export->idx_type);

		assert(export->idx < addrs->n_elts);
		if (!_wasmjit_add_to_namespace(store, module_name,
					       export->name,
					       export->idx_type,
					       addrs->elts[export->idx]))
		    goto error;
	}

	for (i = 0; i < module->data_section.n_datas; ++i) {
		struct DataSectionData *data = &module->data_section.datas[i];
		struct MemInst *meminst =
		    &store->mems.elts[module_inst->memaddrs.elts[data->memidx]];
		struct Value value;
		int rrr;

		rrr = read_constant_expression(store, module_inst,
					       VALTYPE_I32, &value,
					       data->n_instructions,
					       data->instructions);
		if (!rrr)
			goto error;

#if UINT32_MAX > SIZE_MAX
		if (data->buf_size > SIZE_MAX)
			goto error;
#endif

		memcpy(meminst->data +
		       value.data.i32, data->buf,
		       data->buf_size);
	}

	/* add start function */
	if (module->start_section.has_start) {
		if (!addrs_grow(&store->startfuncs, 1))
			goto error;

		store->startfuncs.elts[store->startfuncs.n_elts - 1] =
			module_inst->funcaddrs.elts[module->start_section.funcidx];
	}

	return 1;

 error:
	/* cleanup module_inst */
	for (i = 0; i < IMPORT_DESC_TYPE_LAST; ++i) {
		addrs = addrs_for_section(module_inst, i);
		free(addrs->elts);
	}

	return 0;
}
