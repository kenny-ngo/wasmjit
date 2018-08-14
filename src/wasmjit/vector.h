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

#ifndef __WASMJIT__VECTOR_H__
#define __WASMJIT__VECTOR_H__

#include <assert.h>
#include <string.h>

int wasmjit_vector_set_size(void *, size_t *, size_t, size_t);

#define DEFINE_ANON_VECTOR(type) \
	struct {		 \
		size_t n_elts;	 \
		type *elts;	 \
	}

#define VECTOR_GROW(sstack, _n_elts)					\
	wasmjit_vector_set_size(&(sstack)->elts,			\
				&(sstack)->n_elts,			\
				((sstack)->n_elts + (_n_elts)),		\
				sizeof((sstack)->elts[0]))



#define DECLARE_VECTOR_GROW(name, _type)					\
	int name ## _grow(_type *sstack, size_t n_elts)


#define DEFINE_VECTOR_GROW(name, _type)					\
	int name ## _grow(_type *sstack, size_t n_elts) {		\
		return wasmjit_vector_set_size(&sstack->elts,		\
					       &sstack->n_elts,		\
					       (sstack->n_elts + n_elts), \
					       sizeof(sstack->elts[0])); \
	}

#define DEFINE_VECTOR_TRUNCATE(name, _type)				\
	int name ## _truncate(_type *sstack, size_t amt) {		\
		assert(amt <= sstack->n_elts);				\
									\
		return wasmjit_vector_set_size(&sstack->elts,		\
					       &sstack->n_elts,		\
					       amt,			\
					       sizeof(sstack->elts[0])); \
	}

#endif
