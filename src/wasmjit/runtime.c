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
#include <wasmjit/tls.h>

#include <assert.h>

wasmjit_tls_key_t meminst_key;

__attribute__((constructor))
static void init_meminst_key()
{
	if (!wasmjit_init_tls_key(&meminst_key, NULL))
		abort();
}

void *wasmjit_get_base_address()
{
	struct MemInst **mb;
	if (!wasmjit_get_tls_key(meminst_key, &mb))
		return NULL;
	return (*mb)->data;
}

int _wasmjit_set_base_meminst_ptr_ptr(struct MemInst **meminst_box)
{
	return wasmjit_set_tls_key(meminst_key, meminst_box);
}

wasmjit_addr_t _wasmjit_add_memory_to_store(struct Store *store,
					    size_t size,
					    size_t max)
{
	struct MemInst *meminst;
	wasmjit_addr_t memaddr = store->mems.n_elts;

	if (!store_mems_grow(&store->mems, 1))
		goto error;

	meminst = &store->mems.elts[memaddr];

	meminst->size = size;
	meminst->max = max;

	if (size) {
		meminst->data = malloc(size);
		if (!meminst->data)
			goto error;
	}
	else {
		meminst->data = NULL;
	}


	return memaddr;

 error:
	/* TODO: implement cleanup */
	assert(0);
	return INVALID_ADDR;
}

wasmjit_addr_t _wasmjit_add_function_to_store(struct Store *store,
					      void *code, size_t code_size,
					      size_t n_inputs,
					      unsigned *input_types,
					      size_t n_outputs, unsigned *output_types,
					      struct MemoryReferences memrefs)
{
	struct FuncInst *funcinst;
	wasmjit_addr_t funcaddr = store->funcs.n_elts;

	if (!store_funcs_grow(&store->funcs, 1))
		goto error;

	funcinst = &store->funcs.elts[funcaddr];

	funcinst->type.n_inputs = n_inputs;
	funcinst->type.input_types =
		wasmjit_copy_buf(input_types,
				 n_inputs,
				 sizeof(input_types[0]));
	if (!funcinst->type.input_types)
		goto error;
	funcinst->type.n_outputs = n_outputs;
	funcinst->type.output_types =
		wasmjit_copy_buf(output_types,
				 n_outputs,
				 sizeof(output_types[0]));
	if (!funcinst->type.output_types)
		goto error;

	funcinst->code = code;
	funcinst->code_size = code_size;
	funcinst->memrefs = memrefs;

	return funcaddr;

 error:
	/* TODO: implement cleanup */
	assert(0);
	return INVALID_ADDR;
}

wasmjit_addr_t _wasmjit_add_table_to_store(struct Store *store,
					   unsigned elemtype,
					   size_t length,
					   size_t max)
{
	struct TableInst *tableinst;
	wasmjit_addr_t tableaddr = store->tables.n_elts;

	assert(elemtype == ELEMTYPE_ANYFUNC);

	if (!store_tables_grow(&store->tables, 1))
		goto error;

	tableinst = &store->tables.elts[tableaddr];

	tableinst->elemtype = elemtype;
	tableinst->length = length;
	tableinst->max = max;

	if (length) {
		tableinst->data = wasmjit_alloc_vector(length,
						       sizeof(void *),
						       NULL);
		if (!tableinst->data)
			goto error;
	}
	else {
		tableinst->data = NULL;
	}


	return tableaddr;

 error:
	/* TODO: implement cleanup */
	assert(0);
	return INVALID_ADDR;
}

wasmjit_addr_t _wasmjit_add_global_to_store(struct Store *store,
					    struct Value value,
					    unsigned mut)
{
	struct GlobalInst *globalinst;
	wasmjit_addr_t globaladdr = store->globals.n_elts;

	assert(value.type == VALTYPE_I32 ||
	       value.type == VALTYPE_I64 ||
	       value.type == VALTYPE_F32 ||
	       value.type == VALTYPE_F64);

	if (!store_globals_grow(&store->globals, 1))
		goto error;

	globalinst = &store->globals.elts[globaladdr];

	globalinst->value = value;
	globalinst->mut = mut;

	return globaladdr;

 error:
	return INVALID_ADDR;
}

int _wasmjit_add_to_namespace(struct Store *store,
			      const char *module_name,
			      const char *name,
			      unsigned type,
			      wasmjit_addr_t addr)
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
	entry->type = type;
	entry->addr = addr;

	return 1;

 error:
	/* TODO: implement cleanup */
	assert(0);
	return 0;
}

int wasmjit_import_memory(struct Store *store,
			  const char *module_name,
			  const char *name,
			  size_t size, size_t max)
{
	wasmjit_addr_t memaddr =
		_wasmjit_add_memory_to_store(store, size, max);

	if (memaddr == INVALID_ADDR)
		goto error;

	/* now add it to namespace */
	if (!_wasmjit_add_to_namespace(store, module_name, name,
				       IMPORT_DESC_TYPE_MEM, memaddr))
	    goto error;

	return 1;

 error:
	/* TODO: implement cleanup */
	assert(0);
	return 0;
}

int wasmjit_import_function(struct Store *store,
			    const char *module_name,
			    const char *name,
			    void *funcptr,
			    size_t n_inputs,
			    unsigned *input_types,
			    size_t n_outputs, unsigned *output_types)
{
	wasmjit_addr_t funcaddr;
	struct MemoryReferences m = {0, NULL};

	funcaddr = _wasmjit_add_function_to_store(store,
						  funcptr, 0,
						  n_inputs, input_types,
						  n_outputs, output_types,
						  m);
	if (funcaddr == INVALID_ADDR)
		goto error;

	/* now add it to namespace */
	if (!_wasmjit_add_to_namespace(store, module_name, name,
				       IMPORT_DESC_TYPE_FUNC, funcaddr))
	    goto error;

	return 1;

 error:
	/* TODO: cleanup */
	assert(0);
	return 0;

}

int wasmjit_import_table(struct Store *store,
			 const char *module_name,
			 const char *name,
			 unsigned elemtype,
			 size_t length,
			 size_t max)
{
	wasmjit_addr_t tableaddr;

	tableaddr = _wasmjit_add_table_to_store(store,
						elemtype,
						length,
						max);
	if (tableaddr == INVALID_ADDR)
		goto error;

	if (!_wasmjit_add_to_namespace(store, module_name, name,
				       IMPORT_DESC_TYPE_TABLE, tableaddr))
	    goto error;

	return 1;

 error:
	/* TODO: cleanup */
	assert(0);
	return 0;
}
