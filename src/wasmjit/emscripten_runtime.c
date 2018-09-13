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

#include <wasmjit/emscripten_runtime.h>

#include <wasmjit/emscripten_runtime_sys.h>

#include <wasmjit/runtime.h>

static int wasmjit_emscripten_check_range(struct MemInst *meminst,
					  uint32_t user_ptr,
					  size_t extent)
{
	return user_ptr + extent <= meminst->size;
}

static size_t wasmjit_emscripten_copy_to_user(struct MemInst *meminst,
					      uint32_t user_dest_ptr,
					      void *src,
					      size_t src_size)
{
	if (!wasmjit_emscripten_check_range(meminst, user_dest_ptr, src_size)) {
		return src_size;
	}

	memcpy(user_dest_ptr + meminst->data, src, src_size);
	return 0;
}

static size_t wasmjit_emscripten_copy_from_user(struct MemInst *meminst,
						void *dest,
						uint32_t user_src_ptr,
						size_t src_size)
{
	if (!wasmjit_emscripten_check_range(meminst, user_src_ptr, src_size)) {
		return src_size;
	}

	memcpy(dest, user_src_ptr + meminst->data, src_size);
	return 0;
}

/* shortcut functions */
#define _wasmjit_emscripten_check_range(funcinst, user_ptr, src_size) \
	wasmjit_emscripten_check_range(wasmjit_emscripten_get_mem_inst(funcinst), \
				       user_ptr,			\
				       src_size)			\

#define _wasmjit_emscripten_copy_to_user(funcinst, user_dest_ptr, src, src_size) \
	wasmjit_emscripten_copy_to_user(wasmjit_emscripten_get_mem_inst(funcinst), \
					user_dest_ptr,			\
					src,				\
					src_size)			\

#define _wasmjit_emscripten_copy_from_user(funcinst, dest, user_src_ptr, src_size) \
	wasmjit_emscripten_copy_from_user(wasmjit_emscripten_get_mem_inst(funcinst), \
					  dest,				\
					  user_src_ptr,			\
					  src_size)			\

static char *wasmjit_emscripten_get_base_address(struct FuncInst *funcinst) {
	return wasmjit_emscripten_get_mem_inst(funcinst)->data;
}

int wasmjit_emscripten_init_for_module(struct EmscriptenContext *ctx,
				       struct FuncInst *errno_location_inst)
{
	struct FuncType errno_location_type;
	wasmjit_valtype_t errno_location_return_type = VALTYPE_I32;

	_wasmjit_create_func_type(&errno_location_type,
				  0, NULL,
				  1, &errno_location_return_type);

	if (!wasmjit_typecheck_func(&errno_location_type,
				    errno_location_inst)) {
		return -1;
	}

	ctx->errno_location_inst = errno_location_inst;

	return 0;
}

int wasmjit_emscripten_invoke_main(struct MemInst *meminst,
				   struct FuncInst *stack_alloc_inst,
				   struct FuncInst *main_inst,
				   int argc,
				   char *argv[]) {
	uint32_t (*stack_alloc)(uint32_t);
	union ValueUnion out;
	int ret;

	if (!(stack_alloc_inst->type.n_inputs == 1 &&
	      stack_alloc_inst->type.input_types[0] == VALTYPE_I32 &&
	      stack_alloc_inst->type.output_type)) {
		return -1;
	}

	stack_alloc = stack_alloc_inst->compiled_code;

	if (main_inst->type.n_inputs == 0 &&
	    main_inst->type.output_type == VALTYPE_I32) {
		ret = wasmjit_invoke_function(main_inst, NULL, &out);
	} else if (main_inst->type.n_inputs == 2 &&
		   main_inst->type.input_types[0] == VALTYPE_I32 &&
		   main_inst->type.input_types[1] == VALTYPE_I32 &&
		   main_inst->type.output_type == VALTYPE_I32) {
		uint32_t argv_i, zero = 0;
		int i;
		union ValueUnion args[2];

		argv_i = stack_alloc((argc + 1) * 4);

		for (i = 0; i < argc; ++i) {
			size_t len = strlen(argv[i]) + 1;
			uint32_t ret = stack_alloc(len);

			if (wasmjit_emscripten_copy_to_user(meminst,
							    ret,
							    argv[i],
							    len))
				return -1;

			if (wasmjit_emscripten_copy_to_user(meminst,
							    argv_i + i * 4,
							    &ret,
							    4))
				return -1;
		}

		if (wasmjit_emscripten_copy_to_user(meminst,
						    argv_i + argc * 4,
						    &zero, 4))
			return -1;

		args[0].i32 = argc;
		args[1].i32 = argv_i;

		ret = wasmjit_invoke_function(main_inst, args, &out);

		/* TODO: copy back mutations to argv? */
	} else {
		return -1;
	}

	if (ret)
		return ret;

	return 0xff & out.i32;
}

/* shortcut function */
static struct EmscriptenContext *_wasmjit_emscripten_get_context(struct FuncInst *funcinst)
{
	return wasmjit_emscripten_get_context(funcinst->module_inst);
}

static int32_t check_ret(long ret) {
	/* these are defined by GCC */
#if __LONG_MAX__ > __INT32_MAX__
	if (ret > INT32_MAX || ret < INT32_MIN) {
		wasmjit_trap(WASMJIT_TRAP_INTEGER_OVERFLOW);
	}
#endif
	return ret;
}

void wasmjit_emscripten_abortStackOverflow(uint32_t allocSize, struct FuncInst *funcinst)
{
	(void)funcinst;
	(void)allocSize;
	wasmjit_emscripten_abort("Stack overflow!");
}

uint32_t wasmjit_emscripten_abortOnCannotGrowMemory(struct FuncInst *funcinst)
{
	(void)funcinst;
	wasmjit_emscripten_abort("Cannot enlarge memory arrays.");
	return 0;
}

uint32_t wasmjit_emscripten_enlargeMemory(struct FuncInst *funcinst)
{
	(void)funcinst;
	return 0;
}

uint32_t wasmjit_emscripten_getTotalMemory(struct FuncInst *funcinst)
{
	(void)funcinst;
	return WASMJIT_EMSCRIPTEN_TOTAL_MEMORY;
}

void wasmjit_emscripten_nullFunc_ii(uint32_t x, struct FuncInst *funcinst)
{
	(void)funcinst;
	(void)x;
	wasmjit_emscripten_abort("Invalid function pointer called with signature 'ii'. Perhaps this is an invalid value (e.g. caused by calling a virtual method on a NULL pointer)? Or calling a function with an incorrect type, which will fail? (it is worth building your source files with -Werror (warnings are errors), as warnings can indicate undefined behavior which can cause this)");
}

void wasmjit_emscripten_nullFunc_iiii(uint32_t x, struct FuncInst *funcinst)
{
	(void)funcinst;
	(void)x;
	wasmjit_emscripten_abort("Invalid function pointer called with signature 'iiii'. Perhaps this is an invalid value (e.g. caused by calling a virtual method on a NULL pointer)? Or calling a function with an incorrect type, which will fail? (it is worth building your source files with -Werror (warnings are errors), as warnings can indicate undefined behavior which can cause this)");
}

void wasmjit_emscripten____lock(uint32_t x, struct FuncInst *funcinst)
{
	(void)funcinst;
	(void)x;
}

void wasmjit_emscripten____setErrNo(uint32_t value, struct FuncInst *funcinst)
{
	union ValueUnion out;
	int ret;
	struct EmscriptenContext *ctx =
		_wasmjit_emscripten_get_context(funcinst);

	ret = wasmjit_invoke_function(ctx->errno_location_inst, NULL, &out);
	if (!ret &&
	    !_wasmjit_emscripten_copy_to_user(funcinst, out.i32, &value, sizeof(value)))
			return;
	wasmjit_emscripten_abort("failed to set errno from JS");
}

/*  _llseek */
uint32_t wasmjit_emscripten____syscall140(uint32_t which, uint32_t varargs, struct FuncInst *funcinst)
{
	char *base;
	struct {
		uint32_t fd, offset_high, offset_low,
			result, whence;
	} args;
	int32_t ret;

	(void)which;

	base = wasmjit_emscripten_get_base_address(funcinst);

	if (_wasmjit_emscripten_copy_from_user(funcinst,
					       &args, varargs, sizeof(args)))
		return -EFAULT;

	if (!_wasmjit_emscripten_check_range(funcinst, args.result, 4))
		return -EFAULT;

	// emscripten off_t is 32-bits, offset_high is useless
	if (args.offset_high)
		return -EINVAL;

	ret = check_ret(sys_lseek(args.fd, args.offset_low, args.whence));

	if (ret) {
		return ret;
	} else {
		memcpy(base + args.result, &ret, sizeof(ret));
		return 0;
	}
}

/* writev */
uint32_t wasmjit_emscripten____syscall146(uint32_t which, uint32_t varargs, struct FuncInst *funcinst)
{
	uint32_t i;
	char *base;
	struct {
		uint32_t fd, iov, iovcnt;
	} args;
	long rret;
	struct iovec *liov;

	(void)which;
	base = wasmjit_emscripten_get_base_address(funcinst);

	if (_wasmjit_emscripten_copy_from_user(funcinst,
					       &args, varargs,
					       sizeof(args)))
		return -EFAULT;

	/* TODO: do UIO_FASTIOV stack optimization */
	liov = wasmjit_alloc_vector(args.iovcnt,
				    sizeof(struct iovec), NULL);
	if (!liov) {
		return -ENOMEM;
	}

	for (i = 0; i < args.iovcnt; ++i) {
		struct em_iovec {
			uint32_t iov_base;
			uint32_t iov_len;
		} iov;
		if (_wasmjit_emscripten_copy_from_user(funcinst,
						       &iov,
						       args.iov +
						       sizeof(struct em_iovec) * i,
						       sizeof(struct em_iovec))) {
			rret = -EFAULT;
			goto error;
		}

		if (!_wasmjit_emscripten_check_range(funcinst,
						     iov.iov_base,
						     iov.iov_len)) {
			rret = -EFAULT;
			goto error;
		}

		liov[i].iov_base = base + iov.iov_base;
		liov[i].iov_len = iov.iov_len;
	}

	rret = sys_writev(args.fd, liov, args.iovcnt);

 error:
	free(liov);

	return check_ret(rret);
}

/* write */
uint32_t wasmjit_emscripten____syscall4(uint32_t which, uint32_t varargs, struct FuncInst *funcinst)
{
	char *base;
	struct {
		uint32_t fd, buf, count;
	} args;

	(void)which;

	if (_wasmjit_emscripten_copy_from_user(funcinst, &args, varargs, sizeof(args)))
		return -EFAULT;

	if (!_wasmjit_emscripten_check_range(funcinst, args.buf, args.count))
		return -EFAULT;

	base = wasmjit_emscripten_get_base_address(funcinst);

	return check_ret(sys_write(args.fd, base + args.buf, args.count));
}

/* ioctl */
uint32_t wasmjit_emscripten____syscall54(uint32_t which, uint32_t varargs, struct FuncInst *funcinst)
{
	(void)funcinst;
	/* TODO: need to define non-no filesystem case */
	(void)which;
	(void)varargs;
	return 0;
}

/* close */
uint32_t wasmjit_emscripten____syscall6(uint32_t which, uint32_t varargs, struct FuncInst *funcinst)
{
	/* TODO: need to define non-no filesystem case */
	struct {
		uint32_t fd;
	} args;

	(void)which;

	if (_wasmjit_emscripten_copy_from_user(funcinst, &args, varargs, sizeof(args)))
		return -EFAULT;

	return check_ret(sys_close(args.fd));
}

void wasmjit_emscripten____unlock(uint32_t x, struct FuncInst *funcinst)
{
	(void)funcinst;
	(void)x;
}

uint32_t wasmjit_emscripten__emscripten_memcpy_big(uint32_t dest, uint32_t src, uint32_t num, struct FuncInst *funcinst)
{
	char *base = wasmjit_emscripten_get_base_address(funcinst);
	if (!_wasmjit_emscripten_check_range(funcinst, dest, num) ||
	    !_wasmjit_emscripten_check_range(funcinst, src, num)) {
		wasmjit_trap(WASMJIT_TRAP_MEMORY_OVERFLOW);
	}
	memcpy(dest + base, src + base, num);
	return dest;
}

void wasmjit_emscripten_cleanup(struct ModuleInst *moduleinst) {
	(void)moduleinst;
	/* TODO: implement */
}
