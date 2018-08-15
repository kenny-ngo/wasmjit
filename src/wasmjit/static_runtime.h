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

struct GlobalInit {
	unsigned init_type;
	union GlobalInitUnion {
		struct Value constant;
		struct GlobalInit *parent;
	} init;
};

struct ElementInst {
	size_t tableidx;
	unsigned offset_source_type;
	union ElementInstUnion {
		struct Value constant;
		struct GlobalInit *global;
	} offset;
	size_t n_funcidxs;
	uint32_t *funcidxs;
};

struct DataInst {
	size_t memidx;
	unsigned offset_source_type;
	union DataInstUnion {
		struct Value constant;
		struct GlobalInit *global;
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
	struct GlobalInit WASM_SYMBOL(_module, _name, global_init) = {	\
		.init_type = GLOBAL_CONST_INIT,				\
		.init = {						\
			.constant = {					\
				.type = (_type),			\
				.data = {				\
					._member = (_init),		\
				}					\
			}						\
		}							\
	};								\
									\
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

#define NUMARGS(...)  (sizeof((wasmjit_valtype_t[]){__VA_ARGS__})/sizeof(wasmjit_valtype_t))

/* All these crazy macros are here because we have to statically generate a function
   with a compiled code signature from a function with host function signature */

#define CTYPE_VALTYPE_I32 uint32_t
#define CTYPE_VALTYPE_NULL void
#define CTYPE(val) CTYPE_ ## val

#define PP_NARG(...) \
	PP_NARG_(dummy, ##__VA_ARGS__, PP_RSEQ_N())
#define PP_NARG_(...) \
	PP_ARG_N(__VA_ARGS__)
#define PP_ARG_N( \
		 _1, _2, _3, _4, _5, _6, _7, _8, _9,_10, \
	         _11,_12,_13,_14,_15,_16,_17,_18,_19,_20, \
	         _21,_22,_23,_24,_25,_26,_27,_28,_29,_30, \
	         _31,_32,_33,_34,_35,_36,_37,_38,_39,_40, \
	         _41,_42,_43,_44,_45,_46,_47,_48,_49,_50, \
	         _51,_52,_53,_54,_55,_56,_57,_58,_59,_60, \
	         _61,_62,_63,N,...) N
#define PP_RSEQ_N() \
	62,61,60,                   \
		59,58,57,56,55,54,53,52,51,50, \
		49,48,47,46,45,44,43,42,41,40, \
		39,38,37,36,35,34,33,32,31,30, \
		29,28,27,26,25,24,23,22,21,20, \
		19,18,17,16,15,14,13,12,11,10, \
		9,8,7,6,5,4,3,2,1,0

#define SECOND(a, b, ...) b
#define IS_PROBE(...) SECOND(__VA_ARGS__, 0)
#define PROBE() ~, 1
#define NOT(x) IS_PROBE(CAT(_NOT_, x))
#define _NOT_0 PROBE()
#define BOOL(x) NOT(NOT(x))
#define FIRST(a, ...) a
#define HAS_ARGS(...) BOOL(FIRST(_END_OF_ARGUMENTS_ __VA_ARGS__)())
#define _END_OF_ARGUMENTS_() 0

#define ITER_0(m, n)
#define ITER_1(m, n, first, ...) m(n, first) CAT(ITER_, PP_NARG(__VA_ARGS__))(m, PP_NARG(__VA_ARGS__) , ##__VA_ARGS__)
#define ITER_2(m, n, first, ...) m(n, first) , CAT(ITER_, PP_NARG(__VA_ARGS__))(m, PP_NARG(__VA_ARGS__) , ##__VA_ARGS__)
#define ITER_3(m, n, first, ...) m(n, first) , CAT(ITER_, PP_NARG(__VA_ARGS__))(m, PP_NARG(__VA_ARGS__),  ##__VA_ARGS__)
#define ITER(m, ...) CAT(ITER_, PP_NARG(__VA_ARGS__))(m, PP_NARG(__VA_ARGS__), ##__VA_ARGS__)

#define CT(n, t) CTYPE(t) CAT(arg, n)
#define EXPAND_PARAMS(...) ITER(CT, ##__VA_ARGS__)

#define ARG(n, t) CAT(arg, n)
#define EXPAND_ARGS(...) ITER(ARG, ##__VA_ARGS__)

#define COMMA_0
#define COMMA_1 ,
#define COMMA_IF_NOT_EMPTY(...) CAT(COMMA_, HAS_ARGS(__VA_ARGS__))

#define _DEFINE_WASM_FUNCTION(_module, _name, _fptr, _output, ...)	\
	extern struct FuncInst WASM_FUNC_SYMBOL(_module, _name);	\
									\
	CTYPE(_output) CAT(CAT(CAT(_module,  __), _name),  __emscripten__hostfunc__)(EXPAND_PARAMS(__VA_ARGS__)) { \
		return (*_fptr)(EXPAND_ARGS(__VA_ARGS__) COMMA_IF_NOT_EMPTY(__VA_ARGS__)  &WASM_FUNC_SYMBOL(_module, _name)); \
	}								\
									\
	struct FuncInst WASM_FUNC_SYMBOL(_module, _name) = {		\
		.module_inst = &WASM_MODULE_SYMBOL(_module),		\
		.compiled_code = CAT(CAT(CAT(_module,  __), _name),  __emscripten__hostfunc__),	\
		.type = {						\
			.n_inputs = NUMARGS(__VA_ARGS__),		\
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
