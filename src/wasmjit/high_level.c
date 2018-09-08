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

#include <wasmjit/high_level.h>

#include <wasmjit/kwasmjit.h>
#include <wasmjit/parse.h>
#include <wasmjit/instantiate.h>
#include <wasmjit/dynamic_emscripten_runtime.h>
#include <wasmjit/emscripten_runtime.h>
#include <wasmjit/sys.h>
#include <wasmjit/util.h>

#if defined(__linux__) && !defined(__KERNEL__)
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

static int add_named_module(struct WasmJITHigh *self,
			    const char *module_name,
			    struct ModuleInst *module)
{
	int ret;
	char *new_module_name = NULL;
	struct NamedModule *new_named_modules = NULL;
	self->n_modules += 1;

	new_module_name = strdup(module_name);
	if (!new_module_name)
		goto error;

	new_named_modules = realloc(self->modules,
				    self->n_modules * sizeof(self->modules[0]));
	if (!new_named_modules) {
		goto error;
	}

	new_named_modules[self->n_modules - 1].name = new_module_name;
	new_named_modules[self->n_modules - 1].module = module;
	self->modules = new_named_modules;
	new_module_name = NULL;
	new_named_modules = NULL;

	if (0) {
 error:
		ret = 0;
	}
	else {
		ret = 1;
	}

	if (new_named_modules)
		free(new_named_modules);

	if (new_module_name)
		free(new_module_name);

	return ret;
}

int wasmjit_high_init(struct WasmJITHigh *self)
{
#if defined(__linux__) && !defined(__KERNEL__)
	{
		int fd;
		fd = open("/dev/wasm", O_RDWR | O_CLOEXEC);
		if (fd >= 0) {
			self->fd = fd;
			return 1;
		}
	}
	self->fd = -1;
#endif

	self->n_modules = 0;
	self->modules = NULL;
	return 1;
}

int wasmjit_high_instantiate_buf(struct WasmJITHigh *self,
				 const char *buf, size_t size,
				 const char *module_name, int flags)
{
	int ret;
	struct ParseState pstate;
	struct Module module;
	struct ModuleInst *module_inst = NULL;

#if defined(__linux__) && !defined(__KERNEL__)
	if (self->fd >= 0) {
		/* not implemented */
		goto error;
	}
#endif

	(void)flags;

	wasmjit_init_module(&module);

	if (!init_pstate(&pstate, buf, size)) {
		goto error;
	}

	if (!read_module(&pstate, &module, NULL, 0)) {
		goto error;
	}

	/* TODO: validate module */

	module_inst = wasmjit_instantiate(&module, self->n_modules, self->modules,
					  NULL, 0);
	if (!module_inst) {
		goto error;
	}

	if (!add_named_module(self, module_name, module_inst)) {
		goto error;
	}
	module_inst = NULL;

	if (0) {
 error:
		ret = 0;
	} else {
		ret = 1;
	}

	wasmjit_free_module(&module);

	if (module_inst) {
		wasmjit_free_module_inst(module_inst);
	}

	return ret;
}

#ifndef __KERNEL__

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

int wasmjit_high_instantiate(struct WasmJITHigh *self, const char *filename, const char *module_name, int flags)
{
	int ret;
	size_t size;
	char *buf = NULL;
#if defined(__linux__) && !defined(__KERNEL__)
	int fd = -1;
#endif

#if defined(__linux__) && !defined(__KERNEL__)
	if (self->fd >= 0) {
		struct kwasmjit_instantiate_args arg;

		fd = open(filename, O_RDONLY);
		if (fd < 0)
			goto error;

		arg.version = 0;
		arg.fd = fd;
		arg.module_name = module_name;
		arg.flags = flags;

		if (ioctl(self->fd, KWASMJIT_INSTANTIATE, &arg) < 0)
			goto error;

		goto success;
	}
#endif

	buf = wasmjit_load_file(filename, &size);
	if (!buf)
		goto error;

	ret = wasmjit_high_instantiate_buf(self, buf, size, module_name, flags);

 success:
	if (0) {
	error:
		ret = 0;
	}

	if (buf)
		free(buf);

#if defined(__linux__) && !defined(__KERNEL__)
	if (fd >= 0) {
		close(fd);
	}
#endif

	return ret;
}

#endif

int wasmjit_high_instantiate_emscripten_runtime(struct WasmJITHigh *self,
						size_t tablemin,
						size_t tablemax,
						int flags)
{
	int ret;
	size_t n_modules, i;
	struct NamedModule *modules = NULL;

#if defined(__linux__) && !defined(__KERNEL__)
	if (self->fd >= 0) {
		struct kwasmjit_instantiate_emscripten_runtime_args arg;

		arg.version = 0;
		arg.tablemin = tablemin;
		arg.tablemax = tablemax;
		arg.flags = flags;

		if (ioctl(self->fd, KWASMJIT_INSTANTIATE_EMSCRIPTEN_RUNTIME, &arg) < 0)
			goto error;

		goto success;
	}
#endif

	modules = wasmjit_instantiate_emscripten_runtime(tablemin,
							 tablemax,
							 &n_modules);
	if (!modules) {
		goto error;
	}

	for (i = 0; i < n_modules; ++i)  {
		if (!add_named_module(self, modules[i].name, modules[i].module)) {
			goto error;
		}

		modules[i].module = NULL;
	}

#if defined(__linux__) && !defined(__KERNEL__)
 success:
#endif
	ret = 1;

	if (0) {
	error:
		ret = 0;
	}

	if (modules) {
		for (i = 0; i < n_modules; ++i) {
			free(modules[i].name);
			if (modules[i].module)
				wasmjit_free_module_inst(modules[i].module);
		}
		free(modules);
	}

	return ret;
}

int wasmjit_high_emscripten_invoke_main(struct WasmJITHigh *self,
					const char *module_name,
					int argc, char **argv, int flags)
{
	size_t i;
	struct ModuleInst *env_module_inst;
	struct FuncInst *main_inst, *stack_alloc_inst;
	struct MemInst *meminst;
	struct ModuleInst *module_inst;
	int ret;
	jmp_buf jmpbuf, *preserve;

#if defined(__linux__) && !defined(__KERNEL__)
	if (self->fd >= 0) {
		struct kwasmjit_emscripten_invoke_main_args arg;

		arg.version = 0;
		arg.module_name = module_name;
		arg.argc = argc;
		arg.argv = argv;
		arg.flags = flags;

		return ioctl(self->fd, KWASMJIT_EMSCRIPTEN_INVOKE_MAIN, &arg);
	}
#endif

	module_inst = NULL;
	for (i = 0; i < self->n_modules; ++i) {
		if (!strcmp(self->modules[i].name, module_name)) {
			module_inst = self->modules[i].module;
		}
	}

	if (!module_inst)
		return -1;

	env_module_inst = NULL;
	for (i = 0; i < self->n_modules; ++i) {
		if (!strcmp(self->modules[i].name, "env")) {
			env_module_inst = self->modules[i].module;
			break;
		}
	}

	if (!env_module_inst)
		return -1;

	main_inst = wasmjit_get_export(module_inst, "_main",
				       IMPORT_DESC_TYPE_FUNC).func;
	if (!main_inst)
		return -1;

	stack_alloc_inst = wasmjit_get_export(module_inst, "stackAlloc",
					      IMPORT_DESC_TYPE_FUNC).func;
	if (!stack_alloc_inst)
		return -1;

	meminst = wasmjit_get_export(env_module_inst, "memory",
				     IMPORT_DESC_TYPE_MEM).mem;
	if (!meminst)
		return -1;

	preserve = wasmjit_get_jmp_buf();

	if ((ret = setjmp(jmpbuf))) {
		/* if the code trapped, return error */
		ret = (WASMJIT_TRAP_OFFSET + (ret - 1));
	} else {
		wasmjit_set_jmp_buf(&jmpbuf);
		ret = wasmjit_emscripten_invoke_main(meminst,
						     stack_alloc_inst,
						     main_inst,
						     argc, argv);
	}

	wasmjit_set_jmp_buf(preserve);

	wasmjit_emscripten_cleanup(env_module_inst);

	return ret;
}

void wasmjit_high_close(struct WasmJITHigh *self)
{
	size_t i;

#if defined(__linux__) && !defined(__KERNEL__)
	if (self->fd >= 0) {
		(void)close(self->fd);
		return;
	}
#endif

	for (i = 0; i < self->n_modules; ++i) {
		free(self->modules[i].name);
		wasmjit_free_module_inst(self->modules[i].module);
	}
	if (self->modules)
		free(self->modules);

}
