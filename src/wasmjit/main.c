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
#include <wasmjit/compile.h>
#include <wasmjit/ast_dump.h>
#include <wasmjit/parse.h>
#include <wasmjit/runtime.h>
#include <wasmjit/instantiate.h>
#include <wasmjit/emscripten_runtime.h>
#include <wasmjit/dynamic_emscripten_runtime.h>
#include <wasmjit/elf_relocatable.h>
#include <wasmjit/util.h>
#include <wasmjit/high_level.h>

#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>
#include <regex.h>
#include <unistd.h>

#include <sys/types.h>

#ifdef __linux__

#include <valgrind/valgrind.h>

#include <sys/time.h>
#include <sys/resource.h>

static void *get_stack_top(void)
{
	struct rlimit rlim;
	uintptr_t stack_bottom;
	FILE *stream;
	void *stack_top;
	char *pathname = NULL;

	if (RUNNING_ON_VALGRIND)
		return NULL;

	stream = fopen("/proc/self/maps", "r");
	if (!stream)
		goto error;

	while (1) {
		uintptr_t start_address, end_address;
		char perms[4];
		uint64_t offset;
		uint8_t dev_major, dev_minor;
		uint64_t inode;
		int ret;

		free(pathname);

		ret = fscanf(stream,
			     "%" SCNxPTR "-%" SCNxPTR "%*[ ]%4c%*[ ]%" SCNx64
			     "%*[ ]%" SCNx8 ":%" SCNx8 "%*[ ]%" SCNu64
			     "%*[ ]%m[^\n]%*[\n]",
			     &start_address, &end_address, perms, &offset,
			     &dev_major, &dev_minor, &inode, &pathname);
		if (ret == 7) {
			/* read newline if there was no pathname */
			int c;
			c = fgetc(stream);
			if (c != '\n')
				goto error;
			pathname = NULL;
		} else if (ret != 8) {
			goto error;
		}

		if (pathname && !strcmp(pathname, "[stack]")) {
			stack_bottom = end_address;
			break;
		}
	}

	if (getrlimit(RLIMIT_STACK, &rlim)) {
		goto error;
	}

	stack_top = (void *)(stack_bottom - rlim.rlim_cur);

	if (0) {
 error:
		stack_top = NULL;
	}

	free(pathname);

	if (stream)
		fclose(stream);

	return stack_top;
}

#else

static void *get_stack_top(void)
{
	return NULL;
}

#endif

static int parse_module(const char *filename, struct Module *module)
{
	char *buf = NULL;
	int ret, result;
	size_t size;
	struct ParseState pstate;

	buf = wasmjit_load_file(filename, &size);
	if (!buf)
		goto error;

	ret = init_pstate(&pstate, buf, size);
	if (!ret)
		goto error;

	ret = read_module(&pstate, module, NULL, 0);
	if (!ret)
		goto error;

	result = 0;

	if (0) {
	error:
		result = -1;
	}

	if (buf)
		wasmjit_unload_file(buf, size);
	return result;
}

static int dump_wasm_module(const char *filename)
{
	uint32_t i;
	struct Module module;
	int res = -1;

	wasmjit_init_module(&module);

	if (parse_module(filename, &module))
		goto error;

	/* the most basic validation */
	if (module.code_section.n_codes != module.function_section.n_typeidxs) {
		fprintf(stderr,
			"# Functions != # Codes %" PRIu32 " != %" PRIu32 "\n",
			module.function_section.n_typeidxs,
			module.code_section.n_codes);
		goto error;
	}

	for (i = 0; i < module.code_section.n_codes; ++i) {
		uint32_t j;
		struct TypeSectionType *type;
		struct CodeSectionCode *code =
			&module.code_section.codes[i];

		type =
			&module.type_section.types[module.function_section.
						   typeidxs[i]];

		printf("Code #%" PRIu32 "\n", i);

		printf("Locals (%" PRIu32 "):\n", code->n_locals);
		for (j = 0; j < code->n_locals; ++j) {
			printf("  %s (%" PRIu32 ")\n",
			       wasmjit_valtype_repr(code->locals[j].
						    valtype),
			       code->locals[j].count);
		}

		printf("Signature: [");
		for (j = 0; j < type->n_inputs; ++j) {
			printf("%s,",
			       wasmjit_valtype_repr(type->
						    input_types[j]));
		}
		printf("] -> [");
		for (j = 0; j < FUNC_TYPE_N_OUTPUTS(type); ++j) {
			printf("%s,",
			       wasmjit_valtype_repr(FUNC_TYPE_OUTPUT_IDX(type, j)));
		}
		printf("]\n");

		printf("Instructions:\n");
		dump_instructions(module.code_section.codes[i].
				  instructions,
				  module.code_section.codes[i].
				  n_instructions, 1);
		printf("\n");
	}

	res = 0;

 error:
	wasmjit_free_module(&module);

	return res;
}

static int get_static_bump(const char *filename, uint32_t *static_bump)
{
	char *js_path = NULL, *filebuf = NULL, *filebuf2 = NULL;
	size_t fnlen, filesize;
	regex_t re;
	regmatch_t pmatch[4];
	int ret, compiled = 0;
	long result;

	fnlen = strlen(filename);
	if (fnlen < 4)
		goto error;

	js_path = malloc(fnlen + 1);
	memcpy(js_path, filename, fnlen - 4);
	strcpy(&js_path[fnlen - 4], "js");

	filebuf = wasmjit_load_file(js_path, &filesize);
	if (!filebuf)
		goto error;

	/* we need to null terminal the file... */
	filebuf2 = malloc(filesize + 1);
	if (!filebuf2)
		goto error;

	memcpy(filebuf2, filebuf, filesize);
	filebuf2[filesize] = '\0';

	ret = regcomp(&re, "(^|;|\n) *var +STATIC_BUMP *= *([0-9]+) *(;|$|\n)", REG_EXTENDED);
	if (ret)
		goto error;

	compiled = 1;

	ret = regexec(&re, filebuf2, 4, pmatch, 0);
	if (ret)
		goto error;

	result = strtol(filebuf2 + pmatch[2].rm_so, NULL, 10);
	if ((result == LONG_MIN || result == LONG_MAX) && errno == ERANGE)
		goto error;

	if (result < 0 || result > UINT32_MAX)
		goto error;

	*static_bump = result;

	ret = 0;

	if (0) {
 error:
		ret = -1;
	}

	if (compiled)
		regfree(&re);
	free(filebuf2);
	if (filebuf)
		wasmjit_unload_file(filebuf, filesize);
	free(js_path);

	return ret;
}

static int get_emscripten_runtime_parameters(const char *filename,
					     uint32_t *static_bump,
					     size_t *tablemin, size_t *tablemax)
{
	size_t i;
	int ret;
	struct Module module;

	wasmjit_init_module(&module);

	if (parse_module(filename, &module))
		goto error;

	/* find correct tablemin and tablemax */
	for (i = 0; i < module.import_section.n_imports; ++i) {
		struct ImportSectionImport *import;
		import = &module.import_section.imports[i];
		if (strcmp(import->module, "env") ||
		    strcmp(import->name, "table") ||
		    import->desc_type != IMPORT_DESC_TYPE_TABLE)
			continue;

		*tablemin = import->desc.tabletype.limits.min;
		*tablemax = import->desc.tabletype.limits.max;
		break;
	}

	if (i == module.import_section.n_imports)
		goto error;

	ret = get_static_bump(filename, static_bump);

	if (0) {
	error:
		ret = -1;
	}

	wasmjit_free_module(&module);

	return ret;
}

static int run_emscripten_file(const char *filename,
			       uint32_t static_bump,
			       size_t tablemin, size_t tablemax,
			       int argc, char **argv)
{
	struct WasmJITHigh high;
	int ret;
	void *stack_top;
	int high_init = 0;
	const char *msg;

	if (wasmjit_high_init(&high)) {
		msg = "failed to initialize";
		goto error;
	}
	high_init = 1;

	if (wasmjit_high_instantiate_emscripten_runtime(&high,
							static_bump,
							tablemin, tablemax, 0)) {
		msg = "failed to instantiate emscripten runtime";
		goto error;
	}

	if (wasmjit_high_instantiate(&high, filename, "asm", 0)) {
		msg = "failed to instantiate module";
		goto error;
	}

	stack_top = get_stack_top();
	if (!stack_top) {
		fprintf(stderr, "warning: running without a stack limit\n");
	}

	wasmjit_set_stack_top(stack_top);

	ret = wasmjit_high_emscripten_invoke_main(&high, "asm",
						  argc, argv, 0);

	if (WASMJIT_IS_TRAP_ERROR(ret)) {
		fprintf(stderr, "TRAP: %s\n",
			wasmjit_trap_reason_to_string(WASMJIT_DECODE_TRAP_ERROR(ret)));
	} else if (ret < 0) {
		msg = "failed to invoke main";
		goto error;
	}

	if (0) {
		char error_buffer[256];

	error:
		ret = wasmjit_high_error_message(&high,
						 error_buffer,
						 sizeof(error_buffer));
		if (!ret) {
			fprintf(stderr, "%s: %s\n",
				msg, error_buffer);
			ret = -1;
		}
	}

	if (high_init)
		wasmjit_high_close(&high);

	return ret;
}

int main(int argc, char *argv[])
{
	int ret;
	char *filename;
	int dump_module, create_relocatable, create_relocatable_helper, opt;
	size_t tablemin = 0, tablemax = 0;
	uint32_t static_bump = 0;

	dump_module =  0;
	create_relocatable =  0;
	create_relocatable_helper =  0;
	while ((opt = getopt(argc, argv, "dop")) != -1) {
		switch (opt) {
		case 'o':
			create_relocatable = 1;
			break;
		case 'p':
			create_relocatable_helper = 1;
			break;
		case 'd':
			dump_module = 1;
			break;
		default:
			return -1;
		}
	}

	if (optind >= argc) {
		fprintf(stderr, "Need an input file\n");
		return -1;
	}

	filename = argv[optind];

	if (dump_module)
		return dump_wasm_module(filename);

	if (create_relocatable) {
		struct Module module;

		wasmjit_init_module(&module);

		if (!parse_module(filename, &module)) {
			void *a_out;
			size_t size;
			a_out = wasmjit_output_elf_relocatable("asm", &module, &size);
			ret = write(1, a_out, size);
			free(a_out);
			ret = ret >= 0 ? 0 : -1;
		} else {
			ret = -1;
		}

		wasmjit_free_module(&module);

		return ret;
	}

	ret = get_emscripten_runtime_parameters(filename, &static_bump, &tablemin, &tablemax);
	if (ret)
		return -1;

	if (create_relocatable_helper) {
		struct WasmJITEmscriptenMemoryGlobals globals;

		wasmjit_emscripten_derive_memory_globals(static_bump, &globals);

		printf("#include <wasmjit/static_runtime.h>\n");
		printf("#define CURRENT_MODULE env\n");
		printf("DEFINE_WASM_TABLE(table, ELEMTYPE_ANYFUNC, %zu, %zu)\n",
		       tablemin, tablemax);
		printf("DEFINE_WASM_GLOBAL(memoryBase, %" PRIu32 ", VALTYPE_I32, i32, 0)\n",
		       globals.memoryBase);
		printf("DEFINE_WASM_GLOBAL(tempDoublePtr, %" PRIu32 ", VALTYPE_I32, i32, 0)\n",
		       globals.tempDoublePtr);
		printf("DEFINE_WASM_GLOBAL(DYNAMICTOP_PTR, %" PRIu32 ", VALTYPE_I32, i32, 0)\n",
		       globals.DYNAMICTOP_PTR);
		printf("DEFINE_WASM_GLOBAL(STACKTOP, %" PRIu32 ", VALTYPE_I32, i32, 0)\n",
		       globals.STACKTOP);
		printf("DEFINE_WASM_GLOBAL(STACK_MAX, %" PRIu32 ", VALTYPE_I32, i32, 0)\n",
		       globals.STACK_MAX);

		return 0;
	}

	return run_emscripten_file(filename,
				   static_bump, tablemin, tablemax,
				   argc - optind, &argv[optind]);
}
