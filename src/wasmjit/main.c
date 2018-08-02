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

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>

#include <unistd.h>

uint32_t write_callback(uint32_t fd_arg, uint32_t buf_arg, uint32_t count)
{
	char *base_address = wasmjit_get_base_address();
	return write(fd_arg, base_address + buf_arg, count);
}

int main(int argc, char *argv[])
{
	int ret;
	struct ParseState pstate;
	struct Module module;
	struct Store store;
	size_t startaddr;

	if (argc < 2) {
		printf("Need an input file\n");
		return -1;
	}

	ret = init_pstate(&pstate, argv[1]);
	if (!ret) {
		printf("Error loading file %s\n", strerror(errno));
		return -1;
	}

	ret = read_module(&pstate, &module);
	if (!ret) {
		printf("Error parsing module\n");
		return -1;
	}

	if (1) {
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
				       code->locals[i].count);
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

	/* initialize store */
	memset(&store, 0, sizeof(store));

	{
		unsigned inputs[3] = { VALTYPE_I32, VALTYPE_I32, VALTYPE_I32 };
		unsigned outputs[1] = { VALTYPE_I32 };
		if (!wasmjit_import_function(&store,
					     "env", "write",
					     write_callback,
					     3, inputs, 1, outputs))
			return -1;
	}

	if (!wasmjit_instantiate("env", &module, &store, &startaddr))
		return -1;

	/* execute module */
	if (module.start_section.has_start) {
		if (!wasmjit_execute(&store, startaddr, NULL, 0, NULL))
			return -1;
	}

	return 0;
}
