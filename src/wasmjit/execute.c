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

#include <wasmjit/execute.h>

#include <wasmjit/ast.h>
#include <wasmjit/compile.h>
#include <wasmjit/runtime.h>
#include <wasmjit/util.h>
#include <wasmjit/tls.h>

#include <sys/mman.h>

__attribute__((noreturn))
static void trap(void)
{
	asm("int $4");
	__builtin_unreachable();
}

wasmjit_tls_key_t store_key;

__attribute__((constructor))
static void init_store_key(void)
{
	if (!wasmjit_init_tls_key(&store_key, NULL))
		abort();
}

static const struct Store *_get_store(void)
{
	const struct Store *store;
	if (!wasmjit_get_tls_key(store_key, &store))
		return NULL;
	return store;
}

static int _set_store(const struct Store *store)
{
	return wasmjit_set_tls_key(store_key, store);
}
void *_resolve_indirect_call(const struct TableInst *tableinst,
			     const struct FuncType *expected_type,
			     uint32_t idx)
{
	const struct Store *store = _get_store();
	struct FuncInst *funcinst;
	wasmjit_addr_t faddr;
	if (idx >= tableinst->length)
		trap();

	faddr = tableinst->data[idx];
	if (!faddr)
		trap();

	funcinst = &store->funcs.elts[faddr];
	if (!wasmjit_typelist_equal(funcinst->type.n_inputs,
				    funcinst->type.input_types,
				    expected_type->n_inputs,
				    expected_type->input_types) ||
	    !wasmjit_typelist_equal(funcinst->type.n_outputs,
				    funcinst->type.output_types,
				    expected_type->n_outputs,
				    expected_type->output_types))
		trap();

	return funcinst->compiled_code;
}

int wasmjit_execute(const struct Store *store, int argc, char *argv[])
{
	size_t i;
	int ret;

	_set_store(store);

	/* map all code in executable memory */
	for (i = 0; i < store->funcs.n_elts; ++i) {
		struct FuncInst *funcinst = &store->funcs.elts[i];
		void *newcode, *unmapped;
		struct ModuleInst *module_inst = funcinst->module_inst;
		struct ModuleTypes module_types;
		struct CodeSectionCode code;
		struct MemoryReferences memrefs;
		size_t code_size;

		if (IS_HOST(funcinst))
			continue;

		/* TODO: set module_types, code */
		assert(0);
		unmapped = wasmjit_compile_function(module_inst->types.elts,
						    &module_types,
						    &funcinst->type,
						    &code,
						    &memrefs,
						    &code_size);
		if (!unmapped)
			goto error;

		newcode = mmap(NULL, code_size, PROT_READ | PROT_WRITE,
			       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (newcode == MAP_FAILED)
			goto error;

		memcpy(newcode, unmapped, code_size);
		/* TODO: store newcode somewhere */
		assert(0);
	}

	/* resolve references */
	for (i = 0; i < store->funcs.n_elts; ++i) {
		size_t j;
		struct FuncInst *funcinst = &store->funcs.elts[i];

		if (IS_HOST(funcinst))
			continue;

		/* TODO: get memrefs here */
		assert(0);
		for (j = 0; j < 1; ++j) {
			uint64_t val;
			struct MemoryReferenceElt *melt = NULL;

			assert(sizeof(uint64_t) == sizeof(uintptr_t));

			switch (melt->type) {
			case MEMREF_CALL:
				{
					struct FuncInst *finst = &store->funcs.elts[melt->addr];
					val = (uintptr_t) finst->code;
					break;
				}
			case MEMREF_MEM_ADDR:
			case MEMREF_MEM_SIZE:
				{
					struct MemInst *minst = &store->mems.elts[melt->addr];
					val =
						melt->type == MEMREF_MEM_ADDR ? (uintptr_t) &minst->data :
						(uintptr_t) &minst->size;
					break;
				}
			case MEMREF_GLOBAL_ADDR:
				{
					struct GlobalInst *ginst = &store->globals.elts[melt->addr];
					switch (ginst->value.type) {
					case VALTYPE_I32:
						val = (uintptr_t) &ginst->value.data.i32;
						break;
					case VALTYPE_I64:
						val = (uintptr_t) &ginst->value.data.i64;
						break;
					case VALTYPE_F32:
						val = (uintptr_t) &ginst->value.data.f32;
						break;
					case VALTYPE_F64:
						val = (uintptr_t) &ginst->value.data.f64;
						break;
					default:
						assert(0);
						break;
					}
				}
				break;
			case MEMREF_MODULE_TABLES:
				val = (uintptr_t) &store->tables.elts[funcinst->module_inst->tableaddrs.elts[0]];
				break;
			case MEMREF_RESOLVE_INDIRECT_CALL:
				val = (uintptr_t) &_resolve_indirect_call;
				break;
			default:
				assert(0);
				break;
			}

			encode_le_uint64_t(val, &((char *)funcinst->code)[melt->code_offset]);
		}
	}

	/* mark code executable only */
	for (i = 0; i < store->funcs.n_elts; ++i) {
		struct FuncInst *funcinst = &store->funcs.elts[i];
		size_t code_size = 0;

		if (IS_HOST(funcinst))
			continue;

		/* TODO: get code_size here */
		assert(0);
		if (mprotect(funcinst->code, code_size, PROT_READ | PROT_EXEC))
			goto error;
	}

	/* execute start functions */
	for (i = 0; i < store->startfuncs.n_elts; ++i) {
		size_t startaddr = store->startfuncs.elts[i];

		if (store->funcs.elts[startaddr].type.n_outputs ||
		    store->funcs.elts[startaddr].type.n_inputs)
			goto error;

		/* TODO: get code here */
		assert(0);
		void (*fptr)() = 0;
		fptr();
	}

	/* find main function */
	for (i = 0; i < store->names.n_elts; ++i) {
		struct FuncInst *funcinst;
		struct NamespaceEntry *entry = &store->names.elts[i];

		if (strcmp("env", entry->module_name) ||
		    strcmp("_main", entry->name) ||
		    entry->type != IMPORT_DESC_TYPE_FUNC)
			continue;

		funcinst = &store->funcs.elts[entry->addr];
		if (funcinst->type.n_outputs == 1 &&
		    funcinst->type.output_types[0] == VALTYPE_I32) {
			if (funcinst->type.n_inputs == 0) {
				/* TODO: get code here */
				int (*fptr)() = 0;
				ret = fptr();
			} else if (funcinst->type.n_inputs == 2 &&
				   funcinst->type.input_types[0] == VALTYPE_I32 &&
				   funcinst->type.input_types[1] == VALTYPE_I32) {
				/* TODO: get code here */
				int (*fptr)(int, int) = 0;
				/* TODO: map argv into memory module of function */
				(void) argc;
				(void) argv;
				ret = fptr(0, 0);
			} else {
				continue;
			}
			break;
		}
	}

	if (i == store->names.n_elts)
		goto error;

	return ret;

 error:
	/* TODO: cleanup on error */
	assert(0);
	return -1;
}
