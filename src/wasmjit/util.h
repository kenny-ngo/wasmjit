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

#ifndef __WASMJIT__UTIL_H__
#define __WASMJIT__UTIL_H__

#include <wasmjit/vector.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

__attribute__ ((unused))
static uint32_t uint32_t_swap_bytes(uint32_t data)
{
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	return data;
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	uint32_t _4 = data >> 24;
	uint32_t _3 = (data >> 16) & 0xFF;
	uint32_t _2 = (data >> 8) & 0xFF;
	uint32_t _1 = (data >> 0) & 0xFF;
	return _4 | (_3 << 8) | (_2 << 16) | (_1 << 24);
#else
#error Unsupported Architecture
#endif
}

__attribute__ ((unused))
static uint64_t uint64_t_swap_bytes(uint64_t data)
{
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	return data;
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	uint64_t bottom = uint32_t_swap_bytes(data & 0xffffffff);
	uint64_t top = uint32_t_swap_bytes((data >> 32) & 0xffffffff);
	return (bottom << 32) | top;
#else
#error Unsupported Architecture
#endif
}

__attribute__ ((unused))
static void encode_le_uint64_t(uint64_t val, char *buf)
{
	uint64_t le_val = uint64_t_swap_bytes(val);
	memcpy(buf, &le_val, sizeof(le_val));
}

__attribute__ ((unused, malloc))
static void *wasmjit_alloc_vector(size_t n_elts, size_t elt_size, size_t *alloced) {
	size_t size;
	if (__builtin_umull_overflow(n_elts, elt_size, &size)) {
		return NULL;
	}

	if (alloced)
		*alloced = size;

	return malloc(size);
}

__attribute__ ((unused, malloc))
static void *wasmjit_copy_buf(void *buf, size_t n_elts, size_t elt_size)
{
	void *newbuf;
	size_t size;
	newbuf = wasmjit_alloc_vector(n_elts, elt_size, &size);
	if (!newbuf)
		return NULL;

	memcpy(newbuf, buf, size);
	return newbuf;
}

struct SizedBuffer {
	size_t n_elts;
	char *elts;
};

DECLARE_VECTOR_GROW(buffer, struct SizedBuffer);

int output_buf(struct SizedBuffer *sstack, const void *buf,
	       size_t n_elts);

#define ARRAY_LEN(arr) (sizeof(arr) / sizeof(arr[0]))
#define NUMVALS(...)  (sizeof((int[]){__VA_ARGS__})/sizeof(int))

#define CATAGAIN(a, b) a ## b
#define CAT(a, b) CATAGAIN(a, b)

#endif
