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

#ifndef __WASMJIT__RUNTIME_H__
#define __WASMJIT__RUNTIME_H__

#include <wasmjit/vector.h>

#include <stddef.h>
#include <stdint.h>


struct Addrs {
	size_t n_elts;
	size_t *elts;
};

__attribute__ ((unused))
static DEFINE_VECTOR_GROW(addrs, struct Addrs);

struct MemoryReferences {
	size_t n_elts;
	struct MemoryReferenceElt {
		enum {
			MEMREF_CALL,
			MEMREF_MEM_ADDR,
			MEMREF_MEM_SIZE,
			MEMREF_MEM,
			MEMREF_MEM_BOX,
		} type;
		size_t code_offset;
		size_t addr;
	} *elts;
};

__attribute__ ((unused))
static DEFINE_VECTOR_GROW(memrefs, struct MemoryReferences);

#define IS_HOST(funcinst) (!(funcinst)->code_size)

struct Store {
	struct Namespace {
		size_t n_elts;
		struct NamespaceEntry {
			char *module_name;
			char *name;
			unsigned type;
			size_t addr;
		} *elts;
	} names;
	struct StoreFuncs {
		size_t n_elts;
		struct FuncInst {
			struct FuncInstType {
				size_t n_inputs;
				unsigned *input_types;
				size_t n_outputs;
				unsigned *output_types;
			} type;
			void *code;
			size_t code_size;
			struct MemoryReferences memrefs;
		} *elts;
	} funcs;
	struct TableFuncs {
		size_t n_elts;
		struct TableInst {
			void **data;
			int elemtype;
			size_t length;
			size_t max;
		} *elts;
	} tables;
	struct StoreMems {
		size_t n_elts;
		struct MemInst {
			char *data;
			size_t size;
			size_t max; /* max of 0 means no max */
		} *elts;
	} mems;
	struct Addrs startfuncs;
};

__attribute__ ((unused))
static DEFINE_VECTOR_GROW(store_names, struct Namespace);
__attribute__ ((unused))
static DEFINE_VECTOR_GROW(store_funcs, struct StoreFuncs);
__attribute__ ((unused))
static DEFINE_VECTOR_GROW(store_tables, struct TableFuncs);
__attribute__ ((unused))
static DEFINE_VECTOR_GROW(store_mems, struct StoreMems);

int wasmjit_import_function(struct Store *store,
			    const char *module_name,
			    const char *name,
			    void *funcaddr,
			    size_t n_inputs,
			    unsigned *input_types,
			    size_t n_outputs, unsigned *output_types);

#endif
