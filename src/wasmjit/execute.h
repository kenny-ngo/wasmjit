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

#ifndef __WASMJIT__EXECUTE_H__
#define __WASMJIT__EXECUTE_H__

#include <wasmjit/runtime.h>

#define WASMJIT_INVOKE_TRAP_OFFSET 0x100
#define WASMJIT_INVOKE_IS_TRAP_ERROR(ret) ((ret) <= -WASMJIT_INVOKE_TRAP_OFFSET)
#define WASMJIT_INVOKE_ENCODE_TRAP_ERROR(ret) (-(ret) - WASMJIT_INVOKE_TRAP_OFFSET)
#define WASMJIT_INVOKE_DECODE_TRAP_ERROR(ret) (-(ret) - WASMJIT_INVOKE_TRAP_OFFSET)

int wasmjit_invoke_function(struct FuncInst *funcinst, union ValueUnion *values,
			    union ValueUnion *out);

#endif
