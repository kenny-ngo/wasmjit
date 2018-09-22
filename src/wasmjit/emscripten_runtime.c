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
#include <wasmjit/util.h>
#include <wasmjit/runtime.h>
#include <wasmjit/sys.h>

#define STATIC_ASSERT(COND,MSG) typedef char static_assertion_##MSG[(COND)?1:-1]
#define COMPILE_TIME_ASSERT3(X,L) STATIC_ASSERT(X,static_assertion_at_line_##L)
#define COMPILE_TIME_ASSERT2(X,L) COMPILE_TIME_ASSERT3(X,L)
#define COMPILE_TIME_ASSERT(X)    COMPILE_TIME_ASSERT2(X,__LINE__)

/* We directly memcpy int32_t from wasm memory */
/* TODO: fix this */
COMPILE_TIME_ASSERT(-1 == ~0);

/* we need to at least be able to compute wasm addresses */
COMPILE_TIME_ASSERT(sizeof(size_t) >= sizeof(uint32_t));

/* the sys_ interface uses ints extensively,
   we need to be able to represent wasm values in ints */
COMPILE_TIME_ASSERT(sizeof(int) >= sizeof(uint32_t));

#define __MMAP0(args,m,...)
#define __MMAP1(args,m,t,a,...) m(args, t, a)
#define __MMAP2(args,m,t,a,...) m(args, t, a) __MMAP1(args,m,__VA_ARGS__)
#define __MMAP3(args,m,t,a,...) m(args, t, a) __MMAP2(args,m,__VA_ARGS__)
#define __MMAP4(args,m,t,a,...) m(args, t, a) __MMAP3(args,m,__VA_ARGS__)
#define __MMAP5(args,m,t,a,...) m(args, t, a) __MMAP4(args,m,__VA_ARGS__)
#define __MMAP6(args,m,t,a,...) m(args, t, a) __MMAP5(args,m,__VA_ARGS__)
#define __MMAP7(args,m,t,a,...) m(args, t, a) __MMAP6(args,m,__VA_ARGS__)
#define __MMAP(args,n,...) __MMAP##n(args, __VA_ARGS__)

#define __DECL(args, t, a) t a;
#define __SWAP(args, t, a) args.a = t ## _swap_bytes(args.a);

static int32_t int32_t_swap_bytes(int32_t a)
{
	return uint32_t_swap_bytes(a);
}

#define LOAD_ARGS_CUSTOM(args, funcinst, varargs, n, ...)		\
	struct {							\
		__MMAP(args, n, __DECL, __VA_ARGS__)			\
	} args;								\
	if (_wasmjit_emscripten_copy_from_user(funcinst,		\
					       &args, varargs,		\
					       sizeof(args)))		\
		return -SYS_EFAULT;					\
	__MMAP(args, n, __SWAP, __VA_ARGS__)

#define LOAD_ARGS(...)				\
	LOAD_ARGS_CUSTOM(args, __VA_ARGS__)

enum {
#define ERRNO(name, value) SYS_ ## name = value,
#include <wasmjit/emscripten_runtime_sys_errno_def.h>
#undef ERRNO
};

#ifndef __LONG_WIDTH__
#ifdef __LP64__
#define __LONG_WIDTH__ 64
#else
#error Please define __LONG_WIDTH__
#endif
#endif

/* error codes are the same for these targets */
#if (defined(__KERNEL__) || defined(__linux__)) && defined(__x86_64__)

static int32_t check_ret(long errno_)
{
#if __LONG_WIDTH__ > 32
	if (errno_ < -2147483648)
		wasmjit_trap(WASMJIT_TRAP_INTEGER_OVERFLOW);
#endif
#if __LONG_WIDTH__ > 32
	if (errno_ > INT32_MAX)
		wasmjit_trap(WASMJIT_TRAP_INTEGER_OVERFLOW);
#endif
	return errno_;
}

#else

static int32_t check_ret(long errno_)
{
	static int32_t to_sys_errno[] = {
#define ERRNO(name, value) [name] = -value,
#include <wasmjit/emscripten_runtime_sys_errno_def.h>
#undef ERRNO
	};

	int32_t toret;

	if (errno_ >= 0) {
#if __LONG_WIDTH__ > 32
		if (errno_ > INT32_MAX)
			wasmjit_trap(WASMJIT_TRAP_INTEGER_OVERFLOW);
#endif
		return errno_;
	}

	if (errno_ == LONG_MIN)
		wasmjit_trap(WASMJIT_TRAP_INTEGER_OVERFLOW);

	errno_ = -errno_;

	if ((size_t) errno_ >= sizeof(to_sys_errno) / sizeof(to_sys_errno[0])) {
		toret = -SYS_EINVAL;
	} else {
		toret = to_sys_errno[errno_];
		if (!toret)
			toret = -SYS_EINVAL;
	}

	return toret;
}

#endif

static int wasmjit_emscripten_check_range(struct MemInst *meminst,
					  uint32_t user_ptr,
					  size_t extent)
{
	size_t ret;
	if (__builtin_add_overflow(user_ptr, extent, &ret))
		return 0;
	return ret <= meminst->size;
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

static int _wasmjit_emscripten_check_string(struct FuncInst *funcinst,
					    uint32_t user_ptr,
					    size_t max)
{
	struct MemInst *meminst = wasmjit_emscripten_get_mem_inst(funcinst);
	size_t len = 0;

	while (user_ptr + len < meminst->size && len < max) {
		if (!*(meminst->data + user_ptr + len))
			return 1;
		len += 1;
	}

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

int wasmjit_emscripten_init(struct EmscriptenContext *ctx,
			    struct FuncInst *errno_location_inst,
			    struct FuncInst *malloc_inst,
			    struct FuncInst *free_inst,
			    char *envp[])
{
	assert(malloc_inst);
	{
		struct FuncType malloc_type;
		wasmjit_valtype_t malloc_input_type = VALTYPE_I32;
		wasmjit_valtype_t malloc_return_type = VALTYPE_I32;

		_wasmjit_create_func_type(&malloc_type,
					  1, &malloc_input_type,
					  1, &malloc_return_type);

		if (!wasmjit_typecheck_func(&malloc_type,
					    malloc_inst))
			return -1;
	}

	assert(free_inst);
	{
		struct FuncType free_type;
		wasmjit_valtype_t free_input_type = VALTYPE_I32;

		_wasmjit_create_func_type(&free_type,
					  1, &free_input_type,
					  0, NULL);

		if (!wasmjit_typecheck_func(&free_type,
					    free_inst))
			return -1;
	}

	if (errno_location_inst) {
		struct FuncType errno_location_type;
		wasmjit_valtype_t errno_location_return_type = VALTYPE_I32;

		_wasmjit_create_func_type(&errno_location_type,
					  0, NULL,
					  1, &errno_location_return_type);

		if (!wasmjit_typecheck_func(&errno_location_type,
					    errno_location_inst)) {
			return -1;
		}
	}

	ctx->errno_location_inst = errno_location_inst;
	ctx->malloc_inst = malloc_inst;
	ctx->free_inst = free_inst;
	ctx->environ = envp;
	ctx->buildEnvironmentCalled = 0;

	return 0;
}

int wasmjit_emscripten_build_environment(struct FuncInst *environ_constructor)
{
	int ret;
	if (environ_constructor) {
		struct FuncType errno_location_type;

		_wasmjit_create_func_type(&errno_location_type,
					  0, NULL,
					  0, NULL);

		if (!wasmjit_typecheck_func(&errno_location_type,
					    environ_constructor)) {
			return -1;
		}

		ret = wasmjit_invoke_function(environ_constructor, NULL, NULL);
		if (ret)
			return -1;
	}

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
	} else if ((main_inst->type.n_inputs == 2 ||
		    (main_inst->type.n_inputs == 3 &&
		     main_inst->type.input_types[2] == VALTYPE_I32)) &&
		   main_inst->type.input_types[0] == VALTYPE_I32 &&
		   main_inst->type.input_types[1] == VALTYPE_I32 &&
		   main_inst->type.output_type == VALTYPE_I32) {
		uint32_t argv_i, zero = 0;
		int i;
		union ValueUnion args[3];

		argv_i = stack_alloc((argc + 1) * 4);

		for (i = 0; i < argc; ++i) {
			size_t len = strlen(argv[i]) + 1;
			uint32_t ret = stack_alloc(len);

			if (wasmjit_emscripten_copy_to_user(meminst,
							    ret,
							    argv[i],
							    len))
				return -1;

			ret = uint32_t_swap_bytes(ret);

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
		args[2].i32 = 0;

		ret = wasmjit_invoke_function(main_inst, args, &out);

		/* TODO: copy back mutations to argv? */
	} else {
		return -1;
	}

	if (ret) {
		if (ret > 0) {
			/* these are trap return codes, shift it left
			   8 bits to distinguish from return code from
			   normal execution of wasm main() */
			ret = WASMJIT_ENCODE_TRAP_ERROR(ret);
		}

		return ret;
	}

	return 0xff & out.i32;
}

/* shortcut function */
static struct EmscriptenContext *_wasmjit_emscripten_get_context(struct FuncInst *funcinst)
{
	return wasmjit_emscripten_get_context(funcinst->module_inst);
}

void wasmjit_emscripten_abortStackOverflow(uint32_t allocSize, struct FuncInst *funcinst)
{
	(void)funcinst;
	(void)allocSize;
	wasmjit_emscripten_internal_abort("Stack overflow!");
}

uint32_t wasmjit_emscripten_abortOnCannotGrowMemory(struct FuncInst *funcinst)
{
	(void)funcinst;
	wasmjit_emscripten_internal_abort("Cannot enlarge memory arrays.");
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
	wasmjit_emscripten_internal_abort("Invalid function pointer called with signature 'ii'. Perhaps this is an invalid value (e.g. caused by calling a virtual method on a NULL pointer)? Or calling a function with an incorrect type, which will fail? (it is worth building your source files with -Werror (warnings are errors), as warnings can indicate undefined behavior which can cause this)");
}

void wasmjit_emscripten_nullFunc_iiii(uint32_t x, struct FuncInst *funcinst)
{
	(void)funcinst;
	(void)x;
	wasmjit_emscripten_internal_abort("Invalid function pointer called with signature 'iiii'. Perhaps this is an invalid value (e.g. caused by calling a virtual method on a NULL pointer)? Or calling a function with an incorrect type, which will fail? (it is worth building your source files with -Werror (warnings are errors), as warnings can indicate undefined behavior which can cause this)");
}

void wasmjit_emscripten____lock(uint32_t x, struct FuncInst *funcinst)
{
	(void)funcinst;
	(void)x;
}

void wasmjit_emscripten____setErrNo(uint32_t value, struct FuncInst *funcinst)
{
	union ValueUnion out;
	struct EmscriptenContext *ctx =
		_wasmjit_emscripten_get_context(funcinst);

	if (ctx->errno_location_inst &&
	    !wasmjit_invoke_function(ctx->errno_location_inst, NULL, &out)) {
		value = uint32_t_swap_bytes(value);
		if (!_wasmjit_emscripten_copy_to_user(funcinst, out.i32, &value, sizeof(value)))
			return;
	}
	wasmjit_emscripten_internal_abort("failed to set errno from JS");
}

/*  _llseek */
uint32_t wasmjit_emscripten____syscall140(uint32_t which, uint32_t varargs, struct FuncInst *funcinst)
{
	char *base;
	int32_t ret;

	LOAD_ARGS(funcinst, varargs, 5,
		  int32_t, fd,
		  uint32_t, offset_high,
		  int32_t, offset_low,
		  uint32_t, result,
		  int32_t, whence);

	(void)which;

	base = wasmjit_emscripten_get_base_address(funcinst);

	if (!_wasmjit_emscripten_check_range(funcinst, args.result, 4))
		return -SYS_EFAULT;

	// emscripten off_t is 32-bits, offset_high is useless
	if (args.offset_high)
		return -SYS_EINVAL;

	ret = check_ret(sys_lseek(args.fd, args.offset_low, args.whence));

	if (ret) {
		return ret;
	} else {
		memcpy(base + args.result, &ret, sizeof(ret));
		return 0;
	}
}

struct em_iovec {
	uint32_t iov_base;
	uint32_t iov_len;
};

static long copy_iov(struct FuncInst *funcinst,
		     uint32_t iov_user,
		     uint32_t iov_len, struct iovec **out)
{
	struct iovec *liov = NULL;
	char *base;
	long ret;
	uint32_t i;

	base = wasmjit_emscripten_get_base_address(funcinst);

	/* TODO: do UIO_FASTIOV stack optimization */
	liov = wasmjit_alloc_vector(iov_len,
				    sizeof(struct iovec), NULL);
	if (!liov) {
		ret = -ENOMEM;
		goto error;
	}

	for (i = 0; i < iov_len; ++i) {
		struct em_iovec iov;

		if (_wasmjit_emscripten_copy_from_user(funcinst,
						       &iov,
						       iov_user +
						       sizeof(struct em_iovec) * i,
						       sizeof(struct em_iovec))) {
			ret = -EFAULT;
			goto error;
		}

		iov.iov_base = uint32_t_swap_bytes(iov.iov_base);
		iov.iov_len = uint32_t_swap_bytes(iov.iov_len);

		if (!_wasmjit_emscripten_check_range(funcinst,
						     iov.iov_base,
						     iov.iov_len)) {
			ret = -EFAULT;
			goto error;
		}

		liov[i].iov_base = base + iov.iov_base;
		liov[i].iov_len = iov.iov_len;
	}

	*out = liov;
	ret = 0;

	if (0) {
	error:
		free(liov);
	}

	return ret;
}

/* writev */
uint32_t wasmjit_emscripten____syscall146(uint32_t which, uint32_t varargs, struct FuncInst *funcinst)
{
	long rret;
	struct iovec *liov;

	LOAD_ARGS(funcinst, varargs, 3,
		  int32_t, fd,
		  uint32_t, iov,
		  uint32_t, iovcnt);

	(void)which;

	rret = copy_iov(funcinst, args.iov, args.iovcnt, &liov);
	if (rret)
		goto error;

	rret = sys_writev(args.fd, liov, args.iovcnt);

	free(liov);

 error:
	return check_ret(rret);
}

/* write */
uint32_t wasmjit_emscripten____syscall4(uint32_t which, uint32_t varargs, struct FuncInst *funcinst)
{
	char *base;

	LOAD_ARGS(funcinst, varargs, 3,
		  int32_t, fd,
		  uint32_t, buf,
		  uint32_t, count);

	(void)which;

	if (!_wasmjit_emscripten_check_range(funcinst, args.buf, args.count))
		return -SYS_EFAULT;

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
	LOAD_ARGS(funcinst, varargs, 1,
		  int32_t, fd);

	(void)which;
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

__attribute__((noreturn))
void wasmjit_emscripten_abort(uint32_t what, struct FuncInst *funcinst)
{
	struct MemInst *meminst = wasmjit_emscripten_get_mem_inst(funcinst);
	char *abort_string;
	if (!_wasmjit_emscripten_check_string(funcinst, what, 256)) {
		abort_string = "Invalid abort string";
	} else {
		abort_string = meminst->data + what;
	}
	wasmjit_emscripten_internal_abort(abort_string);
}

static uint32_t getMemory(struct EmscriptenContext *ctx,
			  uint32_t amount)
{
	union ValueUnion input, output;
	input.i32 = amount;
	if (wasmjit_invoke_function(ctx->malloc_inst, &input, &output))
		wasmjit_emscripten_internal_abort("Failed to invoke allocator");
	return output.i32;
}

static void freeMemory(struct EmscriptenContext *ctx,
		       uint32_t ptr)
{
	union ValueUnion input;
	input.i32 = ptr;
	if (wasmjit_invoke_function(ctx->free_inst, &input, NULL))
		wasmjit_emscripten_internal_abort("Failed to invoke deallocator");
}

void wasmjit_emscripten____buildEnvironment(uint32_t environ_arg,
					    struct FuncInst *funcinst)
{
	char *base = wasmjit_emscripten_get_base_address(funcinst);
	uint32_t envPtr;
	uint32_t poolPtr;
	uint32_t total_pool_size;
	uint32_t n_envs;
	size_t i;
	char **env;
	struct EmscriptenContext *ctx = _wasmjit_emscripten_get_context(funcinst);

	if (ctx->buildEnvironmentCalled) {
		/* free old stuff */
		if (_wasmjit_emscripten_copy_from_user(funcinst,
						       &envPtr,
						       environ_arg,
						       sizeof(envPtr)))
			wasmjit_trap(WASMJIT_TRAP_MEMORY_OVERFLOW);

		envPtr = uint32_t_swap_bytes(envPtr);

		freeMemory(ctx, envPtr);

		if (_wasmjit_emscripten_copy_from_user(funcinst,
						       &poolPtr,
						       envPtr,
						       sizeof(poolPtr)))
			wasmjit_trap(WASMJIT_TRAP_MEMORY_OVERFLOW);

		poolPtr = uint32_t_swap_bytes(poolPtr);

		freeMemory(ctx, poolPtr);
	}

	n_envs = 0;
	total_pool_size = 0;
	for (env = ctx->environ; *env; ++env) {
		total_pool_size += strlen(*env) + 1;
		n_envs += 1;
	}

	poolPtr = getMemory(ctx, total_pool_size);
	if (!poolPtr)
		wasmjit_emscripten_internal_abort("Failed to allocate memory in critical region");

	/* double check user space isn't malicious */
	if (!_wasmjit_emscripten_check_range(funcinst, poolPtr, total_pool_size))
		wasmjit_trap(WASMJIT_TRAP_MEMORY_OVERFLOW);

	envPtr = getMemory(ctx, (n_envs + 1) * 4);
	if (!envPtr)
		wasmjit_emscripten_internal_abort("Failed to allocate memory in critical region");

	/* double check user space isn't malicious */
	if (!_wasmjit_emscripten_check_range(funcinst, envPtr, (n_envs + 1) * 4))
		wasmjit_trap(WASMJIT_TRAP_MEMORY_OVERFLOW);

	{
		uint32_t ep;
		ep = uint32_t_swap_bytes(envPtr);
		if (_wasmjit_emscripten_copy_to_user(funcinst,
						     environ_arg,
						     &ep,
						     sizeof(envPtr)))
			wasmjit_trap(WASMJIT_TRAP_MEMORY_OVERFLOW);
	}

	for (env = ctx->environ, i = 0; *env; ++env, ++i) {
		size_t len = strlen(*env);
		uint32_t pp;
		/* NB: these memcpys are checked above */
		memcpy(base + poolPtr, *env, len + 1);
		pp = uint32_t_swap_bytes(poolPtr);
		memcpy(base + envPtr + i * sizeof(uint32_t),
		       &pp, sizeof(poolPtr));
		poolPtr += len + 1;
	}

	poolPtr = 0;
	memset(base + envPtr + i * sizeof(uint32_t),
	       0, sizeof(poolPtr));

	ctx->buildEnvironmentCalled = 1;
}

uint32_t wasmjit_emscripten____syscall10(uint32_t which, uint32_t varargs,
					 struct FuncInst *funcinst)
{
	char *base;

	LOAD_ARGS(funcinst, varargs, 1,
		  uint32_t, pathname);

	(void)which;

	base = wasmjit_emscripten_get_base_address(funcinst);

	if (!_wasmjit_emscripten_check_string(funcinst, args.pathname, PATH_MAX))
		return -SYS_EFAULT;

	return check_ret(sys_unlink(base + args.pathname));
}

#ifndef __INT_WIDTH__
#ifdef __LP64__
#define __INT_WIDTH__ 32
#else
#error Please define __INT_WIDTH__
#endif
#endif

#if (defined(__linux__) || defined(__KERNEL__)) && !defined(__mips__)

static int convert_socket_type_to_local(int32_t type)
{
	return type;
}

#else

static int convert_socket_type_to_local(int32_t type)
{
	int ltype, nonblock_type, cloexec_type;

#define SYS_SOCK_NONBLOCK 2048
	nonblock_type = !!(type & SYS_SOCK_NONBLOCK);
	type &= ~(int) SYS_SOCK_NONBLOCK;
#define SYS_SOCK_CLOEXEC 524288
	cloexec_type = !!(type & SYS_SOCK_CLOEXEC);
	type &= ~(int) SYS_SOCK_CLOEXEC;

	switch (type) {
	case 1: ltype = SOCK_STREAM; break;
	case 2: ltype = SOCK_DGRAM; break;
	case 5: ltype = SOCK_SEQPACKET; break;
	case 3: ltype = SOCK_RAW; break;
	case 4: ltype = SOCK_RDM; break;
#ifdef SOCK_PACKET
	case 10: ltype = SOCK_PACKET; break;
#endif
	default: return -1;
	}

	if (nonblock_type) {
#ifdef SOCK_NONBLOCK
		ltype |= SOCK_NONBLOCK;
#else
		/*
		  user requested SOCK_NONBLOCK but runtime doesn't support it,
		  return a type of -1, which will invoke a EINVAL on sys_socket()
		 */
		return -1;
#endif
	}

	if (cloexec_type) {
#ifdef SOCK_CLOEXEC
		ltype |= SOCK_CLOEXEC;
#else
		/*
		  user requested SOCK_CLOEXEC but runtime doesn't support it,
		  return a type of -1, which will invoke a EINVAL on sys_socket()
		 */
		return -1;
#endif
	}

	return ltype;
}

#endif

#define SYS_AF_UNIX 1
#define SYS_AF_INET 2
#define SYS_AF_INET6 10

#if defined(__linux__) || defined(__KERNEL__)

static int convert_socket_domain_to_local(int32_t domain)
{
	return domain;
}

static int convert_proto_to_local(int domain, int32_t proto)
{
	(void)domain;
	return proto;
}

#else

static int convert_socket_domain_to_local(int32_t domain)
{
	switch (domain) {
	case SYS_AF_UNIX: return AF_UNIX;
	case SYS_AF_INET: return AF_INET;
	case SYS_AF_INET6: return AF_INET6;
	case 4: return AF_IPX;
#ifdef AF_NETLINK
	case 16: return AF_NETLINK;
#endif
#ifdef AF_X25
	case 9: return AF_X25;
#endif
#ifdef AF_AX25
	case 3: return AF_AX25;
#endif
#ifdef AF_ATMPVC
	case 8: return AF_ATMPVC;
#endif
	case 5: return AF_APPLETALK;
#ifdef AF_PACKET
	case 17: return AF_PACKET;
#endif
	default: return -1;
	}
}

static int convert_proto_to_local(int domain, int32_t proto)
{
	if (domain == AF_INET || domain == AF_INET6) {
		switch (proto) {
		case 0: return IPPROTO_IP;
		case 1: return IPPROTO_ICMP;
		case 2: return IPPROTO_IGMP;
		case 4: return IPPROTO_IPIP;
		case 6: return IPPROTO_TCP;
		case 8: return IPPROTO_EGP;
		case 12: return IPPROTO_PUP;
		case 17: return IPPROTO_UDP;
		case 22: return IPPROTO_IDP;
		case 29: return IPPROTO_TP;
#ifdef IPPROTO_DCCP
		case 33: return IPPROTO_DCCP;
#endif
		case 41: return IPPROTO_IPV6;
		case 46: return IPPROTO_RSVP;
		case 47: return IPPROTO_GRE;
		case 50: return IPPROTO_ESP;
		case 51: return IPPROTO_AH;
		case 92: return IPPROTO_MTP;
#ifdef IPPROTO_BEETPH
		case 94: return IPPROTO_BEETPH;
#endif
		case 98: return IPPROTO_ENCAP;
		case 103: return IPPROTO_PIM;
#ifdef IPPROTO_COMP
		case 108: return IPPROTO_COMP;
#endif
		case 132: return IPPROTO_SCTP;
#ifdef IPPROTO_UDPLITE
		case 136: return IPPROTO_UDPLITE;
#endif
#if IPPROTO_MPLS
		case 137: return IPPROTO_MPLS;
#endif
		case 255: return IPPROTO_RAW;
		default: return -1;
		}
	} else {
		if (proto)
			return -1;
		return 0;
	}
}

#endif


#define FAS 2

#if (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__ && (defined(__linux__) || defined(__KERNEL__)))
#define SAME_SOCKADDR
#endif

#ifndef SAME_SOCKADDR

static long read_sockaddr(struct sockaddr_storage *ss, size_t *size,
			  const char *addr, uint32_t len)
{
	uint16_t family;
	assert(sizeof(family) == FAS);

	if (len < FAS)
		return -1;

	memcpy(&family, addr, FAS);
	family = uint16_t_swap_bytes(family);

	switch (family) {
	case SYS_AF_UNIX: {
		struct sockaddr_un sun;
		if (len - FAS > sizeof(sun.sun_path))
			return -1;
		memset(&sun, 0, sizeof(sun));
		sun.sun_family = AF_UNIX;
		memcpy(&sun.sun_path, addr + FAS, len - FAS);
		*size = offsetof(struct sockaddr_un, sun_path) + (len - FAS);
		memcpy(ss, &sun, *size);
		break;
	}
	case SYS_AF_INET: {
		struct sockaddr_in sin;
		if (len < 8)
			return -1;
		memset(&sin, 0, sizeof(struct sockaddr_in));
		sin.sin_family = AF_INET;
		/* these are in network order so they don't need to be swapped */
		assert(sizeof(sin.sin_port) == 2);
		memcpy(&sin.sin_port, addr + FAS, 2);
		assert(sizeof(sin.sin_addr) == 4);
		memcpy(&sin.sin_addr, addr + FAS + 2, 4);
		*size = sizeof(struct sockaddr_in);
		memcpy(ss, &sin, *size);
		break;
	}
	case SYS_AF_INET6: {
		struct sockaddr_in6 sin6;

		if (len < 28)
			return -1;

		memset(&sin6, 0, sizeof(struct sockaddr_in6));
		sin6.sin6_family = AF_INET6;

		/* this is in network order so it doesn't need to be swapped */
		assert(sizeof(sin6.sin6_port) == 2);
		memcpy(&sin6.sin6_port, addr + FAS, 2);

		assert(4 == sizeof(sin6.sin6_flowinfo));
		memcpy(&sin6.sin6_flowinfo, addr + FAS + 2, 4);
		sin6.sin6_flowinfo = uint32_t_swap_bytes(sin6.sin6_flowinfo);

		/* this is in network order so it doesn't need to be swapped */
		assert(16 == sizeof(sin6.sin6_addr));
		memcpy(&sin6.sin6_addr, addr + FAS + 2 + 4, 16);

		assert(4 == sizeof(sin6.sin6_scope_id));
		memcpy(&sin6.sin6_scope_id, addr + FAS + 2 + 4 + 16, 4);
		sin6.sin6_scope_id = uint32_t_swap_bytes(sin6.sin6_scope_id);

		*size = sizeof(struct sockaddr_in6);
		memcpy(ss, &sin6, *size);
		break;
	}
	default: {
		/* TODO: add more support */
		return -1;
		break;
	}
	}

	return 0;
}

/* need to byte swap and adapt sockaddr to current platform */
static long write_sockaddr(struct sockaddr_storage *ss, socklen_t ssize,
			   char *addr, uint32_t addrlen, void *len)
{
	uint32_t newlen;

	switch (ss->ss_family) {
	case AF_UNIX: {
		struct sockaddr_un sun;
		uint16_t f = uint16_t_swap_bytes(SYS_AF_UNIX);

		newlen = FAS + (ssize - offsetof(struct sockaddr_un, sun_path));

		if (addrlen < newlen)
			return -1;

		if (ssize > sizeof(sun))
			return -1;

		memcpy(&sun, ss, ssize);

		memcpy(addr, &f, FAS);
		memcpy(addr + FAS, sun.sun_path, ssize - offsetof(struct sockaddr_un, sun_path));
		break;
	}
	case AF_INET: {
		struct sockaddr_in sin;
		uint16_t f = uint16_t_swap_bytes(SYS_AF_INET);

		newlen = FAS + 2 + 4;

		if (addrlen < newlen)
			return -1;

		if (ssize != sizeof(sin))
			return -1;

		memcpy(&sin, ss, sizeof(sin));

		memcpy(addr, &f, FAS);
		/* these are in network order so they don't need to be swapped */
		memcpy(addr + FAS, &sin.sin_port, 2);
		memcpy(addr + FAS + 2, &sin.sin_addr, 4);
		break;
	}
	case AF_INET6: {
		struct sockaddr_in6 sin6;
		uint16_t f = uint16_t_swap_bytes(SYS_AF_INET6);

		newlen = FAS + 2 + 4 + 16 + 4;

		if (addrlen < newlen)
			return -1;

		if (ssize != sizeof(sin6))
			return -1;

		memcpy(&sin6, ss, ssize);

		memcpy(addr, &f, FAS);

		/* this is stored in network order */
		memcpy(addr + FAS, &sin6.sin6_port, 2);

		sin6.sin6_flowinfo = uint32_t_swap_bytes(sin6.sin6_flowinfo);
		memcpy(addr + FAS + 2, &sin6.sin6_flowinfo, 4);

		/* this is stored in network order */
		memcpy(addr + FAS + 2 + 4, &sin6.sin6_addr, 16);

		sin6.sin6_scope_id = uint32_t_swap_bytes(sin6.sin6_scope_id);
		memcpy(addr + FAS + 2 + 4 + 16, &sin6.sin6_scope_id, 4);

		break;
	}
	default: {
		/* TODO: add more support */
		return -1;
		break;
	}
	}

	newlen = uint32_t_swap_bytes(newlen);
	memcpy(len, &newlen, sizeof(newlen));

	return 0;
}

#endif

#ifdef SAME_SOCKADDR

static long finish_bindlike(long (*bindlike)(int, const struct sockaddr *, socklen_t),
			    int fd, void *addr, size_t len)
{
	return bindlike(fd, addr, len);
}

#else

/* need to byte swap and adapt sockaddr to current platform */
static long finish_bindlike(long (*bindlike)(int, const struct sockaddr *, socklen_t),
			    int fd, char *addr, size_t len)
{
	struct sockaddr_storage ss;
	size_t ptr_size;

	if (read_sockaddr(&ss, &ptr_size, addr, len))
		return -EINVAL;

	return bindlike(fd, (void *) &ss, ptr_size);
}

#endif

#if __INT_WIDTH__ == 32 && defined(SAME_SOCKADDR)

static long finish_acceptlike(long (*acceptlike)(int, struct sockaddr *, socklen_t *),
			      int fd, void *addr, uint32_t addrlen, void *len)
{
	/* socklen_t == uint32_t */
	(void) addrlen;
	return acceptlike(fd, addr, len);
}

#else

/* need to byte swap and adapt sockaddr to current platform */
static long finish_acceptlike(long (*acceptlike)(int, struct sockaddr *, socklen_t *),
			      int fd, char *addr, uint32_t addrlen, void *len)
{
	long rret;
	struct sockaddr_storage ss;
	socklen_t ssize = sizeof(ss);

	rret = acceptlike(fd, (void *) &ss, &ssize);
	if (rret < 0)
		return rret;

	if (write_sockaddr(&ss, ssize, addr, addrlen, len)) {
		/* NB: we have to abort here because we can't undo the sys_accept() */
		wasmjit_emscripten_internal_abort("Failed to convert sockaddr");
	}

	return rret;
}

#endif


#if defined(__linux__) || defined(__KERNEL__)

static int convert_sendto_flags(int32_t flags)
{
	return flags;
}

static int has_bad_sendto_flag(int32_t flags)
{
	(void) flags;
	return 0;
}

#else

#define SYS_MSG_CONFIRM 2048
#define SYS_MSG_DONTROUTE 4
#define SYS_MSG_DONTWAIT 64
#define SYS_MSG_EOR 128
#define SYS_MSG_MORE 32768
#define SYS_MSG_NOSIGNAL 16384
#define SYS_MSG_OOB 1

enum {
	ALLOWED_SENDTO_FLAGS =
#ifdef MSG_CONFIM
	SYS_MSG_CONFIRM |
#endif
#ifdef MSG_DONTROUTE
	SYS_MSG_DONTROUTE |
#endif
#ifdef MSG_DONTWAIT
	SYS_MSG_DONTWAIT |
#endif
#ifdef MSG_EOR
	SYS_MSG_EOR |
#endif
#ifdef MSG_MORE
	SYS_MSG_MORE |
#endif
#ifdef MSG_NOSIGNAL
	SYS_MSG_NOSIGNAL |
#endif
#ifdef MSG_OOB
	SYS_MSG_OOB |
#endif
	0,
};

static int convert_sendto_flags(int32_t flags)
{
	int oflags = 0;

#define SETF(n)					\
	if (flags & SYS_MSG_ ## n) {		\
		oflags |= MSG_ ## n;		\
	}

#ifdef MSG_CONFIGM
	SETF(CONFIRM);
#endif
	SETF(DONTROUTE);
	SETF(DONTWAIT);
	SETF(EOR);
#ifdef MSG_MORE
	SETF(MORE);
#endif
#ifdef MSG_NOSIGNAL
	SETF(NOSIGNAL);
#endif
	SETF(OOB);

#undef SETF

	return oflags;
}

static int has_bad_sendto_flag(int32_t flags)
{
	return flags & ~(int32_t) ALLOWED_SENDTO_FLAGS;
}


#endif

#ifdef SAME_SOCKADDR

static long finish_sendto(int32_t fd,
			  const void *buf, uint32_t len,
			  int flags,
			  const void *dest_addr, uint32_t addrlen)
{
	return sys_sendto(fd, buf, len, flags, dest_addr, addrlen);
}

#else

static long finish_sendto(int32_t fd,
			  const void *buf, uint32_t len,
			  int flags2,
			  const void *dest_addr, uint32_t addrlen)
{

	struct sockaddr_storage ss;
	size_t ptr_size;

	/* convert dest_addr to form understood by sys_sendto */
	if (read_sockaddr(&ss, &ptr_size, dest_addr, addrlen))
		return -EINVAL;

	return sys_sendto(fd, buf, len, flags2, (void *) &ss, ptr_size);
}

#endif

#ifdef SAME_SOCKADDR

static long finish_recvfrom(int32_t fd,
			    void *buf, uint32_t len,
			    int32_t flags,
			    void *dest_addr,
			    uint32_t addrlen,
			    void *addrlenp)
{
	(void)addrlen;
	return sys_recvfrom(fd, buf, len, flags, dest_addr, addrlenp);
}

#else

#define SYS_MSG_CMSG_CLOEXEC 1073741824
#define SYS_MSG_DONTWAIT 64
#define SYS_MSG_ERRQUEUE 8192
#define SYS_MSG_OOB 1
#define SYS_MSG_PEEK 2
#define SYS_MSG_TRUNC 32
#define SYS_MSG_WAITALL 256

enum {
	ALLOWED_RECVFROM_FLAGS =
#ifdef MSG_CMSG_CLOEXEC
	SYS_MSG_CMSG_CLOEXEC |
#endif
#ifdef MSG_DONTWAIT
	SYS_MSG_DONTWAIT |
#endif
#ifdef MSG_ERRQUEUE
	SYS_MSG_ERRQUEUE |
#endif
#ifdef MSG_OOB
	SYS_MSG_OOB |
#endif
#ifdef MSG_PEEK
	SYS_MSG_PEEK |
#endif
#ifdef MSG_TRUNC
	SYS_MSG_TRUNC |
#endif
#ifdef MSG_WAITALL
	SYS_MSG_WAITALL |
#endif
	0,
};

static int convert_recvfrom_flags(int32_t flags)
{
	int oflags = 0;

#define SETF(n)					\
	if (flags & SYS_MSG_ ## n) {		\
		oflags |= MSG_ ## n;		\
	}

#ifdef MSG_CMSG_CLOEXEC
	SETF(CMSG_CLOEXEC);
#endif
	SETF(DONTWAIT);
#ifdef MSG_ERRQUEUE
	SETF(ERRQUEUE);
#endif
	SETF(OOB);
	SETF(PEEK);
	SETF(TRUNC);
	SETF(WAITALL);

#undef SETF

	return oflags;
}

static long finish_recvfrom(int32_t fd,
			    void *buf, uint32_t len,
			    int32_t flags,
			    void *dest_addr,
			    uint32_t addrlen,
			    void *addrlenp)
{
	struct sockaddr_storage ss;
	socklen_t ssize = sizeof(ss);
	long rret;
	int flags2;

	/* if there are flags we don't understand, then return invalid flag */
	if (flags & ~(int32_t) ALLOWED_RECVFROM_FLAGS)
		return -EINVAL;

	flags2 = convert_recvfrom_flags(flags);

	rret = sys_recvfrom(fd, buf, len, flags2, (void *) &ss, &ssize);
	if (rret < 0)
		return rret;

	if (write_sockaddr(&ss, ssize, dest_addr, addrlen, addrlenp)) {
		/* NB: we have to abort here because we can't undo the sys_accept() */
		wasmjit_emscripten_internal_abort("Failed to convert sockaddr");
	}

	return rret;
}

#endif

struct em_linger {
	int32_t l_onoff, l_linger;
};

struct em_ucred {
	uint32_t pid, uid, gid;
};

struct em_timeval {
	uint32_t tv_sec, tv_usec;
};

struct linux_ucred {
	uint32_t pid;
	uint32_t uid;
	uint32_t gid;
};

COMPILE_TIME_ASSERT(sizeof(struct timeval) == sizeof(long) * 2);
COMPILE_TIME_ASSERT(sizeof(socklen_t) == sizeof(unsigned));

#define SYS_SOL_SOCKET 1
#define SYS_SCM_RIGHTS 1
#define SYS_SCM_CREDENTIALS 2

enum {
	OPT_TYPE_INT,
	OPT_TYPE_LINGER,
	OPT_TYPE_UCRED,
	OPT_TYPE_TIMEVAL,
	OPT_TYPE_STRING,
};

static int convert_sockopt(int32_t level,
			   int32_t optname,
			   int *level2,
			   int *optname2,
			   int *opttype)
{
	switch (level) {
	case SYS_SOL_SOCKET: {
		switch (optname) {
#define SO(name, value, opt_type) case value: *optname2 = SO_ ## name; *opttype = OPT_TYPE_ ## opt_type; break;
#include <wasmjit/emscripten_runtime_sys_so_def.h>
#undef SO
		default: return -1;
		}
		*level2 = SOL_SOCKET;
		break;
	}
	default: return -1;
	}
	return 0;
}

static long finish_setsockopt(int32_t fd,
			      int32_t level,
			      int32_t optname,
			      char *optval,
			      uint32_t optlen)
{
	int level2, optname2, opttype;
	union {
		int int_;
		struct linger linger;
		/* NB: this is a linux-only struct and it's defined using
		   constant bit-widths */
		struct linux_ucred ucred;
		struct timeval timeval;
	} real_optval;
	void *real_optval_p;
	socklen_t real_optlen;

	if (convert_sockopt(level, optname, &level2, &optname2, &opttype))
		return -EINVAL;

	switch (opttype) {
	case OPT_TYPE_INT: {
		int32_t wasm_int_optval;
		if (optlen != sizeof(wasm_int_optval))
			return -EINVAL;
		memcpy(&wasm_int_optval, optval, sizeof(wasm_int_optval));
		wasm_int_optval = int32_t_swap_bytes(wasm_int_optval);
		real_optval.int_ = wasm_int_optval;
		real_optval_p = &real_optval.int_;
		real_optlen = sizeof(real_optval.int_);
		break;
	}
	case OPT_TYPE_LINGER: {
		struct em_linger wasm_linger_optval;
		if (optlen != sizeof(struct em_linger))
			return -EINVAL;
		memcpy(&wasm_linger_optval, optval, sizeof(struct em_linger));
		wasm_linger_optval.l_onoff =
			int32_t_swap_bytes(wasm_linger_optval.l_onoff);
		wasm_linger_optval.l_linger =
			int32_t_swap_bytes(wasm_linger_optval.l_linger);
		real_optval.linger.l_onoff = wasm_linger_optval.l_onoff;
		real_optval.linger.l_linger = wasm_linger_optval.l_linger;
		real_optval_p = &real_optval.linger;
		real_optlen = sizeof(real_optval.linger);
		break;
	}
	case OPT_TYPE_UCRED: {
		struct em_ucred wasm_ucred_optval;
		if (optlen != sizeof(struct em_ucred))
			return -EINVAL;
		memcpy(&wasm_ucred_optval, optval, sizeof(struct em_ucred));
		real_optval.ucred.pid = uint32_t_swap_bytes(wasm_ucred_optval.pid);
		real_optval.ucred.uid = uint32_t_swap_bytes(wasm_ucred_optval.uid);
		real_optval.ucred.gid = uint32_t_swap_bytes(wasm_ucred_optval.gid);
		real_optval_p = &real_optval.ucred;
		real_optlen = sizeof(real_optval.ucred);
		break;
	}
	case OPT_TYPE_TIMEVAL: {
		struct em_timeval wasm_timeval_optval;
		if (optlen != sizeof(struct em_timeval))
			return -EINVAL;
		memcpy(&wasm_timeval_optval, optval, sizeof(struct em_timeval));
		wasm_timeval_optval.tv_sec =
			uint32_t_swap_bytes(wasm_timeval_optval.tv_sec);
		wasm_timeval_optval.tv_usec =
			uint32_t_swap_bytes(wasm_timeval_optval.tv_usec);
#if 32 > __LONG_WIDTH__
		if (wasm_timeval_optval.tv_sec > LONG_MAX ||
		    wasm_timeval_optval.tv_sec < LONG_MIN ||
		    wasm_timeval_optval.tv_usec > LONG_MAX ||
		    wasm_timeval_optval.tv_usec < LONG_MIN)
			return -EINVAL;
#endif
		real_optval.timeval.tv_sec = wasm_timeval_optval.tv_sec;
		real_optval.timeval.tv_usec = wasm_timeval_optval.tv_usec;
		real_optval_p = &real_optval.timeval;
		real_optlen = sizeof(real_optval.timeval);
		break;
	}
	case OPT_TYPE_STRING: {
		real_optval_p = optval;
		assert(sizeof(real_optlen) >= sizeof(optlen));
		real_optlen = optlen;
		break;
	}
	default: assert(0); __builtin_unreachable();
	}

	return sys_setsockopt(fd, level2, optname2, real_optval_p, real_optlen);
}

static long finish_getsockopt(int32_t fd,
			      int32_t level,
			      int32_t optname,
			      char *optval,
			      uint32_t optlen,
			      char *optlenp)
{
	int level2, optname2, opttype;
	union {
		int int_;
		struct linger linger;
		/* NB: this is a linux-only struct and it's defined using
		   constant bit-widths */
		struct linux_ucred ucred;
		struct timeval timeval;
	} real_optval;
	void *real_optval_p;
	socklen_t real_optlen;
	long ret;
	uint32_t newlen;

	if (convert_sockopt(level, optname, &level2, &optname2, &opttype))
		return -EINVAL;

	switch (opttype) {
	case OPT_TYPE_INT: {
		if (optlen < sizeof(int32_t))
			return -EINVAL;
		real_optval_p = &real_optval.int_;
		real_optlen = sizeof(real_optval.int_);
		break;
	}
	case OPT_TYPE_LINGER: {
		if (optlen < sizeof(struct em_linger))
			return -EINVAL;
		real_optval_p = &real_optval.linger;
		real_optlen = sizeof(real_optval.linger);
		break;
	}
	case OPT_TYPE_UCRED: {
		if (optlen < sizeof(struct em_ucred))
			return -EINVAL;
		real_optval_p = &real_optval.ucred;
		real_optlen = sizeof(real_optval.ucred);
		break;
	}
	case OPT_TYPE_TIMEVAL: {
		if (optlen < sizeof(struct em_timeval))
			return -EINVAL;
		real_optval_p = &real_optval.timeval;
		real_optlen = sizeof(real_optval.timeval);
		break;
	}
	case OPT_TYPE_STRING: {
		real_optval_p = optval;
		real_optlen = optlen;
		break;
	}
	default: assert(0); __builtin_unreachable();
	}

	ret = sys_getsockopt(fd, level2, optname2, real_optval_p, &real_optlen);
	if (ret < 0)
		return ret;

	switch (opttype) {
	case OPT_TYPE_INT: {
		int32_t v;
#if __INT_WIDTH__ > 32
		if (real_optval.int_ > INT32_MAX ||
		    real_optval.int_ < INT32_MIN)
			wasmjit_emscripten_internal_abort("Failed to convert sockopt");
#endif
		v = int32_t_swap_bytes((int32_t) real_optval.int_);
		memcpy(optval, &v, sizeof(v));
		newlen = sizeof(v);
		break;
	}
	case OPT_TYPE_LINGER: {
		struct em_linger v;
#if __INT_WIDTH__ > 32
		if (real_optval.linger.l_onoff > INT32_MAX ||
		    real_optval.linger.l_onoff < INT32_MIN ||
		    real_optval.linger.l_linger > INT32_MAX ||
		    real_optval.linger.l_linger < INT32_MIN)
			wasmjit_emscripten_internal_abort("Failed to convert sockopt");
#endif
		v.l_onoff = int32_t_swap_bytes((int32_t) real_optval.linger.l_onoff);
		v.l_linger = int32_t_swap_bytes((int32_t) real_optval.linger.l_linger);
		memcpy(optval, &v, sizeof(v));
		newlen = sizeof(v);
		break;
	}
	case OPT_TYPE_UCRED: {
		struct em_ucred v;
		v.pid = uint32_t_swap_bytes(real_optval.ucred.pid);
		v.uid = uint32_t_swap_bytes(real_optval.ucred.uid);
		v.gid = uint32_t_swap_bytes(real_optval.ucred.gid);
		memcpy(optval, &v, sizeof(v));
		newlen = sizeof(v);
		break;
	}
	case OPT_TYPE_TIMEVAL: {
		struct em_timeval v;
#if __LONG_WIDTH__ > 32
		if (real_optval.timeval.tv_sec > INT32_MAX ||
		    real_optval.timeval.tv_sec < INT32_MIN ||
		    real_optval.timeval.tv_usec > INT32_MAX ||
		    real_optval.timeval.tv_usec < INT32_MIN)
			wasmjit_emscripten_internal_abort("Failed to convert sockopt");
#endif
		v.tv_sec = uint32_t_swap_bytes((uint32_t) real_optval.timeval.tv_sec);
		v.tv_usec = uint32_t_swap_bytes((uint32_t) real_optval.timeval.tv_usec);
		memcpy(optval, &v, sizeof(v));
		newlen = sizeof(v);
		break;
	}
	case OPT_TYPE_STRING: {
#if __INT_WIDTH__ > 32
		if (real_optlen > UINT32_MAX)
			wasmjit_emscripten_internal_abort("Failed to convert sockopt");
#endif
		newlen = real_optlen;
		break;
	}
	default: assert(0); __builtin_unreachable();
	}

	newlen = uint32_t_swap_bytes(newlen);
	memcpy(optlenp, &newlen, sizeof(newlen));

	return 0;
}

struct em_cmsghdr {
	uint32_t cmsg_len;
	uint32_t cmsg_level;
	uint32_t cmsg_type;
};

struct em_msghdr {
	uint32_t msg_name;
	uint32_t msg_namelen;
	uint32_t msg_iov;
	uint32_t msg_iovlen;
	uint32_t msg_control;
	uint32_t msg_controllen;
	uint32_t msg_flags;
};

#define SYS_CMSG_ALIGN(len) (((len) + sizeof (uint32_t) - 1)		\
			     & (uint32_t) ~(sizeof (uint32_t) - 1))

/* NB: cmsg_len can be size_t or socklen_t depending on host kernel */
typedef typeof(((struct cmsghdr *)0)->cmsg_len) cmsg_len_t;

static long copy_cmsg(struct FuncInst *funcinst,
		      uint32_t control,
		      uint32_t controllen,
		      struct msghdr *msg)
{
	char *base;
	uint32_t controlptr;
	uint32_t controlmax;
	cmsg_len_t buf_offset;
	struct cmsghdr *cmsg;

	base = wasmjit_emscripten_get_base_address(funcinst);

	/* control and controllen are user-controlled,
	   check for overflow */
	if (__builtin_add_overflow(control, controllen, &controlmax))
		return -EFAULT;

	if (controlmax < SYS_CMSG_ALIGN(sizeof(struct em_cmsghdr)))
		return -EINVAL;

	/* count up required space */
	buf_offset = 0;
	controlptr = control;
	while (!(controlptr > controlmax - SYS_CMSG_ALIGN(sizeof(struct em_cmsghdr)))) {
		struct em_cmsghdr user_cmsghdr;
		size_t cur_len, buf_len;

		if (_wasmjit_emscripten_copy_from_user(funcinst,
						       &user_cmsghdr,
						       controlptr,
						       sizeof(user_cmsghdr)))
			return -EFAULT;

		user_cmsghdr.cmsg_len = uint32_t_swap_bytes(user_cmsghdr.cmsg_len);
		user_cmsghdr.cmsg_level = uint32_t_swap_bytes(user_cmsghdr.cmsg_level);
		user_cmsghdr.cmsg_type = uint32_t_swap_bytes(user_cmsghdr.cmsg_type);

		/* kernel says must check this */
		{
			uint32_t sum;
			/* controlptr and cmsg_len are user-controlled,
			   check for overflow */
			if (__builtin_add_overflow(SYS_CMSG_ALIGN(user_cmsghdr.cmsg_len),
						   controlptr,
						   &sum))
				return -EFAULT;

			if (sum > controlmax)
				break;
		}

		/* check if control data is in range */
		if (!_wasmjit_emscripten_check_range(funcinst,
						     controlptr,
						     SYS_CMSG_ALIGN(user_cmsghdr.cmsg_len)))
			return -EFAULT;

		if (user_cmsghdr.cmsg_len < SYS_CMSG_ALIGN(sizeof(struct em_cmsghdr)))
			return -EFAULT;
		buf_len = user_cmsghdr.cmsg_len - SYS_CMSG_ALIGN(sizeof(struct em_cmsghdr));

		switch (user_cmsghdr.cmsg_level) {
		case SYS_SOL_SOCKET: {
			switch (user_cmsghdr.cmsg_type) {
			case SYS_SCM_RIGHTS:
				/* convert int size from wasm to host */
				if (buf_len % sizeof(int32_t))
					return -EINVAL;
				cur_len = (buf_len / sizeof(int32_t)) * sizeof(int);
				break;
			case SYS_SCM_CREDENTIALS:
#ifdef SCM_CREDENTIALS
				/* passes a struct ucred which is the same across
				   all archs */
				if (buf_len != sizeof(struct linux_ucred))
					return -EINVAL;
				cur_len = buf_len;
				break;
#else
				/* TODO: convert to host's version of SCM_CREDENTIALS */
				return -EFAULT;
#endif
			default:
				return -EFAULT;
			}
			break;
		}
		default:
			return -EFAULT;
		}

		/* it's not exactly clear that this can't overflow at
		   this point given the varying conditions in which we
		   operate (e.g. sizeof(cmsg_len_t) depends on
		   architecture and host kernel)
		 */
		if (__builtin_add_overflow(buf_offset, CMSG_SPACE(cur_len),
					   &buf_offset))
			return -EFAULT;
		/* the safety of this was checked above */
		controlptr += SYS_CMSG_ALIGN(user_cmsghdr.cmsg_len);
	}

	msg->msg_control = calloc(buf_offset, 1);
	if (!msg->msg_control)
		return -ENOMEM;

	msg->msg_controllen = buf_offset;

	cmsg = CMSG_FIRSTHDR(msg);

	/* now convert each control message */
	controlptr = control;
	while (!(controlptr > controlmax - SYS_CMSG_ALIGN(sizeof(struct em_cmsghdr)))) {
		struct em_cmsghdr user_cmsghdr;
		size_t cur_len, buf_len, new_len;
		char *src_buf_base;
		unsigned char *dest_buf_base;

		assert(cmsg);

		memcpy(&user_cmsghdr, base + controlptr, sizeof(struct em_cmsghdr));
		user_cmsghdr.cmsg_len = uint32_t_swap_bytes(user_cmsghdr.cmsg_len);
		user_cmsghdr.cmsg_level = uint32_t_swap_bytes(user_cmsghdr.cmsg_level);
		user_cmsghdr.cmsg_type = uint32_t_swap_bytes(user_cmsghdr.cmsg_type);

		/* kernel says must check this */
		if (SYS_CMSG_ALIGN(user_cmsghdr.cmsg_len) + controlptr > controlmax)
			break;

		cur_len = SYS_CMSG_ALIGN(sizeof(struct em_cmsghdr));
		buf_len = user_cmsghdr.cmsg_len - cur_len;

		/* kernel sources differ from libc sources on where
		   the buffer starts, but in any case the correct code
		   is the aligned offset from the beginning,
		   i.e. what (struct cmsghdr *)a + 1 means */
		src_buf_base = base + controlptr + cur_len;
		dest_buf_base = CMSG_DATA(cmsg);

		switch (user_cmsghdr.cmsg_level) {
		case SYS_SOL_SOCKET: {
			switch (user_cmsghdr.cmsg_type) {
			case SYS_SCM_RIGHTS: {
				size_t i;
				for (i = 0; i < buf_len / sizeof(int32_t); ++i) {
					int32_t fd;
					int destfd;
					memcpy(&fd, src_buf_base + i * sizeof(int32_t),
					       sizeof(int32_t));
					fd = int32_t_swap_bytes(fd);
					destfd = fd;
					memcpy(dest_buf_base + i * sizeof(int),
					       &destfd, sizeof(int));
				}

				new_len = (buf_len / sizeof(int32_t)) * sizeof(int);
				cmsg->cmsg_type = SCM_RIGHTS;
				break;
				}
#ifdef SCM_CREDENTIALS
			case SYS_SCM_CREDENTIALS: {
				/* struct ucred is same across all archs,
				   just flip bytes if necessary
				 */
				size_t i;
				for (i = 0; i < 3; ++i) {
					uint32_t tmp;
					memcpy(&tmp, src_buf_base + i * 4, sizeof(tmp));
					tmp = uint32_t_swap_bytes(tmp);
					memcpy(dest_buf_base + i * 4, &tmp, sizeof(tmp));
				}

				new_len = buf_len;
				cmsg->cmsg_type = SCM_CREDENTIALS;
				break;
			}
#endif
			default:
				assert(0);
				__builtin_unreachable();
				break;

			}
			cmsg->cmsg_level = SOL_SOCKET;
			break;
		}
		default:
			assert(0);
			__builtin_unreachable();
			break;
		}

		cmsg->cmsg_len = CMSG_LEN(new_len);

		cmsg = CMSG_NXTHDR(msg, cmsg);
		controlptr += SYS_CMSG_ALIGN(user_cmsghdr.cmsg_len);
	}

	return 0;
}

#ifdef SAME_SOCKADDR

static long finish_sendmsg(int fd, struct msghdr *msg, int flags)
{
	return sys_sendmsg(fd, msg, flags);
}

#else

static long finish_sendmsg(int fd, struct msghdr *msg, int flags)
{
	struct sockaddr_storage ss;
	size_t ptr_size;

	/* convert msg_name to form understood by sys_sendmsg */
	if (msg->msg_name) {
		if (read_sockaddr(&ss, &ptr_size, msg->msg_name, msg->msg_namelen))
			return -EINVAL;
		msg->msg_name = (void *)&ss;
		msg->msg_namelen = ptr_size;
	}

	return sys_sendmsg(fd, msg, flags);
}

#endif

uint32_t wasmjit_emscripten____syscall102(uint32_t which, uint32_t varargs,
					  struct FuncInst *funcinst)
{
	long ret;
	uint32_t ivargs, icall;
	LOAD_ARGS(funcinst, varargs, 2,
		  uint32_t, call,
		  uint32_t, varargs);

	(void) which;
	ivargs = args.varargs;
	icall = args.call;

	switch (icall) {
	case 1: { // socket
		int domain, type, protocol;

		LOAD_ARGS(funcinst, ivargs, 3,
			  int32_t, domain,
			  int32_t, type,
			  int32_t, protocol);

		domain = convert_socket_domain_to_local(args.domain);
		type = convert_socket_type_to_local(args.type);
		protocol = convert_proto_to_local(domain, args.protocol);

		ret = sys_socket(domain, type, protocol);
		break;
	}
	case 2: // bind
	case 3: { // connect
		char *base;

		LOAD_ARGS(funcinst, ivargs, 3,
			  int32_t, fd,
			  uint32_t, addrp,
			  uint32_t, addrlen);

		if (!_wasmjit_emscripten_check_range(funcinst,
						     args.addrp,
						     args.addrlen))
			return -SYS_EFAULT;

		base = wasmjit_emscripten_get_base_address(funcinst);

		if (icall == 2) {
			ret = finish_bindlike(&sys_bind,
					      args.fd,
					      base + args.addrp, args.addrlen);
		} else {
			assert(icall == 3);
			ret = finish_bindlike(&sys_connect,
					      args.fd,
					      base + args.addrp, args.addrlen);
		}
		break;
	}
	case 4: { // listen
		LOAD_ARGS(funcinst, ivargs, 2,
			  int32_t, fd,
			  int32_t, backlog);

		ret = sys_listen(args.fd, args.backlog);
		break;
	}
	case 5: // accept
	case 6: // getsockname
	case 7: { // getpeername
		char *base;
		uint32_t addrlen;

		LOAD_ARGS(funcinst, ivargs, 3,
			  int32_t, fd,
			  uint32_t, addrp,
			  uint32_t, addrlenp);

		if (_wasmjit_emscripten_copy_from_user(funcinst,
						       &addrlen,
						       args.addrlenp,
						       sizeof(addrlen)))
			return -SYS_EFAULT;

		addrlen = uint32_t_swap_bytes(addrlen);

		if (!_wasmjit_emscripten_check_range(funcinst,
						     args.addrp,
						     addrlen))
			return -SYS_EFAULT;

		base = wasmjit_emscripten_get_base_address(funcinst);

		switch (icall) {
		case 5:
			ret = finish_acceptlike(&sys_accept,
						args.fd, base + args.addrp,
						addrlen,
						base + args.addrlenp);
			break;
		case 6:
			ret = finish_acceptlike(&sys_getsockname,
						args.fd, base + args.addrp,
						addrlen,
						base + args.addrlenp);
			break;
		case 7:
			ret = finish_acceptlike(&sys_getpeername,
						args.fd, base + args.addrp,
						addrlen,
						base + args.addrlenp);
			break;
		default:
			assert(0);
			__builtin_unreachable();
		}

		break;
	}
	case 11: { // sendto
		char *base;
		int flags2;

		LOAD_ARGS(funcinst, ivargs, 6,
			  int32_t, fd,
			  uint32_t, message,
			  uint32_t, length,
			  int32_t, flags,
			  uint32_t, addrp,
			  uint32_t, addrlen);

		if (!_wasmjit_emscripten_check_range(funcinst,
						     args.message,
						     args.length))
			return -SYS_EFAULT;

		if (!_wasmjit_emscripten_check_range(funcinst,
						     args.addrp,
						     args.addrlen))
			return -SYS_EFAULT;

		base = wasmjit_emscripten_get_base_address(funcinst);

		/* if there are flags we don't understand, then return invalid flag */
		if (has_bad_sendto_flag(args.flags))
			return -SYS_EINVAL;

		flags2 = convert_sendto_flags(args.flags);

		ret = finish_sendto(args.fd,
				    base + args.message,
				    args.length,
				    flags2,
				    base + args.addrp,
				    args.addrlen);
		break;
	}
	case 12: { // recvfrom
		char *base;
		uint32_t addrlen;

		LOAD_ARGS(funcinst, ivargs, 6,
			  int32_t, fd,
			  uint32_t, buf,
			  uint32_t, len,
			  uint32_t, flags,
			  uint32_t, addrp,
			  uint32_t, addrlenp);

		if (_wasmjit_emscripten_copy_from_user(funcinst,
						       &addrlen,
						       args.addrlenp,
						       sizeof(uint32_t)))
			return -SYS_EFAULT;

		addrlen = uint32_t_swap_bytes(addrlen);

		if (!_wasmjit_emscripten_check_range(funcinst,
						     args.addrp,
						     addrlen))
			return -SYS_EFAULT;

		if (!_wasmjit_emscripten_check_range(funcinst,
						     args.buf,
						     args.len))
			return -SYS_EFAULT;

		base = wasmjit_emscripten_get_base_address(funcinst);

		ret = finish_recvfrom(args.fd,
				      base + args.buf,
				      args.len,
				      args.flags,
				      base + args.addrp,
				      addrlen,
				      base + args.addrlenp);
		break;
	}
	case 14: { // setsockopt
		char *base;

		LOAD_ARGS(funcinst, ivargs, 5,
			  int32_t, fd,
			  int32_t, level,
			  int32_t, optname,
			  uint32_t, optval,
			  uint32_t, optlen);

		if (!_wasmjit_emscripten_check_range(funcinst,
						     args.optval,
						     args.optlen))
			return -SYS_EFAULT;

		base = wasmjit_emscripten_get_base_address(funcinst);

		ret = finish_setsockopt(args.fd,
					args.level,
					args.optname,
					base + args.optval,
					args.optlen);
		break;
	}
	case 15: { // getsockopt
		char *base;
		uint32_t optlen;

		LOAD_ARGS(funcinst, ivargs, 5,
			  int32_t, fd,
			  int32_t, level,
			  int32_t, optname,
			  uint32_t, optval,
			  uint32_t, optlenp);

		if (_wasmjit_emscripten_copy_from_user(funcinst,
						       &optlen,
						       args.optlenp,
						       sizeof(optlen)))
			return -SYS_EFAULT;

		optlen = uint32_t_swap_bytes(optlen);

		if (!_wasmjit_emscripten_check_range(funcinst,
						     args.optval,
						     optlen))
			return -SYS_EFAULT;

		base = wasmjit_emscripten_get_base_address(funcinst);

		ret = finish_getsockopt(args.fd,
					args.level,
					args.optname,
					base + args.optval,
					optlen,
					base + args.optlenp);
		break;
	}
	case 16: { // sendmsg
		LOAD_ARGS(funcinst, ivargs, 3,
			  int32_t, fd,
			  uint32_t, msg,
			  int32_t, flags);

		if (has_bad_sendto_flag(args.flags))
			return -SYS_EINVAL;

		{
			char *base;
			struct msghdr msg;
			msg.msg_iov = NULL;
			msg.msg_control = NULL;

			LOAD_ARGS_CUSTOM(emmsg, funcinst, args.msg, 7,
					 uint32_t, name,
					 uint32_t, namelen,
					 uint32_t, iov,
					 uint32_t, iovlen,
					 uint32_t, control,
					 uint32_t, controllen,
					 uint32_t, flags);

			base = wasmjit_emscripten_get_base_address(funcinst);

			if (emmsg.name) {
				if (!_wasmjit_emscripten_check_range(funcinst,
								     emmsg.name,
								     emmsg.namelen)) {
					ret = -EFAULT;
					goto error;
				}

				msg.msg_name = base + emmsg.name;
				msg.msg_namelen = emmsg.namelen;
			} else {
				if (emmsg.namelen) {
					ret = -EINVAL;
					goto error;
				}
				msg.msg_name = NULL;
				msg.msg_namelen = 0;
			}

			ret = copy_iov(funcinst, emmsg.iov, emmsg.iovlen, &msg.msg_iov);
			if (ret) {
				goto error;
			}
			msg.msg_iovlen = emmsg.iovlen;

			if (emmsg.control) {
				ret = copy_cmsg(funcinst, emmsg.control, emmsg.controllen,
						&msg);
				if (ret)
					goto error;
			} else {
				if (emmsg.controllen) {
					ret = -EINVAL;
					goto error;
				}
				msg.msg_control = NULL;
				msg.msg_controllen = 0;
			}

			/* unused in sendmsg */
			msg.msg_flags = 0;

			ret = finish_sendmsg(args.fd, &msg,
					     convert_sendto_flags(args.flags));

		error:
			free(msg.msg_control);
			free(msg.msg_iov);
		}

		break;
	}
	case 17: { // recvmsg
	}
		ret = -EINVAL;
		break;
	default: {
		char buf[64];
		snprintf(buf, sizeof(buf),
			 "unsupported socketcall syscall %d\n", args.call);
		wasmjit_emscripten_internal_abort(buf);
		ret = -EINVAL;
		break;
	}
	}
	return check_ret(ret);
}

/* fcntl64 */
uint32_t wasmjit_emscripten____syscall221(uint32_t which, uint32_t varargs,
					  struct FuncInst *funcinst)
{
	LOAD_ARGS(funcinst, varargs, 2,
		  int32_t, fd,
		  int32_t, cmd);

	(void) which;

	/* TODO: implement */
	(void) args;

	return -SYS_EINVAL;
}

void wasmjit_emscripten_cleanup(struct ModuleInst *moduleinst) {
	(void)moduleinst;
	/* TODO: implement */
}

struct EmscriptenContext *wasmjit_emscripten_get_context(struct ModuleInst *module_inst)
{
	return module_inst->private_data;
}

#define alignMemory(size, factor) \
	(((size) % (factor)) ? ((size) - ((size) % (factor)) + (factor)) : (size))

void wasmjit_emscripten_start_func(struct FuncInst *funcinst)
{
	struct MemInst *meminst;
	struct GlobalInst *DYNAMICTOP_PTR, *STACK_MAX;
	uint32_t DYNAMIC_BASE;
	uint32_t copy_user_res;

	/* fill DYNAMIC_BASE */

	if (funcinst->module_inst->mems.n_elts < 1)
		wasmjit_emscripten_internal_abort("no memory");

	meminst = funcinst->module_inst->mems.elts[0];
	assert(meminst);

	DYNAMICTOP_PTR =
		wasmjit_get_export(funcinst->module_inst, "DYNAMICTOP_PTR",
				   IMPORT_DESC_TYPE_GLOBAL).global;
	assert(DYNAMICTOP_PTR && DYNAMICTOP_PTR->value.type == VALTYPE_I32);

	STACK_MAX =
		wasmjit_get_export(funcinst->module_inst, "STACK_MAX",
				   IMPORT_DESC_TYPE_GLOBAL).global;
	assert(STACK_MAX && STACK_MAX->value.type == VALTYPE_I32);

	DYNAMIC_BASE = alignMemory(STACK_MAX->value.data.i32,  16);

	DYNAMIC_BASE = uint32_t_swap_bytes(DYNAMIC_BASE);
	copy_user_res = wasmjit_emscripten_copy_to_user(meminst,
							DYNAMICTOP_PTR->value.data.i32,
							&DYNAMIC_BASE,
							sizeof(DYNAMIC_BASE));
	(void)copy_user_res;
	assert(!copy_user_res);
}

void wasmjit_emscripten_derive_memory_globals(uint32_t static_bump,
					      struct WasmJITEmscriptenMemoryGlobals *out)
{

#define staticAlloc(_top , s)                           \
	(((_top) + (s) + 15) & ((uint32_t) -16))

#define TOTAL_STACK 5242880
#define STACK_ALIGN_V 16
#define GLOBAL_BASE 1024
#define STATIC_BASE GLOBAL_BASE
#define STATICTOP (STATIC_BASE + static_bump)

#define tempDoublePtr_V (staticAlloc(STATICTOP, 16))
#define DYNAMICTOP_PTR_V (staticAlloc(tempDoublePtr_V, 4))
#define STACKTOP_V (alignMemory(DYNAMICTOP_PTR_V, STACK_ALIGN_V))
#define STACK_BASE_V (STACKTOP_V)
#define STACK_MAX_V (STACK_BASE_V + TOTAL_STACK)

	out->memoryBase = STATIC_BASE;
	out->DYNAMICTOP_PTR = DYNAMICTOP_PTR_V;
	out->tempDoublePtr = tempDoublePtr_V;
	out->STACKTOP =  STACKTOP_V;
	out->STACK_MAX = STACK_MAX_V;
}
