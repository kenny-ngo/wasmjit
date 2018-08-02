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

#include <wasmjit/ast.h>
#include <wasmjit/util.h>

int wasmjit_import_function(struct Store *store,
			    const char *module_name,
			    const char *name,
			    void *funcptr,
			    size_t n_inputs,
			    unsigned *input_types,
			    size_t n_outputs, unsigned *output_types)
{
	size_t funcaddr = store->funcs.n_elts;

	/* add function to store */
	if (!store_funcs_grow(&store->funcs, 1))
		goto error;

	{
		struct FuncInst *funcinst;

		funcinst = &store->funcs.elts[funcaddr];

		memset(funcinst, 0, sizeof(*funcinst));

		funcinst->is_host = 1;

		funcinst->type.n_inputs = n_inputs;
		funcinst->type.input_types =
		    wasmjit_copy_buf(input_types,
				     n_inputs, sizeof(input_types[0]));
		if (!funcinst->type.input_types)
			goto error;

		funcinst->type.n_outputs = n_outputs;
		funcinst->type.output_types =
		    wasmjit_copy_buf(output_types,
				     n_outputs, sizeof(output_types[0]));
		if (!funcinst->type.output_types)
			goto error;

		funcinst->code = funcptr;
	}

	/* now add it to namespace */
	{
		struct NamespaceEntry *entry;

		if (!store_names_grow(&store->names, 1))
			goto error;

		entry = &store->names.elts[store->names.n_elts - 1];

		entry->module_name = strdup(module_name);
		if (!entry->module_name)
			goto error;
		entry->name = strdup(name);
		if (!entry->name)
			goto error;
		entry->type = IMPORT_DESC_TYPE_FUNC;
		entry->addr = funcaddr;
	}

	return 1;

 error:
	/* TODO: cleanup */
	assert(0);
	return 0;

}
