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

#ifndef __WASMJIT__STATIC_RUNTIME_H__
#define __WASMJIT__STATIC_RUNTIME_H__

#include <wasmjit/runtime.h>
#include <wasmjit/util.h>

#include <stdint.h>

enum {
	GLOBAL_CONST_INIT,
	GLOBAL_GLOBAL_INIT,
};

struct StaticModuleInst;

struct GlobalInit {
	unsigned init_type;
	union GlobalInitUnion {
		struct Value constant;
		struct MGStruct {
			struct StaticModuleInst *module;
			struct GlobalInst *global;
		} parent;
	} init;
};

struct ElementInst {
	size_t tableidx;
	unsigned offset_source_type;
	union ElementInstUnion {
		struct Value constant;
		struct GlobalInst *global;
	} offset;
	size_t n_funcidxs;
	uint32_t *funcidxs;
};

struct DataInst {
	size_t memidx;
	unsigned offset_source_type;
	union DataInstUnion {
		struct Value constant;
		struct GlobalInst *global;
	} offset;
	size_t buf_size;
	char *buf;
};

struct StaticModuleInst {
	struct ModuleInst module;

	DEFINE_ANON_VECTOR(struct FuncType) func_types;
	DEFINE_ANON_VECTOR(struct TableType) table_types;
	DEFINE_ANON_VECTOR(struct MemoryType) mem_types;
	DEFINE_ANON_VECTOR(struct GlobalType) global_types;

	DEFINE_ANON_VECTOR(struct GlobalInit) global_inits;
	DEFINE_ANON_VECTOR(struct DataInst) datas;
	DEFINE_ANON_VECTOR(struct ElementInst) elements;

	int initted;

	struct FuncInst *start_func;
};

void wasmjit_init_static_module(struct StaticModuleInst *smi);


#define WASM_SYMBOL(_module, _name, _type) _module ## __ ## _name ## __ ##  _type

#define WASM_FUNC_SYMBOL(_module, _name) WASM_SYMBOL(_module, _name, func)
#define WASM_TABLE_SYMBOL(_module, _name) WASM_SYMBOL(_module, _name, table)
#define WASM_MEMORY_SYMBOL(_module, _name) WASM_SYMBOL(_module, _name, mem)
#define WASM_GLOBAL_SYMBOL(_module, _name) WASM_SYMBOL(_module, _name, global)
#define WASM_MODULE_SYMBOL(___module) CAT(___module,  _module)

#define _DEFINE_WASM_GLOBAL(_module, _name, _init, _type, _member, _mut)	\
	struct GlobalInst WASM_SYMBOL(_module, _name, global) = {	\
		.value = {						\
			.type = (_type),				\
			.data = {					\
				._member = (_init),			\
			},						\
		},							\
		.mut = (_mut),						\
	};

#define DEFINE_WASM_GLOBAL(...) _DEFINE_WASM_GLOBAL(CURRENT_MODULE, __VA_ARGS__)

/* All these crazy macros are here because we have to statically generate a function
   with a compiled code signature from a function with host function signature */

#define CTYPE_VALTYPE_I32 uint32_t
#define CTYPE_VALTYPE_NULL void
#define CTYPE(val) CTYPE_ ## val

#define VALUE_MEMBER_VALTYPE_I32 i32
#define VALUE_MEMBER(val) VALUE_MEMBER_ ## val

#define ITER __KMAP

#define CT(to, n, t) CTYPE(t) CAT(arg, n)
#define EXPAND_PARAMS(_n, ...) ITER(_n, CT, ##__VA_ARGS__)

#define ARG(to, n, t) CAT(arg, n)
#define EXPAND_ARGS(_n, ...) ITER(_n, ARG, ##__VA_ARGS__)

#define VALUE(to, n, t) args[to - n]. VALUE_MEMBER(t)
#define EXPAND_VALUES(_n, ...) ITER(_n, VALUE, ##__VA_ARGS__)

#define COMMA_0
#define COMMA_1 ,
#define COMMA_2 ,
#define COMMA_3 ,
#define COMMA_IF_NOT_EMPTY(_n) CAT(COMMA_, _n)

#define _DEFINE_INVOKER_VALTYPE_NULL(_module, _name, _fptr, _unused, _n, ...) \
	union ValueUnion CAT(CAT(CAT(_module,  __), _name),  __emscripten__hostfunc__invoker)(union ValueUnion *args) \
	{								\
		union ValueUnion rout;					\
		(void)args;						\
		(*_fptr)(EXPAND_VALUES(_n, ##__VA_ARGS__) COMMA_IF_NOT_EMPTY(_n) &WASM_FUNC_SYMBOL(_module, _name));	\
		memset(&rout.null, 0, sizeof(rout.null));		\
		return rout;						\
	}								\

#define _DEFINE_INVOKER_VALTYPE_NON_NULL(_module, _name, _fptr, _output, _n, ...) \
	union ValueUnion CAT(CAT(CAT(_module,  __), _name),  __emscripten__hostfunc__invoker)(union ValueUnion *args) \
	{								\
		CTYPE(_output) out;					\
		union ValueUnion rout;					\
		(void)args;						\
		out = (*_fptr)(EXPAND_VALUES(_n, ##__VA_ARGS__) COMMA_IF_NOT_EMPTY(_n) &WASM_FUNC_SYMBOL(_module, _name));	\
		rout. VALUE_MEMBER(_output) = out;			\
		return rout;						\
	}								\

#define _DEFINE_INVOKER_VALTYPE_I32 _DEFINE_INVOKER_VALTYPE_NON_NULL

#define _DEFINE_INVOKER(_module, _name, _fptr, _output, _n, ...) \
	_DEFINE_INVOKER_ ## _output(_module, _name, _fptr, _output, _n, ##__VA_ARGS__)

#define _DEFINE_WASM_FUNCTION(_module, _name, _fptr, _output, _n, ...)	\
	extern struct FuncInst WASM_FUNC_SYMBOL(_module, _name);	\
									\
	_DEFINE_INVOKER(_module, _name, _fptr, _output, _n, ##__VA_ARGS__) \
									\
	CTYPE(_output) CAT(CAT(CAT(_module,  __), _name),  __emscripten__hostfunc__)(EXPAND_PARAMS(_n, ##__VA_ARGS__)) \
	{								\
		return (*_fptr)(EXPAND_ARGS(_n, ##__VA_ARGS__) COMMA_IF_NOT_EMPTY(_n) &WASM_FUNC_SYMBOL(_module, _name)); \
	}								\
									\
	struct FuncInst WASM_FUNC_SYMBOL(_module, _name) = {		\
		.module_inst = &WASM_MODULE_SYMBOL(_module),		\
		.compiled_code = CAT(CAT(CAT(_module,  __), _name),  __emscripten__hostfunc__),	\
		.invoker = CAT(CAT(CAT(_module,  __), _name),  __emscripten__hostfunc__invoker), \
		.type = {						\
			.n_inputs = _n,		\
			.input_types = { __VA_ARGS__ },			\
			.output_type = _output,				\
		}							\
	};

#define DEFINE_WASM_FUNCTION(...) _DEFINE_WASM_FUNCTION(CURRENT_MODULE, __VA_ARGS__)

#define _DEFINE_WASM_TABLE(_module, _name, _elemtype, _length_, _max)	\
	struct FuncInst *WASM_SYMBOL(_module, _name, buffer) [(_max)];	\
	struct TableInst WASM_TABLE_SYMBOL(_module, _name) = { \
		.data = WASM_SYMBOL(_module, _name, buffer),		\
		.elemtype = ELEMTYPE_ANYFUNC,				\
		.length = (_max),					\
		.max = (_max),						\
	};

#define DEFINE_WASM_TABLE(...) _DEFINE_WASM_TABLE(CURRENT_MODULE, __VA_ARGS__)

#define _DEFINE_WASM_MEMORY(_module, _name, _min, _max)	\
	char WASM_SYMBOL(_module, _name,  buffer)[(_min) * WASM_PAGE_SIZE]; \
	struct MemInst WASM_MEMORY_SYMBOL(_module, _name) = {\
		.data = WASM_SYMBOL(_module, _name, buffer),		\
		.size = (_min) * WASM_PAGE_SIZE,			\
		.max = (_min) * WASM_PAGE_SIZE,				\
	};

#define DEFINE_WASM_MEMORY(...) _DEFINE_WASM_MEMORY(CURRENT_MODULE, __VA_ARGS__)

#endif
