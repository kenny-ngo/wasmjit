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

#ifndef __WASMJIT__SYS_H__
#define __WASMJIT__SYS_H__

#ifdef __KERNEL__

#include <linux/types.h>
#include <linux/bug.h>
#include <linux/string.h>
#include <linux/mm.h>

#ifdef NDEBUG
#define assert(x)
#else
#define assert(x) BUG_ON(!(x))
#endif

#define OFF (sizeof(size_t) * 2)

__attribute__((unused))
static void *malloc(size_t size)
{
	char *ret;
	size_t cap = 1;

	if (!size)
		return NULL;

	while ((size + OFF) > cap) {
		cap <<= 1;
	}

	ret = kvmalloc(cap, GFP_KERNEL);
	if (!ret)
		return NULL;
	memcpy(ret, &cap, sizeof(cap));
	memcpy(ret + sizeof(cap), &size, sizeof(size));
	return &ret[OFF];
}

__attribute__((unused))
static void free(void *ptr)
{
	if (ptr)
		kvfree(&((char *)ptr)[-OFF]);
}

__attribute__((unused))
static void *calloc(size_t nmemb, size_t elt_size)
{
	char *ret;
	size_t size, cap;
	if (__builtin_umull_overflow(nmemb, elt_size, &size)) {
		return NULL;
	}

	if (!size)
		return NULL;

	cap = 1;
	while ((size + OFF) > cap) {
		cap <<= 1;
	}

	ret = kvzalloc(cap, GFP_KERNEL);
	if (!ret)
		return NULL;

	memcpy(ret, &cap, sizeof(cap));
	memcpy(ret + sizeof(cap), &size, sizeof(size));

	return &ret[OFF];
}

__attribute__((unused))
static void *realloc(void *previous, size_t size)
{
	char *cptr = previous, *new;

	if (!size) {
		free(previous);
		return NULL;
	}

	if (cptr) {
		size_t prev_cap;
		memcpy(&prev_cap, &cptr[-OFF], sizeof(prev_cap));

		if ((size + OFF) < prev_cap) {
			/* NB: we don't support shrinking */
			memcpy(&cptr[-OFF + sizeof(prev_cap)], &size, sizeof(size));
			return cptr;
		}
	}

	new = malloc(size);
	if (!new)
		return NULL;

	if (cptr) {
		size_t prev_size;
		memcpy(&prev_size, &cptr[-OFF + sizeof(size_t)], sizeof(prev_size));
		memcpy(new, cptr, prev_size);
		free(cptr);
	}

	return new;
}

__attribute__((unused))
static char *strdup(const char *s)
{
	size_t l = strlen(s);
	char *n = malloc(l + 1);
	if (!n)
		return NULL;
	memcpy(n, s, l + 1);
	return n;
}

#define PRIx32 "x"
#define PRIu32 "u"
#define INT32_MAX 2147483647
#define INT32_MIN (-2147483648)

#if defined __GNUC__ && defined __GNUC_MINOR__
# define __GNUC_PREREQ(maj, min) \
	((__GNUC__ << 16) + __GNUC_MINOR__ >= ((maj) << 16) + (min))
#else
# define __GNUC_PREREQ(maj, min) 0
#endif

# if __GNUC_PREREQ (3, 3)
#  define INFINITY (__builtin_inff ())
# else
#  define INFINITY HUGE_VALF
# endif

#if __GNUC_PREREQ (3, 3)
#  define NAN (__builtin_nanf (""))
# else
/* This will raise an "invalid" exception outside static initializers,
   but is the best that can be done in ISO C while remaining a
   constant expression.  */
#  define NAN (0.0f / 0.0f)
# endif

typedef int64_t intmax_t;


#ifdef __x86_64__
typedef unsigned long __jmp_buf[8];
#else
#error Only works on x86_64
#endif

typedef struct __jmp_buf_tag {
	__jmp_buf __jb;
	unsigned long __fl;
	unsigned long __ss[128/sizeof(long)];
} jmp_buf[1];

int setjmp(jmp_buf);
void longjmp(jmp_buf, int) __attribute__((noreturn));

#else

#include <stddef.h>
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <math.h>
#include <setjmp.h>

#endif

#endif
