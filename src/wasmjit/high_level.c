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

#include <wasmjit/parse.h>
#include <wasmjit/instantiate.h>
#include <wasmjit/dynamic_emscripten_runtime.h>
#include <wasmjit/emscripten_runtime.h>
#include <wasmjit/sys.h>
#include <wasmjit/util.h>

#ifdef WASMJIT_CAN_USE_DEVICE
#include <wasmjit/kwasmjit.h>

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
#ifdef WASMJIT_CAN_USE_DEVICE
	{
		int fd;
		fd = open("/dev/wasm", O_RDWR | O_CLOEXEC);
		if (fd >= 0) {
			self->fd = fd;
			return 0;
		}
	}
	self->fd = -1;
#endif

	self->n_modules = 0;
	self->modules = NULL;
	self->emscripten_asm_module = NULL;
	self->emscripten_env_module = NULL;
	memset(self->error_buffer, 0, sizeof(self->error_buffer));
	return 0;
}

static int wasmjit_high_instantiate_buf(struct WasmJITHigh *self,
					const char *buf, size_t size,
					const char *module_name, uint32_t flags)
{
	int ret;
	struct ParseState pstate;
	struct Module module;
	struct ModuleInst *module_inst = NULL;

#ifdef WASMJIT_CAN_USE_DEVICE
	/* should not be using this if we are backending to kernel */
	assert(self->fd < 0);
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
					  self->error_buffer, sizeof(self->error_buffer));
	if (!module_inst) {
		goto error;
	}

	if (!add_named_module(self, module_name, module_inst)) {
		goto error;
	}
	module_inst = NULL;

	if (0) {
 error:
		ret = -1;
	} else {
		ret = 0;
	}

	wasmjit_free_module(&module);

	if (module_inst) {
		wasmjit_free_module_inst(module_inst);
	}

	return ret;
}

int wasmjit_high_instantiate(struct WasmJITHigh *self, const char *filename, const char *module_name, uint32_t flags)
{
	int ret;
	size_t size;
	char *buf;

#ifdef WASMJIT_CAN_USE_DEVICE
	if (self->fd >= 0) {
		struct kwasmjit_instantiate_args arg;
		arg.version = 0;
		arg.file_name = filename;
		arg.module_name = module_name;
		arg.flags = flags;

		return !ioctl(self->fd, KWASMJIT_INSTANTIATE, &arg);
	}
#endif

	self->error_buffer[0] = '\0';

	/* TODO: do incremental reading */

	buf = wasmjit_load_file(filename, &size);
	if (!buf)
		goto error;

	ret = wasmjit_high_instantiate_buf(self, buf, size, module_name, flags);

	if (0) {
	error:
		ret = -1;
	}

	if (buf)
		wasmjit_unload_file(buf, size);

	return ret;
}

int wasmjit_high_instantiate_emscripten_runtime(struct WasmJITHigh *self,
						uint32_t static_bump,
						size_t tablemin,
						size_t tablemax,
						uint32_t flags)
{
	int ret;
	size_t n_modules, i;
	struct NamedModule *modules = NULL;

#ifdef WASMJIT_CAN_USE_DEVICE
	if (self->fd >= 0) {
		struct kwasmjit_instantiate_emscripten_runtime_args arg;

		arg.version = 0;
		arg.static_bump = static_bump;
		arg.tablemin = tablemin;
		arg.tablemax = tablemax;
		arg.flags = flags;

		if (ioctl(self->fd, KWASMJIT_INSTANTIATE_EMSCRIPTEN_RUNTIME, &arg) < 0)
			goto error;

		goto success;
	}
#endif

	self->error_buffer[0] = '\0';

	modules = wasmjit_instantiate_emscripten_runtime(static_bump,
							 tablemin,
							 tablemax,
							 &n_modules);
	if (!modules) {
		goto error;
	}

	for (i = 0; i < n_modules; ++i)  {
		if (!add_named_module(self, modules[i].name, modules[i].module)) {
			goto error;
		}

		/* TODO: delegate to emscripten_runtime how to find
		   emscripten cleanup module */
		if (!strcmp(modules[i].name, "env")) {
			self->emscripten_env_module = modules[i].module;
		}

		modules[i].module = NULL;
	}

#ifdef WASMJIT_CAN_USE_DEVICE
 success:
#endif
	ret = 0;

	if (0) {
	error:
		ret = -1;
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
					int argc, char **argv, char **envp,
					uint32_t flags)
{
	size_t i;
	struct ModuleInst *env_module_inst;
	struct FuncInst *main_inst,
		*stack_alloc_inst,
		*environ_constructor;
	struct MemInst *meminst;
	struct ModuleInst *module_inst;
	int ret;

#ifdef WASMJIT_CAN_USE_DEVICE
	if (self->fd >= 0) {
		struct kwasmjit_emscripten_invoke_main_args arg;

		arg.version = 0;
		arg.module_name = module_name;
		arg.argc = argc;
		arg.argv = argv;
		arg.envp = envp;
		arg.flags = flags;

		return ioctl(self->fd, KWASMJIT_EMSCRIPTEN_INVOKE_MAIN, &arg);
	}
#endif

	self->error_buffer[0] = '\0';

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

	if (self->emscripten_env_module) {
		assert(self->emscripten_env_module == env_module_inst);
		if (!self->emscripten_asm_module) {
			struct FuncInst
				*errno_location_inst,
				*malloc_inst,
				*free_inst;

			errno_location_inst = wasmjit_get_export(module_inst, "___errno_location",
								 IMPORT_DESC_TYPE_FUNC).func;

			malloc_inst = wasmjit_get_export(module_inst,
							 "_malloc",
							 IMPORT_DESC_TYPE_FUNC).func;
			if (!malloc_inst)
				return -1;


			free_inst = wasmjit_get_export(module_inst,
						       "_free",
						       IMPORT_DESC_TYPE_FUNC).func;
			if (!free_inst)
				return -1;

			if (wasmjit_emscripten_init(wasmjit_emscripten_get_context(env_module_inst),
						    errno_location_inst,
						    malloc_inst,
						    free_inst,
						    envp))
				return -1;

			self->emscripten_asm_module = module_inst;
		}

		if (self->emscripten_asm_module != module_inst) {
			ret = -1;
			goto error;
		}
	}

	environ_constructor = wasmjit_get_export(module_inst,
						 "___emscripten_environ_constructor",
						 IMPORT_DESC_TYPE_FUNC).func;
	ret = wasmjit_emscripten_build_environment(environ_constructor);
	if (ret) {
		ret = -1;
		goto error;
	}

	ret = wasmjit_emscripten_invoke_main(meminst,
					     stack_alloc_inst,
					     main_inst,
					     argc, argv);
 error:
	return ret;
}

void wasmjit_high_close(struct WasmJITHigh *self)
{
	size_t i;

#ifdef WASMJIT_CAN_USE_DEVICE
	if (self->fd >= 0) {
		(void)close(self->fd);
		return;
	}
#endif

	if (self->emscripten_env_module)
		wasmjit_emscripten_cleanup(self->emscripten_env_module);

	self->error_buffer[0] = '\0';

	for (i = 0; i < self->n_modules; ++i) {
		free(self->modules[i].name);
		wasmjit_free_module_inst(self->modules[i].module);
	}
	if (self->modules)
		free(self->modules);

}

int wasmjit_high_error_message(struct WasmJITHigh *self,
			      char *buf, size_t buf_size)
{
#ifdef WASMJIT_CAN_USE_DEVICE
	if (self->fd >= 0) {
		struct kwasmjit_error_message_args arg;

		arg.version = 0;
		arg.buffer = buf;
		arg.size = buf_size;

		return ioctl(self->fd, KWASMJIT_ERROR_MESSAGE, &arg);
	}
#endif

	strncpy(buf, self->error_buffer, buf_size);
	if (buf_size > 0)
		buf[buf_size - 1] = '\0';

	return 0;
}
