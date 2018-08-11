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

#include <assert.h>

DEFINE_VECTOR_GROW(addrs, struct Addrs);
DEFINE_VECTOR_GROW(func_types, struct FuncTypeVector);
DEFINE_VECTOR_GROW(store_module_insts, struct ModuleInstances);
DEFINE_VECTOR_GROW(store_names, struct Namespace);
DEFINE_VECTOR_GROW(store_funcs, struct StoreFuncs);
DEFINE_VECTOR_GROW(store_tables, struct TableFuncs);
DEFINE_VECTOR_GROW(store_mems, struct StoreMems);
DEFINE_VECTOR_GROW(store_globals, struct StoreGlobals);


int wasmjit_typecheck_func(const struct FuncType *type,
			   const struct FuncInst *funcinst)
{
	return wasmjit_typelist_equal(type->n_inputs, type->input_types,
				      funcinst->type.n_inputs,
				      funcinst->type.input_types) &&
		wasmjit_typelist_equal(FUNC_TYPE_N_OUTPUTS(type),
				       FUNC_TYPE_OUTPUT_TYPES(type),
				       FUNC_TYPE_N_OUTPUTS(&funcinst->type),
				       FUNC_TYPE_OUTPUT_TYPES(&funcinst->type));
}

int wasmjit_typecheck_table(const struct TableType *type,
			    const struct TableInst *tableinst)
{
	return (tableinst->elemtype == type->elemtype &&
		tableinst->length >= type->limits.min &&
		(!type->limits.max ||
		 (type->limits.max && tableinst->max &&
		  tableinst->max <= type->limits.max)));
}

int wasmjit_typecheck_memory(const struct MemoryType *type,
			     const struct MemInst *meminst)
{
	size_t msize = meminst->size / WASM_PAGE_SIZE;
	size_t mmax = meminst->max / WASM_PAGE_SIZE;
	return (msize >= type->limits.min &&
		(!type->limits.max ||
		 (type->limits.max && mmax &&
		  mmax <= type->limits.max)));
}

int wasmjit_typecheck_global(const struct GlobalType *globaltype,
			     const struct GlobalInst *globalinst)
{
	return globalinst->value.type != globaltype->valtype ||
		globalinst->mut != globaltype->mut;
}

int _wasmjit_create_func_type(struct FuncType *ft,
			      size_t n_inputs,
			      wasmjit_valtype_t *input_types,
			      size_t n_outputs,
			      wasmjit_valtype_t *output_types)
{
	assert(n_outputs <= 1);
	assert(n_inputs <= sizeof(ft->input_types) / sizeof(ft->input_types[0]));
	memset(ft, 0, sizeof(*ft));

	ft->n_inputs = n_inputs;
	memcpy(ft->input_types, input_types, sizeof(input_types[0]) * n_inputs);

	if (n_outputs) {
		ft->output_type = output_types[0];
	} else {
		ft->output_type = VALTYPE_NULL;
	}

	return 1;
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
		meminst->data = calloc(size, 1);
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
					      struct ModuleInst *module_inst,
					      void *code,
					      size_t n_inputs,
					      wasmjit_valtype_t *input_types,
					      size_t n_outputs,
					      wasmjit_valtype_t *output_types)
{
	struct FuncInst *funcinst;
	wasmjit_addr_t funcaddr = store->funcs.n_elts;

	if (!store_funcs_grow(&store->funcs, 1))
		goto error;

	funcinst = &store->funcs.elts[funcaddr];

	if (!_wasmjit_create_func_type(&funcinst->type,
				       n_inputs, input_types,
				       n_outputs, output_types))
		goto error;

	funcinst->module_inst = module_inst;
	funcinst->compiled_code = code;

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
		tableinst->data = calloc(length, sizeof(struct FuncInst *));
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

wasmjit_addr_t wasmjit_import_memory(struct Store *store,
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

	return memaddr;

 error:
	/* TODO: implement cleanup */
	assert(0);
	return INVALID_ADDR;
}

int wasmjit_import_function(struct Store *store,
			    const char *module_name,
			    const char *name,
			    void *funcptr,
			    size_t n_inputs,
			    wasmjit_valtype_t *input_types,
			    size_t n_outputs,
			    wasmjit_valtype_t *output_types)
{
	wasmjit_addr_t funcaddr;

	funcaddr = _wasmjit_add_function_to_store(store,
						  NULL,
						  funcptr,
						  n_inputs, input_types,
						  n_outputs, output_types);
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

int wasmjit_import_global(struct Store *store,
			  const char *module_name,
			  const char *name,
			  struct Value value,
			  unsigned mut)
{
	wasmjit_addr_t globaladdr;

	globaladdr = _wasmjit_add_global_to_store(store,
						  value,
						  mut);
	if (globaladdr == INVALID_ADDR)
		goto error;

	if (!_wasmjit_add_to_namespace(store, module_name, name,
				       IMPORT_DESC_TYPE_GLOBAL, globaladdr))
	    goto error;

	return 1;

 error:
	/* TODO: cleanup */
	assert(0);
	return 0;
}
