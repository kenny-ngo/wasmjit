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

#include <wasmjit/wasmbin_compile.h>
#include <wasmjit/wasmbin_dump.h>
#include <wasmjit/wasmbin_parse.h>
#include <wasmjit/wasmbin.h>
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

	/* the most basic validation */
	if (module.code_section.n_codes !=
	    module.function_section.n_typeidxs) {
		printf("# Functions != # Codes %"PRIu32" != %"PRIu32"\n",
		       module.function_section.n_typeidxs,
		       module.code_section.n_codes);
		return -1;
	}

	/* initialize store */
	memset(&store, 0, sizeof(store));

	if (!wasmjit_instantiate("env", &module, &store, &startaddr))
		return -1;

	/* execute module */
	if (module.start_section.has_start) {
		if (!wasmjit_execute(&store, startaddr))
			return -1;
	}

	return 0;
}
