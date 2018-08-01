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

#include <wasmjit/wasmbin.h>
#include <wasmjit/runtime.h>
#include <wasmjit/wasmbin_compile.h>
#include <wasmjit/util.h>

#include <stdio.h>

int wasmjit_instantiate(const char *module_name,
			const struct Module *module,
			struct Store *store, size_t *startaddr)
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

	if (module->table_section.n_tables) {
		/* don't currenly handle tables */
		snprintf(why, sizeof(why), "don't handle tables");
		goto error;
	}

	for (i = 0; i < module->memory_section.n_memories; ++i) {
		struct MemorySectionMemory *memory =
		    &module->memory_section.memories[i];
		size_t memaddr = store->mems.n_elts;
		struct Addrs *addrs = &module_inst->memaddrs;

		if (!store_mems_grow(&store->mems, 1))
			goto error;

		/* TODO: what to do with memory->memtype.min? */

		store->mems.elts[memaddr].data =
		    (char *)(intptr_t) (0xdeadbeef);
		store->mems.elts[memaddr].max = memory->memtype.max;
		store->mems.elts[memaddr].has_max = memory->memtype.has_max;

		if (!addrs_grow(addrs, 1))
			goto error;
		addrs->elts[addrs->n_elts - 1] = memaddr;
	}

	if (module->global_section.n_globals) {
		/* don't currenly handle globals */
		snprintf(why, sizeof(why), "don't handle globals");
		goto error;
	}

	for (i = 0; i < module->code_section.n_codes; ++i) {
		struct TypeSectionType *type;
		struct FuncInst *funcinst;
		size_t funcaddr;
		struct Addrs *addrs = &module_inst->memaddrs;

		type =
		    &module->type_section.types[module->function_section.
						typeidxs[i]];
		funcaddr = store->funcs.n_elts;

		if (!store_funcs_grow(&store->funcs, 1))
			goto error;

		funcinst = &store->funcs.elts[funcaddr];

		memset(funcinst, 0, sizeof(*funcinst));

		funcinst->type.n_inputs = type->n_inputs;
		funcinst->type.input_types =
		    wasmjit_copy_buf(type->input_types,
				     type->n_inputs,
				     sizeof(type->input_types[0]));
		if (!funcinst->type.input_types)
			goto error;
		funcinst->type.n_outputs = type->n_outputs;
		funcinst->type.output_types =
		    wasmjit_copy_buf(type->output_types,
				     type->n_outputs,
				     sizeof(type->output_types[0]));
		if (!funcinst->type.output_types)
			goto error;

		if (!addrs_grow(addrs, 1))
			goto error;

		addrs->elts[addrs->n_elts - 1] = funcaddr;

		funcinst->code = wasmjit_compile_code(store,
						      module_inst,
						      type,
						      &module->code_section.
						      codes[i],
						      &funcinst->memrefs,
						      &funcinst->code_size);
		if (!funcinst->code) {
			snprintf(why, sizeof(why), "compile failed");
			goto error;
		}
	}

	for (i = 0; i < module->export_section.n_exports; ++i) {
		struct ExportSectionExport *export =
		    &module->export_section.exports[i];
		struct NamespaceEntry *entry;
		struct Addrs *addrs;

		if (!store_names_grow(&store->names, 1))
			goto error;

		entry = &store->names.elts[store->names.n_elts - 1];

		entry->module_name = strdup(module_name);
		if (!entry->module_name)
			goto error;
		entry->name = strdup(export->name);
		if (!entry->name)
			goto error;
		entry->type = export->idx_type;

		switch (entry->type) {
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

		entry->addr = addrs->elts[export->idx];
	}

	/* return funcaddr of start_function */
	if (module->start_section.has_start) {
		*startaddr =
		    module_inst->funcaddrs.elts[module->start_section.funcidx];
	}

	return 1;

 error:
	/* TODO: cleanup isn't currently implemented */
	fprintf(stderr, "%s\n", why);
	assert(0);
	return 0;
}
