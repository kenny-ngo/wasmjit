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

int wasmjit_instantiate(const char *module_name,
			const struct Module *module,
			struct Store *store)
{
	char why[0x100];
	uint32_t i;
	struct ModuleInst module_inst_v;
	struct ModuleInst *module_inst = &module_inst_v;

	memset(module_inst, 0, sizeof(module_inst_v));

	/* load imports */
	for (i = 0; i < module->import_section.n_imports; ++i) {
		size_t j;
		struct ImportSectionImport *import =
		    &module->import_section.imports[i];

		/* look for import */
		for (j = 0; j < store->names.n_elts; ++j) {
			struct Addrs *addrs;
			struct NamespaceEntry *entry = &store->names.elts[i];
			if (strcmp(entry->module_name, import->module) ||
			    strcmp(entry->name, import->name))
				continue;

			if (entry->type != (unsigned)import->desc_type) {
				/* bad import type */
				snprintf(why, sizeof(why), "bad import type");
				goto error;
			}

			switch (entry->type) {
			case IMPORT_DESC_TYPE_FUNC: {
				assert(entry->addr < store->funcs.n_elts);
				struct FuncInst *funcinst = &store->funcs.elts[entry->addr];
				struct TypeSectionType *type = &module->type_section.types[import->desc.typeidx];
				if (!typelist_equal(type->n_inputs, type->input_types,
						    funcinst->type.n_inputs,
						    funcinst->type.input_types))
					goto error;

				if (!typelist_equal(type->n_outputs, type->output_types,
						    funcinst->type.n_outputs,
						    funcinst->type.output_types))
					goto error;

				addrs = &module_inst->funcaddrs;
				break;
			}
			case IMPORT_DESC_TYPE_TABLE:
				addrs = &module_inst->tableaddrs;
				break;
			case IMPORT_DESC_TYPE_MEM: {
				assert(entry->addr < store->mems.n_elts);
				struct MemInst *meminst = &store->mems.elts[entry->addr];

				if (!(meminst->size / WASM_PAGE_SIZE >= import->desc.memtype.min &&
				      meminst->max / WASM_PAGE_SIZE == import->desc.memtype.max))
					goto error;

				addrs = &module_inst->memaddrs;
				break;
			}
			case IMPORT_DESC_TYPE_GLOBAL:
				addrs = &module_inst->globaladdrs;
				break;
			default:
				assert(0);
				break;
			}

			if (!addrs_grow(addrs, 1))
				goto error;
			addrs->elts[addrs->n_elts - 1] = entry->addr;
			break;
		}

		if (j == store->names.n_elts) {
			/* couldn't find import */
			snprintf(why, sizeof(why),
				 "couldn't find import: %s.%s", import->module,
				 import->name);
			goto error;
		}
	}

	for (i = 0; i < module->table_section.n_tables; ++i) {
		struct TableSectionTable *table =
		    &module->table_section.tables[i];

		size_t tableaddr = store->tables.n_elts;
		struct Addrs *addrs = &module_inst->tableaddrs;
		struct TableInst *tableinst;

		assert(!table->limits.max
		       || table->limits.min <= table->limits.max);

		if (!store_tables_grow(&store->tables, 1))
			goto error;

		tableinst = &store->tables.elts[tableaddr];
		tableinst->elemtype = table->elemtype;
		if (table->limits.min) {
			tableinst->data =
			    wasmjit_alloc_vector(table->limits.min,
						 sizeof(tableinst->data[0]),
						 NULL);
			if (!tableinst->data)
				goto error;
		} else {
			tableinst->data = NULL;
		}
		tableinst->length = table->limits.min;
		tableinst->max = table->limits.max;

		if (!addrs_grow(addrs, 1))
			goto error;
		addrs->elts[addrs->n_elts - 1] = tableaddr;
	}

	for (i = 0; i < module->memory_section.n_memories; ++i) {
		struct MemorySectionMemory *memory =
		    &module->memory_section.memories[i];
		struct Addrs *addrs = &module_inst->memaddrs;
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

	if (module->global_section.n_globals) {
		/* don't currenly handle globals */
		snprintf(why, sizeof(why), "don't handle globals");
		goto error;
	}

	if (module->element_section.n_elements) {
		/* don't currenly handle elements */
		snprintf(why, sizeof(why), "don't handle elements");
		goto error;
	}

	for (i = 0; i < module->code_section.n_codes; ++i) {
		struct TypeSectionType *type;
		size_t funcaddr;
		struct Addrs *addrs = &module_inst->funcaddrs;
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
			snprintf(why, sizeof(why), "compile failed");
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
		struct Addrs *addrs;

		switch (export->idx_type) {
		case IMPORT_DESC_TYPE_FUNC:
			addrs = &module_inst->funcaddrs;
			break;
		case IMPORT_DESC_TYPE_TABLE:
			addrs = &module_inst->tableaddrs;
			break;
		case IMPORT_DESC_TYPE_MEM:
			addrs = &module_inst->memaddrs;
			break;
		case IMPORT_DESC_TYPE_GLOBAL:
			addrs = &module_inst->globaladdrs;
			break;
		default:
			assert(0);
			break;
		}

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

		if (data->n_instructions != 1
		    || data->instructions[0].opcode != OPCODE_I32_CONST) {
			/* TODO: execute instructions in the future,
			   rely on validation stage to check for const property */
			goto error;
		}
#if UINT32_MAX > SIZE_MAX
		if (data->buf_size > SIZE_MAX)
			goto error;
#endif

		memcpy(meminst->data +
		       data->instructions[0].data.i32_const.value, data->buf,
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
	/* TODO: cleanup isn't currently implemented */
	fprintf(stderr, "%s\n", why);
	assert(0);
	return 0;
}
