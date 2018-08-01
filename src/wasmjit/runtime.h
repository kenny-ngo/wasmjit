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

struct MemoryReferences {
	size_t n_elts;
	struct MemoryReferenceElt {
		enum {
			MEMREF_CALL,
			MEMREF_MEM_ADDR,
			MEMREF_MEM_MAX,
		} type;
		size_t extra_offset;
		size_t code_offset;
		size_t addr;
	} *elts;
};

__attribute__ ((unused))
static DEFINE_VECTOR_GROW(memrefs, struct MemoryReferences);

struct Store {
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
		} *elts;
	} funcs;
	struct StoreMems {
		size_t n_elts;
		struct MemInst {
			char *data;
			uint32_t max;
			int has_max;
		} *elts;
	} mems;
};

#endif
