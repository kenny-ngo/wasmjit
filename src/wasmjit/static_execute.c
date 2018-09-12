/* -*-mode:c; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <wasmjit/execute.h>

#include <wasmjit/ast.h>
#include <wasmjit/runtime.h>

int wasmjit_invoke_function(struct FuncInst *funcinst,
			    union ValueUnion *values,
			    union ValueUnion *out)
{
	return _wasmjit_static_invoke_function(funcinst, value, out);
}
