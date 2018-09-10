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

#include <wasmjit/sys.h>

/* platform specific */

#ifdef __KERNEL__

#include <wasmjit/ktls.h>

#include <linux/mm.h>
#include <linux/sched/task_stack.h>

void *wasmjit_map_code_segment(size_t code_size)
{
	return __vmalloc(code_size, GFP_KERNEL, PAGE_KERNEL_EXEC);
}

int wasmjit_mark_code_segment_executable(void *code, size_t code_size)
{
	/* TODO: mess with pte a la mprotect_fixup */
	(void)code;
	(void)code_size;
	return 1;
}

int wasmjit_unmap_code_segment(void *code, size_t code_size)
{
	(void)code_size;
	vfree(code);
	return 1;
}

jmp_buf *wasmjit_get_jmp_buf(void)
{
	return wasmjit_get_ktls()->jmp_buf;
}

int wasmjit_set_jmp_buf(jmp_buf *jmpbuf)
{
	wasmjit_get_ktls()->jmp_buf = jmpbuf;
	return 1;
}

void *wasmjit_stack_top(void)
{
	return wasmjit_get_ktls()->stack_top;
}

int wasmjit_set_stack_top(void *stack_top)
{
	wasmjit_get_ktls()->stack_top = stack_top;
	return 1;
}

#else

#include <wasmjit/tls.h>

#include <sys/mman.h>

void *wasmjit_map_code_segment(size_t code_size)
{
	void *newcode;
	newcode = mmap(NULL, code_size, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (newcode == MAP_FAILED)
		return NULL;
	return newcode;
}

int wasmjit_mark_code_segment_executable(void *code, size_t code_size)
{
	return !mprotect(code, code_size, PROT_READ | PROT_EXEC);
}


int wasmjit_unmap_code_segment(void *code, size_t code_size)
{
	return !munmap(code, code_size);
}

wasmjit_tls_key_t jmp_buf_key;

__attribute__((constructor))
static void _init_jmp_buf(void)
{
	wasmjit_init_tls_key(&jmp_buf_key, NULL);
}

jmp_buf *wasmjit_get_jmp_buf(void)
{
	jmp_buf *toret;
	int ret;
	ret = wasmjit_get_tls_key(jmp_buf_key, &toret);
	if (!ret) return NULL;
	return toret;
}

int wasmjit_set_jmp_buf(jmp_buf *jmpbuf)
{
	return wasmjit_set_tls_key(jmp_buf_key, jmpbuf);
}

wasmjit_tls_key_t stack_top_key;

__attribute__((constructor))
static void _init_stack_top(void)
{
	wasmjit_init_tls_key(&stack_top_key, NULL);
}

void *wasmjit_stack_top(void)
{
	jmp_buf *toret;
	int ret;
	ret = wasmjit_get_tls_key(stack_top_key, &toret);
	if (!ret) return NULL;
	return toret;
}

int wasmjit_set_stack_top(void *stack_top)
{
	return wasmjit_set_tls_key(stack_top_key, stack_top);
}

#endif

__attribute__((noreturn))
void wasmjit_trap(int reason)
{
	longjmp(*wasmjit_get_jmp_buf(), reason + 1);
}
