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

#include <wasmjit/util.h>

#include <pthread.h>

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

int wasmjit_execute(const struct Store *store, size_t startaddr)
{
	(void)store;
	(void)startaddr;

	/* TODO: map all code into memory */
	/* TODO: resolve all references */
	/* TODO: execute */

	return 0;
}
