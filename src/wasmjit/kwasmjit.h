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

#ifndef __KWASMJIT__KWASMJIT_H
#define __KWASMJIT__KWASMJIT_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stddef.h>
#endif

#include <linux/ioctl.h>

#define KWASMJIT_MAGIC 0xCC

struct kwasmjit_instantiate_args {
	unsigned version;
	const char *file_name;
	const char *module_name;
	int flags;
};

struct kwasmjit_instantiate_emscripten_runtime_args {
	unsigned version;
	size_t tablemin, tablemax;
	int flags;
};

struct kwasmjit_emscripten_invoke_main_args {
	unsigned version;
	const char *module_name;
	int argc;
	char **argv;
	int flags;
};

#define KWASMJIT_INSTANTIATE _IOW(KWASMJIT_MAGIC, 0, struct kwasmjit_instantiate_args)
#define KWASMJIT_INSTANTIATE_EMSCRIPTEN_RUNTIME _IOW(KWASMJIT_MAGIC, 1, struct kwasmjit_instantiate_emscripten_runtime_args)
#define KWASMJIT_EMSCRIPTEN_INVOKE_MAIN _IOW(KWASMJIT_MAGIC, 2, struct kwasmjit_emscripten_invoke_main_args)

#endif
