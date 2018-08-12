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

#include <stdint.h>

struct WasmInst;

enum {
	GLOBAL_CONST_INIT,
	GLOBAL_GLOBAL_INIT,
};

struct StaticGlobalInst {
	unsigned init_type;
	union StaticGlobalInstUnion {
		struct Value constant;
		struct WasmInst *global;
	} init;
	struct GlobalInst global;
};

struct WasmInst {
	size_t type;
	union WasmInstUnion {
		struct FuncInst func;
		struct TableInst table;
		struct MemInst mem;
		struct StaticGlobalInst global;
	} u;
};

struct FuncReference {
	struct FuncType expected_type;
	struct WasmInst *inst;
};

struct TableReference {
	struct TableType expected_type;
	struct WasmInst *inst;
};

struct MemReference {
	struct MemoryType expected_type;
	struct WasmInst *inst;
};

struct GlobalReference {
	struct GlobalType expected_type;
	struct WasmInst *inst;
};

struct ElementInst {
	size_t tableidx;
	unsigned offset_source_type;
	union ElementInstUnion {
		struct Value constant;
		struct WasmInst *global;
	} offset;
	size_t n_funcidxs;
	uint32_t *funcidxs;
};

struct DataInst {
	size_t memidx;
	unsigned offset_source_type;
	union DataInstUnion {
		struct Value constant;
		struct WasmInst *global;
	} offset;
	size_t buf_size;
	char *buf;
};

struct StaticModuleInst {
	size_t n_funcs, n_tables,
		n_mems, n_globals,
		n_datas, n_elements,
		n_imported_globals;
	struct FuncReference *funcs;
	struct TableReference *tables;
	struct MemReference *mems;
	struct GlobalReference *globals;
	struct DataInst *datas;
	struct ElementInst *elements;
	struct WasmInst *start_func;
};

#define WASM_SYMBOL(_module, _name) (_module ## _ ## _name)

#define DEFINE_WASM_GLOBAL(_module, _name, _init, _type, _member, _mut)    \
	struct WasmInst _module ## _ ## _name = {			\
		.type = IMPORT_DESC_TYPE_GLOBAL,			\
		.u = {							\
			.global = {					\
				.init_type = GLOBAL_CONST_INIT,		\
				.init = {				\
					.constant = {			\
						.type = _type,		\
						.data = {		\
							._member = _init, \
						}			\
					}				\
				},					\
				.global = {				\
					.value = {			\
						.type = _type,		\
						.data = {		\
							._member = _init, \
						},			\
					},				\
					.mut = _mut,			\
				}					\
			}						\
		}							\
	}

#define DEFINE_WASM_I32_GLOBAL(_module, _name, _init, _mut)          \
	DEFINE_WASM_GLOBAL(_module, _name, _init, VALTYPE_I32, i32, _mut)

#define DEFINE_WASM_F64_GLOBAL(_module, _name, _init, _mut)          \
	DEFINE_WASM_GLOBAL(_module, _name, _init, VALTYPE_F64, f64, _mut)

#define NUMARGS(...)  (sizeof((wasmjit_valtype_t[]){__VA_ARGS__})/sizeof(wasmjit_valtype_t))

#define DEFINE_WASM_FUNCTION(_module, _name, _fptr, _output, ...)	\
	struct WasmInst _module ## _ ## _name = {			\
		.type = IMPORT_DESC_TYPE_FUNC,				\
		.u = {							\
			.func = {					\
				.compiled_code = _fptr,			\
				.type = {				\
					.n_inputs = NUMARGS(__VA_ARGS__), \
					.input_types = { __VA_ARGS__ },	\
					.output_type = _output,		\
				}					\
			}						\
		}							\
	}

void wasmjit_init_static_module(struct StaticModuleInst *smi);

#endif
