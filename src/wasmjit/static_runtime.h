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
	unsigned initted;
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
	void (*start_func)();
};

#endif
