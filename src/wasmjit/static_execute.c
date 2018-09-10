/* -*-mode:c; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <wasmjit/execute.h>

#include <wasmjit/ast.h>
#include <wasmjit/runtime.h>

int wasmjit_invoke_function(struct FuncInst *funcinst,
			    union ValueUnion *values,
			    union ValueUnion *out)
{
	if (funcinst->type.n_inputs == 0 &&
	    funcinst->type.output_type == VALTYPE_I32) {
		uint32_t (*func)(void) = funcinst->compiled_code;
		uint32_t mout;

		mout = func();
		if (out)
			out->i32 = mout;

		return 1;
	} else if (funcinst->type.n_inputs == 2 &&
		   funcinst->type.input_types[0] == VALTYPE_I32 &&
		   funcinst->type.input_types[1] == VALTYPE_I32 &&
		   funcinst->type.output_type == VALTYPE_I32) {
		uint32_t (*func)(uint32_t, uint32_t) = funcinst->compiled_code;
		uint32_t mout;

		mout = func(values[0].i32, values[1].i32);
		if (out)
			out->i32 = mout;

		return 1;

	} else {
		return 0;
	}
}
