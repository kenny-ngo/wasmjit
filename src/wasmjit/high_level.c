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
				 const char *module_name)
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

	if (module_inst) {
		wasmjit_free_module_inst(module_inst);
	}

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
