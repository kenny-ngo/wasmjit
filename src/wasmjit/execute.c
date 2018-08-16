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

#include <wasmjit/execute.h>

#include <wasmjit/ast.h>
#include <wasmjit/compile.h>
#include <wasmjit/runtime.h>
#include <wasmjit/util.h>

int wasmjit_invoke_function(struct FuncInst *funcinst,
			    union ValueUnion *values,
			    union ValueUnion *out)
{
	int ret = 0;
	void *unmapped = NULL;
	union ValueUnion (*mapped)(union ValueUnion *) = NULL;
	size_t code_size;
	union ValueUnion lout;

	unmapped = wasmjit_compile_invoker(&funcinst->type,
					   funcinst->compiled_code,
					   &code_size);
	if (!unmapped)
		goto error;

	mapped = wasmjit_map_code_segment(code_size);
	if (!mapped)
		goto error;

	memcpy(mapped, unmapped, code_size);

	if (!wasmjit_mark_code_segment_executable(mapped, code_size))
		goto error;

	lout = mapped(values);
	if (out)
		*out = lout;
	ret = 1;

 error:
	if (mapped)
		wasmjit_unmap_code_segment(mapped, code_size);

	if (unmapped)
		free(unmapped);

	return ret;
}
