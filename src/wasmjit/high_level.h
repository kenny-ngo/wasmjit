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

#ifndef __WASMJIT__HIGH_LEVEL_H
#define __WASMJIT__HIGH_LEVEL_H

#include <wasmjit/sys.h>

/* this interface mimics the kernel interface and thus lacks power
   since we can't pass in abitrary objects for import, like host functions */

#if (defined(__linux__) || defined(__APPLE__) || defined(__unix__)) && !defined(__KERNEL__)
#define WASMJIT_CAN_USE_DEVICE
#endif

struct WasmJITHigh {
#ifdef WASMJIT_CAN_USE_DEVICE
	int fd;
#endif
	size_t n_modules;
	struct NamedModule *modules;
	char error_buffer[256];
	struct ModuleInst *emscripten_asm_module;
	struct ModuleInst *emscripten_env_module;
};

int wasmjit_high_init(struct WasmJITHigh *self);
int wasmjit_high_instantiate(struct WasmJITHigh *self,
			     const char *filename,
			     const char *module_name,
			     int flags);
int wasmjit_high_instantiate_emscripten_runtime(struct WasmJITHigh *self,
						uint32_t static_bump,
						size_t tablemin,
						size_t tablemax,
						int flags);
int wasmjit_high_emscripten_invoke_main(struct WasmJITHigh *self,
					const char *module_name,
					int argc, char **argv, char **envp,
					int flags);
void wasmjit_high_close(struct WasmJITHigh *self);
int wasmjit_high_error_message(struct WasmJITHigh *self, char *buf, size_t buf_size);


#endif
