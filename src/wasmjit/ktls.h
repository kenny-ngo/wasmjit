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

#ifndef __KWASMJIT__KTLS_H
#define __KWASMJIT__KTLS_H

#ifndef __KERNEL__
#error Only for kernel
#endif

#include <linux/sched/task_stack.h>

struct KernelThreadLocal {
	jmp_buf *jmp_buf;
	void *stack_top;
	struct pt_regs regs;
	struct MemInst *mem_inst;
};

static inline char *ptrptr(void) {
	/* NB: use space below entry of kernel stack for our thread local info
	   if task_pt_regs(current) does not point to the bottom of the stack,
	   this will fail very badly. wasmjit_high_emscripten_invoke_main always
	   restores the original value before returning, so while we in the system
	   call it should be safe to reappropriate this space.
	 */
	return (char *)task_pt_regs(current) - sizeof(struct ThreadLocal *);
}

static inline struct KernelThreadLocal *wasmjit_get_ktls(void)
{
	struct KernelThreadLocal *toret;
	memcpy(&toret, ptrptr(), sizeof(toret));
	return toret;
}

#endif
