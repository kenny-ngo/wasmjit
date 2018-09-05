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

#include <wasmjit/ast.h>
#include <wasmjit/compile.h>
#include <wasmjit/ast_dump.h>
#include <wasmjit/parse.h>
#include <wasmjit/runtime.h>
#include <wasmjit/instantiate.h>
#include <wasmjit/execute.h>
#include <wasmjit/emscripten_runtime.h>
#include <wasmjit/dynamic_emscripten_runtime.h>
#include <wasmjit/elf_relocatable.h>
#include <wasmjit/util.h>
#include <wasmjit/high_level.h>

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>

int main(int argc, char *argv[])
{
	int ret;
	struct ParseState pstate;
	struct Module module;
	char *filename;
	int dump_module, create_relocatable, create_relocatable_helper, opt;
	size_t tablemin = 0, tablemax = 0, i;
	void *buf;
	size_t size;

	dump_module =  0;
	create_relocatable =  0;
	create_relocatable_helper =  0;
	while ((opt = getopt(argc, argv, "dop")) != -1) {
		switch (opt) {
		case 'o':
			create_relocatable = 1;
			break;
		case 'p':
			create_relocatable_helper = 1;
			break;
		case 'd':
			dump_module = 1;
			break;
		default:
			return -1;
		}
	}

	if (optind >= argc) {
		printf("Need an input file\n");
		return -1;
	}

	filename = argv[optind];

	buf = wasmjit_load_file(filename, &size);
	if (!buf)
		return -1;

	ret = init_pstate(&pstate, buf, size);
	if (!ret) {
		printf("Error loading file\n");
		return -1;
	}

	init_module(&module);

	ret = read_module(&pstate, &module, NULL, 0);
	if (!ret) {
		printf("Error parsing module\n");
		return -1;
	}

	free(buf);

	if (dump_module) {
		uint32_t i;

		for (i = 0; i < module.code_section.n_codes; ++i) {
			uint32_t j;
			struct TypeSectionType *type;
			struct CodeSectionCode *code =
			    &module.code_section.codes[i];

			type =
			    &module.type_section.types[module.function_section.
						       typeidxs[i]];

			printf("Code #%" PRIu32 "\n", i);

			printf("Locals (%" PRIu32 "):\n", code->n_locals);
			for (j = 0; j < code->n_locals; ++j) {
				printf("  %s (%" PRIu32 ")\n",
				       wasmjit_valtype_repr(code->locals[j].
							    valtype),
				       code->locals[j].count);
			}

			printf("Signature: [");
			for (j = 0; j < type->n_inputs; ++j) {
				printf("%s,",
				       wasmjit_valtype_repr(type->
							    input_types[j]));
			}
			printf("] -> [");
			for (j = 0; j < FUNC_TYPE_N_OUTPUTS(type); ++j) {
				printf("%s,",
				       wasmjit_valtype_repr(FUNC_TYPE_OUTPUT_IDX(type, j)));
			}
			printf("]\n");

			printf("Instructions:\n");
			dump_instructions(module.code_section.codes[i].
					  instructions,
					  module.code_section.codes[i].
					  n_instructions, 1);
			printf("\n");
		}

		return 0;
	}

	/* the most basic validation */
	if (module.code_section.n_codes != module.function_section.n_typeidxs) {
		printf("# Functions != # Codes %" PRIu32 " != %" PRIu32 "\n",
		       module.function_section.n_typeidxs,
		       module.code_section.n_codes);
		return -1;
	}

	if (create_relocatable) {
		void *a_out;
		size_t size;

		a_out = wasmjit_output_elf_relocatable("asm", &module, &size);
		write(1, a_out, size);

		return 0;
	}

	/* find correct tablemin and tablemax */
	for (i = 0; i < module.import_section.n_imports; ++i) {
		struct ImportSectionImport *import;
		import = &module.import_section.imports[i];
		if (strcmp(import->module, "env") ||
		    strcmp(import->name, "table") ||
		    import->desc_type != IMPORT_DESC_TYPE_TABLE)
			continue;

		tablemin = import->desc.tabletype.limits.min;
		tablemax = import->desc.tabletype.limits.max;
		break;
	}

	if (create_relocatable_helper) {
		void *a_out;
		size_t size;
		struct Module env_module;
		struct TableSectionTable table;
		struct ExportSectionExport export;

		memset(&env_module, 0, sizeof(env_module));

		table.elemtype = ELEMTYPE_ANYFUNC;
		table.limits.min = tablemin;
		table.limits.max = tablemax;

		env_module.table_section.n_tables = 1;
		env_module.table_section.tables = &table;

		export.name = "table";
		export.idx_type = IMPORT_DESC_TYPE_TABLE;
		export.idx = 0;

		env_module.export_section.n_exports = 1;
		env_module.export_section.exports = &export;

		a_out = wasmjit_output_elf_relocatable("env", &env_module, &size);
		write(1, a_out, size);

		return 0;
	}

	{
		struct WasmJITHigh high;
		int ret;

		if (!wasmjit_high_init(&high)) {
			fprintf(stderr, "failed to initialize\n");
			return -1;
		}

		if (!wasmjit_high_instantiate_emscripten_runtime(&high,
								 tablemin, tablemax, 0)) {
			fprintf(stderr, "failed to instantiate emscripten runtime\n");
			return -1;
		}

		if (!wasmjit_high_instantiate(&high, filename, "asm", 0)) {
			fprintf(stderr, "failed to instantiate module\n");
			return -1;
		}

		ret = wasmjit_high_emscripten_invoke_main(&high, "asm",
							  argc - optind,
							  &argv[optind], 0);

		wasmjit_high_close(&high);

		free_module(&module);

		return ret;
	}
}
