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
#include <wasmjit/runtime.h>
#include <wasmjit/util.h>

#include <pthread.h>

#include <sys/mman.h>

__attribute__ ((unused))
static void encode_le_uint64_t(uint64_t val, char *buf)
{
	uint64_t le_val = uint64_t_swap_bytes(val);
	memcpy(buf, &le_val, sizeof(le_val));
}

pthread_key_t meminst_key;

__attribute__((constructor))
static void init_meminst_key() {
	if (pthread_key_create(&meminst_key, NULL))
		abort();
}

void *wasmjit_get_base_address()
{
	struct MemInst **mb = pthread_getspecific(meminst_key);
	if (!mb) return NULL;
	return (*mb)->data;
}

int wasmjit_execute(const struct Store *store)
{
	size_t i;

	struct Meminst *meminst_box;

	if (pthread_setspecific(meminst_key, &meminst_box))
		goto error;

	/* map all code in executable memory */
	for (i = 0; i < store->funcs.n_elts; ++i) {
		struct FuncInst *funcinst = &store->funcs.elts[i];
		void *newcode;

		if (funcinst->is_host)
			continue;

		newcode = mmap(NULL, funcinst->code_size, PROT_READ | PROT_WRITE,
			       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (newcode == MAP_FAILED)
			goto error;

		memcpy(newcode, funcinst->code, funcinst->code_size);

		free(funcinst->code);
		funcinst->code = newcode;
	}

	/* resolve references */
	for (i = 0; i < store->funcs.n_elts; ++i) {
		size_t j;
		struct FuncInst *funcinst = &store->funcs.elts[i];

		if (funcinst->is_host)
			continue;

		for (j = 0; j < funcinst->memrefs.n_elts; ++j) {
			uint64_t val;
			struct MemoryReferenceElt *melt = &funcinst->memrefs.elts[j];

			assert(sizeof(uint64_t) == sizeof(uintptr_t));

			switch (melt->type) {
			case MEMREF_CALL:
				{
					struct FuncInst *finst = &store->funcs.elts[melt->addr];
					val = (uintptr_t) finst->code;
					break;
				}
			case MEMREF_MEM:
			case MEMREF_MEM_ADDR:
			case MEMREF_MEM_SIZE:
				{
					struct MemInst *minst = &store->mems.elts[melt->addr];
					val =
						melt->type == MEMREF_MEM_ADDR ? (uintptr_t) &minst->size :
						melt->type == MEMREF_MEM_SIZE ? (uintptr_t) &minst->data :
						(uintptr_t) minst;
					break;
				}
			case MEMREF_MEM_BOX:
				{
					val = (uintptr_t) &meminst_box;

					break;
				}
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

		if (funcinst->is_host)
			continue;

		if (mprotect(funcinst->code, funcinst->code_size, PROT_READ | PROT_EXEC))
			goto error;
	}

	/* execute start functions */
	for (i = 0; i < store->startfuncs.n_elts; ++i) {
		size_t startaddr = store->startfuncs.elts[i];

		if (store->funcs.elts[startaddr].type.n_outputs ||
		    store->funcs.elts[startaddr].type.n_inputs)
			goto error;

		void (*fptr)() = store->funcs.elts[startaddr].code;
		fptr();
	}

	return 0;

 error:
	/* TODO: cleanup on error */
	assert(0);
	return -1;
}
