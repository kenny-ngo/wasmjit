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

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>

#include <unistd.h>

int main(int argc, char *argv[])
{
	int ret;
	struct ParseState pstate;
	struct Module module;
	//struct Store store;
	int dump_module, opt;
	//char error_buffer[0x1000];

	dump_module =  0;
	while ((opt = getopt(argc, argv, "d")) != -1) {
		switch (opt) {
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

	ret = init_pstate(&pstate, argv[optind]);
	if (!ret) {
		printf("Error loading file %s\n", strerror(errno));
		return -1;
	}

	ret = read_module(&pstate, &module);
	if (!ret) {
		printf("Error parsing module\n");
		return -1;
	}

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
			for (j = 0; j < type->n_outputs; ++j) {
				printf("%s,",
				       wasmjit_valtype_repr(type->
							    output_types[j]));
			}
			printf("]\n");

			printf("Instructions:\n");
			dump_instructions(module.code_section.codes[i].
					  instructions,
					  module.code_section.codes[i].
					  n_instructions, 1);
			printf("\n");
		}
	}

	/* the most basic validation */
	if (module.code_section.n_codes != module.function_section.n_typeidxs) {
		printf("# Functions != # Codes %" PRIu32 " != %" PRIu32 "\n",
		       module.function_section.n_typeidxs,
		       module.code_section.n_codes);
		return -1;
	}

	return 0;

#if 0
	/* initialize store */
	memset(&store, 0, sizeof(store));

	if (!wasmjit_add_emscripten_runtime(&store))
		return -1;

	if (!wasmjit_instantiate("env", &module, &store, error_buffer, sizeof(error_buffer))) {
		printf("Error instantiating: %s\n", error_buffer);
		return -1;
	}

	/* go to entry point */
	return wasmjit_execute(&store, optind + 1, &argv[optind + 1]);
#endif
}
