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

#include <wasmjit/ast.h>
#include <wasmjit/static_runtime.h>
#include <wasmjit/util.h>

#include <math.h>

#include <wasmjit/emscripten_runtime.h>

#define START_MODULE()				\
	struct StaticModuleInst WASM_MODULE_SYMBOL(CURRENT_MODULE);

#define DEFINE_WASM_START_FUNCTION(fptr)	\
	DEFINE_WASM_FUNCTION_ADVANCED(static, __start_func__, wasmjit_emscripten_start_func, VALTYPE_NULL, 0)

#define END_MODULE()
#define START_TABLE_DEFS()
#define END_TABLE_DEFS()
#define START_MEMORY_DEFS()
#define END_MEMORY_DEFS()
#define START_GLOBAL_DEFS()
#define END_GLOBAL_DEFS()
#define START_FUNCTION_DEFS()
#define END_FUNCTION_DEFS()
#define DEFINE_EXTERNAL_WASM_TABLE(name)	\
	extern struct TableInst WASM_TABLE_SYMBOL(CURRENT_MODULE, name);
#define DEFINE_EXTERNAL_WASM_GLOBAL(name) \
	extern struct GlobalInst WASM_GLOBAL_SYMBOL(CURRENT_MODULE, name);

#include <wasmjit/emscripten_runtime_def.h>

#undef START_MODULE
#undef END_MODULE
#undef DEFINE_WASM_GLOBAL
#undef DEFINE_WASM_FUNCTION
#undef DEFINE_WASM_TABLE
#undef DEFINE_WASM_MEMORY
#undef START_TABLE_DEFS
#undef END_TABLE_DEFS
#undef START_MEMORY_DEFS
#undef END_MEMORY_DEFS
#undef START_GLOBAL_DEFS
#undef END_GLOBAL_DEFS
#undef START_FUNCTION_DEFS
#undef END_FUNCTION_DEFS
#undef DEFINE_WASM_START_FUNCTION
#undef DEFINE_EXTERNAL_WASM_TABLE
#undef DEFINE_EXTERNAL_WASM_GLOBAL

#define DEFINE_WASM_START_FUNCTION(...)
#define START_MODULE()

#define START_TABLE_DEFS(n)						\
	static struct TableInst *CAT(CURRENT_MODULE, _tables)[] = {
#define DEFINE_WASM_TABLE(_name, ...)			\
	&WASM_TABLE_SYMBOL(CURRENT_MODULE, _name),
#define DEFINE_EXTERNAL_WASM_TABLE(_name)			\
	&WASM_TABLE_SYMBOL(CURRENT_MODULE, _name),
#define END_TABLE_DEFS()			\
	};

#define START_MEMORY_DEFS(n)					\
	static struct MemInst *CAT(CURRENT_MODULE, _mems)[] = {
#define DEFINE_WASM_MEMORY(_name, ...)			\
	&WASM_MEMORY_SYMBOL(CURRENT_MODULE, _name),
#define END_MEMORY_DEFS()			\
	};

#define START_GLOBAL_DEFS(n)						\
	static struct GlobalInst *CAT(CURRENT_MODULE,  _globals)[] = {
#define DEFINE_WASM_GLOBAL(_name, ...)			\
	&WASM_GLOBAL_SYMBOL(CURRENT_MODULE, _name),
#define DEFINE_EXTERNAL_WASM_GLOBAL(_name)			\
	&WASM_GLOBAL_SYMBOL(CURRENT_MODULE, _name),
#define END_GLOBAL_DEFS()			\
	};

#define START_FUNCTION_DEFS(n)					\
	static struct FuncInst *CAT(CURRENT_MODULE,  _funcs)[] = {
#define DEFINE_WASM_FUNCTION(_name, ...)		\
	&WASM_FUNC_SYMBOL(CURRENT_MODULE, _name),
#define END_FUNCTION_DEFS()			\
		};

struct EmscriptenContext g_emscripten_ctx;

#define END_MODULE()

#include <wasmjit/emscripten_runtime_def.h>

#undef START_MODULE
#undef END_MODULE
#undef DEFINE_WASM_GLOBAL
#undef DEFINE_WASM_FUNCTION
#undef DEFINE_WASM_TABLE
#undef DEFINE_WASM_MEMORY
#undef START_TABLE_DEFS
#undef END_TABLE_DEFS
#undef START_MEMORY_DEFS
#undef END_MEMORY_DEFS
#undef START_GLOBAL_DEFS
#undef END_GLOBAL_DEFS
#undef START_FUNCTION_DEFS
#undef END_FUNCTION_DEFS
#undef DEFINE_WASM_START_FUNCTION
#undef DEFINE_EXTERNAL_WASM_TABLE
#undef DEFINE_EXTERNAL_WASM_GLOBAL

/* create exports */

#define DEFINE_WASM_START_FUNCTION(...)
#define START_FUNCTION_DEFS(...)
#define END_FUNCTION_DEFS(...)
#define START_TABLE_DEFS(...)
#define END_TABLE_DEFS(...)
#define START_MEMORY_DEFS(...)
#define END_MEMORY_DEFS(...)
#define START_GLOBAL_DEFS(...)
#define END_GLOBAL_DEFS(...)

#define START_MODULE()						\
	static struct Export CAT(CURRENT_MODULE, _exports)[] = {
#define DEFINE_WASM_FUNCTION(_name, ...)				\
	{							\
		.name = #_name,					\
		.type = IMPORT_DESC_TYPE_FUNC,	\
		.value = {				\
			.func = &WASM_FUNC_SYMBOL(CURRENT_MODULE, _name), \
		}							\
	},
#define DEFINE_WASM_TABLE(_name, ...)				\
	{							\
		.name = #_name,					\
		.type = IMPORT_DESC_TYPE_TABLE,	\
		.value = {				\
			.table = &WASM_TABLE_SYMBOL(CURRENT_MODULE, _name), \
		}							\
	},
#define DEFINE_EXTERNAL_WASM_TABLE(_name) DEFINE_WASM_TABLE(_name)
#define DEFINE_WASM_MEMORY(_name, ...)				\
	{							\
		.name = #_name,					\
		.type = IMPORT_DESC_TYPE_MEM,	\
		.value = {				\
			.mem = &WASM_MEMORY_SYMBOL(CURRENT_MODULE, _name), \
		}							\
	},
#define DEFINE_WASM_GLOBAL(_name, ...)				\
	{							\
		.name = #_name,					\
		.type = IMPORT_DESC_TYPE_GLOBAL,	\
		.value = {				\
			.global = &WASM_GLOBAL_SYMBOL(CURRENT_MODULE, _name), \
		}							\
	},
#define DEFINE_EXTERNAL_WASM_GLOBAL(_name) DEFINE_WASM_GLOBAL(_name)
#define END_MODULE()				\
	};

#include <wasmjit/emscripten_runtime_def.h>

#undef START_MODULE
#undef END_MODULE
#undef DEFINE_WASM_GLOBAL
#undef DEFINE_WASM_FUNCTION
#undef DEFINE_WASM_TABLE
#undef DEFINE_WASM_MEMORY
#undef START_TABLE_DEFS
#undef END_TABLE_DEFS
#undef START_MEMORY_DEFS
#undef END_MEMORY_DEFS
#undef START_GLOBAL_DEFS
#undef END_GLOBAL_DEFS
#undef START_FUNCTION_DEFS
#undef END_FUNCTION_DEFS
#undef DEFINE_WASM_START_FUNCTION
#undef DEFINE_EXTERNAL_WASM_TABLE
#undef DEFINE_EXTERNAL_WASM_GLOBAL

/* create module */

#define START_FUNCTION_DEFS()
#define END_FUNCTION_DEFS()
#define START_TABLE_DEFS()
#define END_TABLE_DEFS()
#define START_MEMORY_DEFS()
#define END_MEMORY_DEFS()
#define START_GLOBAL_DEFS()
#define END_GLOBAL_DEFS()
#define DEFINE_WASM_GLOBAL(...)
#define DEFINE_WASM_FUNCTION(...)
#define DEFINE_WASM_TABLE(...)
#define DEFINE_WASM_MEMORY(...)
#define DEFINE_EXTERNAL_WASM_GLOBAL(...)
#define DEFINE_EXTERNAL_WASM_TABLE(...)

#define START_MODULE()						\
	struct StaticModuleInst WASM_MODULE_SYMBOL(CURRENT_MODULE) = {	\
		.module = {						\
			.funcs = {					\
				.n_elts = ARRAY_LEN(CAT(CURRENT_MODULE, _funcs)), \
				.elts = CAT(CURRENT_MODULE, _funcs),	\
			},						\
			.tables = {					\
				.n_elts = ARRAY_LEN(CAT(CURRENT_MODULE, _tables)), \
				.elts = CAT(CURRENT_MODULE, _tables),	\
			},						\
			.mems = {					\
				.n_elts = ARRAY_LEN(CAT(CURRENT_MODULE, _mems)), \
				.elts = CAT(CURRENT_MODULE, _mems),	\
			},						\
			.globals = {					\
				.n_elts = ARRAY_LEN(CAT(CURRENT_MODULE, _globals)), \
				.elts = CAT(CURRENT_MODULE, _globals),	\
			},						\
			.exports = {					\
				.n_elts = ARRAY_LEN(CAT(CURRENT_MODULE, _exports)),\
				.elts = CAT(CURRENT_MODULE, _exports),	\
			},						\
			.private_data = &g_emscripten_ctx,		\
		},

#define DEFINE_WASM_START_FUNCTION(...)					\
	.start_func = &WASM_FUNC_SYMBOL(CURRENT_MODULE, __start_func__), \

#define END_MODULE() };

#include <wasmjit/emscripten_runtime_def.h>

extern struct FuncInst WASM_FUNC_SYMBOL(asm, _main);
extern struct FuncInst WASM_FUNC_SYMBOL(asm, stackAlloc);
extern struct FuncInst WASM_FUNC_SYMBOL(asm, ___errno_location) __attribute__((weak));
extern struct FuncInst WASM_FUNC_SYMBOL(asm, ___emscripten_environ_constructor) __attribute__((weak));
extern struct FuncInst WASM_FUNC_SYMBOL(asm, _malloc);
extern struct FuncInst WASM_FUNC_SYMBOL(asm, _free);

__attribute__((constructor))
static void init_module(void)
{
	wasmjit_init_static_module(&WASM_MODULE_SYMBOL(env));
}

extern char **environ;
int main(int argc, char *argv[]) {
	int ret;
	ret = wasmjit_emscripten_init(&g_emscripten_ctx,
				      &WASM_FUNC_SYMBOL(asm, ___errno_location),
				      &WASM_FUNC_SYMBOL(asm, ___emscripten_environ_constructor),
				      &WASM_FUNC_SYMBOL(asm, _malloc),
				      &WASM_FUNC_SYMBOL(asm, _free),
				      environ);
	if (ret)
		return -1;
	return wasmjit_emscripten_invoke_main(&WASM_MEMORY_SYMBOL(env, memory),
					      &WASM_FUNC_SYMBOL(asm, stackAlloc),
					      &WASM_FUNC_SYMBOL(asm, _main),
					      argc, argv);
}
