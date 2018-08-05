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

#ifndef __WASMJIT__TLS_H__
#define __WASMJIT__TLS_H__

#include <pthread.h>
#include <errno.h>

typedef pthread_key_t wasmjit_tls_key_t;

__attribute__((unused))
static int wasmjit_init_tls_key(wasmjit_tls_key_t *pkey, void (*destr)(void *))
{
	return !pthread_key_create(pkey, destr);
}

__attribute__((unused))
static int wasmjit_get_tls_key(wasmjit_tls_key_t key, const void *newval)
{
	errno = 0;
	*(void **)newval = pthread_getspecific(key);
	return (*(void **)newval || !errno);
}

__attribute__((unused))
static int wasmjit_set_tls_key(wasmjit_tls_key_t key, const void *val)
{
	return !pthread_setspecific(key, val);
}

#endif
