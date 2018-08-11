#include <wasmjit/runtime.h>

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

wasmjit_tls_key_t baseaddr_key;

__attribute__((constructor))
static void init_baseaddr_key(void)
{
	if (!wasmjit_init_tls_key(&baseaddr_key, NULL))
		abort();
}

static void *wasmjit_get_base_address(void)
{
	void *addr;
	if (!wasmjit_get_tls_key(baseaddr_key, &addr))
		return NULL;
	return addr;
}

static int _wasmjit_set_base_address(void *addr)
{
	return wasmjit_set_tls_key(baseaddr_key, addr);
}

int wasmjit_add_emscripten_runtime(struct Store *store)
{
	wasmjit_addr_t memaddr;
	uint32_t TOTAL_STACK = 5242880;
	uint32_t STACK_ALIGN = 16;
	uint32_t GLOBAL_BASE = 1024;
	uint32_t STATIC_BASE = GLOBAL_BASE;
	uint32_t STATICTOP = STATIC_BASE + 5472, tmp;
#define staticAlloc(s) \
	(tmp = STATICTOP, \
	 STATICTOP = ((STATICTOP + (s) + 15) & ((uint32_t) -16)), \
	 tmp)
	uint32_t tempDoublePtr = STATICTOP; STATICTOP += 16;
	uint32_t DYNAMICTOP_PTR = staticAlloc(4);
	uint32_t STACKTOP = alignMemory(STATICTOP, STACK_ALIGN);
	uint32_t STACK_BASE = STACKTOP;
	uint32_t STACK_MAX = STACK_BASE + TOTAL_STACK;

	assert(tempDoublePtr % 8 == 0);

	memaddr = wasmjit_import_memory(store, "env", "memory",
					256 * WASM_PAGE_SIZE, 256 * WASM_PAGE_SIZE);
	if (memaddr == INVALID_ADDR)
		goto error;
	_wasmjit_set_base_address(store->mems.elts[memaddr].data);

	if (!wasmjit_import_table(store, "env", "table",
				  ELEMTYPE_ANYFUNC,
				  10, 10))
		goto error;

#define ADD_I32_GLOBAL(ns, s, v, m)				\
	do {							\
		struct Value value;				\
		value.type = VALTYPE_I32;			\
		value.data.i32 = v;				\
		if (!wasmjit_import_global(store, ns, s,	\
					   value, m))		\
			goto error;				\
	}							\
	while (0)

#define ADD_F64_GLOBAL(ns, s, v, m)				\
	do {							\
		struct Value value;				\
		value.type = VALTYPE_F64;			\
		value.data.f64 = v;				\
		if (!wasmjit_import_global(store, ns, s,	\
					   value, m))		\
			goto error;				\
	}							\
	while (0)

	ADD_I32_GLOBAL("env", "memoryBase", STATIC_BASE, 0);
	ADD_I32_GLOBAL("env", "tableBase", 0, 0);
	ADD_I32_GLOBAL("env", "DYNAMICTOP_PTR", DYNAMICTOP_PTR, 0);
	ADD_I32_GLOBAL("env", "tempDoublePtr", tempDoublePtr, 0);
	ADD_I32_GLOBAL("env", "ABORT", 0, 0);
	ADD_I32_GLOBAL("env", "STACKTOP", STACKTOP, 0);
	ADD_I32_GLOBAL("env", "STACK_MAX", STACK_MAX, 0);
	ADD_F64_GLOBAL("global", "NaN", NAN, 0);
	ADD_F64_GLOBAL("global", "Infinity", INFINITY, 0);

#undef ADD_I32_GLOBAL

#define ADD_FUNCTION(m, fname, its, ots)				\
	do {								\
		unsigned input_types[] = its;				\
		unsigned output_types[] = ots;				\
		if (!wasmjit_import_function(store,			\
					     m, #fname,			\
					     &fname,			\
					     sizeof(input_types) / sizeof(input_types[0]), \
					     input_types,		\
					     sizeof(output_types) / sizeof(output_types[0]), \
					     output_types))		\
			goto error;					\
	}								\
	while (0)

#define SEP ,

	ADD_FUNCTION("env", enlargeMemory, {}, {VALTYPE_I32});
	ADD_FUNCTION("env", getTotalMemory, {}, {VALTYPE_I32});
	ADD_FUNCTION("env", abortOnCannotGrowMemory, {}, {VALTYPE_I32});
	ADD_FUNCTION("env", abortStackOverflow, {VALTYPE_I32}, {});
	ADD_FUNCTION("env", nullFunc_ii, {VALTYPE_I32}, {});
	ADD_FUNCTION("env", nullFunc_iiii, {VALTYPE_I32}, {});
	ADD_FUNCTION("env", ___lock, {VALTYPE_I32}, {});
	ADD_FUNCTION("env", ___setErrNo, {VALTYPE_I32}, {});
	ADD_FUNCTION("env", ___syscall140, {VALTYPE_I32 SEP VALTYPE_I32}, {VALTYPE_I32});
	ADD_FUNCTION("env", ___syscall146, {VALTYPE_I32 SEP VALTYPE_I32}, {VALTYPE_I32});
	ADD_FUNCTION("env", ___syscall54, {VALTYPE_I32 SEP VALTYPE_I32}, {VALTYPE_I32});
	ADD_FUNCTION("env", ___syscall6, {VALTYPE_I32 SEP VALTYPE_I32}, {VALTYPE_I32});
	ADD_FUNCTION("env", ___unlock, {VALTYPE_I32}, {});
	ADD_FUNCTION("env", _emscripten_memcpy_big, {VALTYPE_I32 SEP VALTYPE_I32 SEP VALTYPE_I32}, {VALTYPE_I32});

#undef SEP
#undef ADD_FUNCTION

	return 1;
 error:
	/* TODO: handle cleanup */
	assert(0);
	return 0;
}
