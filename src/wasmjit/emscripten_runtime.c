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

#ifdef __KERNEL__
#include <linux/socket.h>
#include <linux/net.h>
#include <uapi/linux/in.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>
#endif

#define __MMAP0(m,...)
#define __MMAP1(m,t,a,...) m(t, a)
#define __MMAP2(m,t,a,...) m(t, a) __MMAP1(m,__VA_ARGS__)
#define __MMAP3(m,t,a,...) m(t, a) __MMAP2(m,__VA_ARGS__)
#define __MMAP4(m,t,a,...) m(t, a) __MMAP3(m,__VA_ARGS__)
#define __MMAP5(m,t,a,...) m(t, a) __MMAP4(m,__VA_ARGS__)
#define __MMAP6(m,t,a,...) m(t, a) __MMAP5(m,__VA_ARGS__)
#define __MMAP(n,...) __MMAP##n(__VA_ARGS__)

#define __DECL(t, a) t a;
#define __SWAP(t, a) args.a = t ## _swap_bytes(args.a);

static int32_t int32_t_swap_bytes(int32_t a)
{
	return uint32_t_swap_bytes(a);
}

#define LOAD_ARGS(funcinst, varargs, n, ...)				\
	struct {							\
		__MMAP(n, __DECL, __VA_ARGS__)				\
	} args;								\
	if (_wasmjit_emscripten_copy_from_user(funcinst,		\
					       &args, varargs,		\
					       sizeof(args)))		\
		return -SYS_EFAULT;					\
	__MMAP(n, __SWAP, __VA_ARGS__)

enum {
#define ERRNO(name, value) SYS_ ## name = value,
#include <wasmjit/emscripten_runtime_sys_errno_def.h>
#undef ERRNO
};

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
			    struct FuncInst *environ_constructor,
			    struct FuncInst *malloc_inst,
			    struct FuncInst *free_inst,
			    char **envp)
{
	int ret;

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

	if (ret)
		return ret;

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
	    !wasmjit_invoke_function(ctx->errno_location_inst, NULL, &out) &&
	    !_wasmjit_emscripten_copy_to_user(funcinst, out.i32, &value, sizeof(value)))
			return;
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

/* writev */
uint32_t wasmjit_emscripten____syscall146(uint32_t which, uint32_t varargs, struct FuncInst *funcinst)
{
	uint32_t i;
	char *base;
	long rret;
	struct iovec *liov;

	LOAD_ARGS(funcinst, varargs, 3,
		  int32_t, fd,
		  uint32_t, iov,
		  uint32_t, iovcnt);

	(void)which;
	base = wasmjit_emscripten_get_base_address(funcinst);

	/* TODO: do UIO_FASTIOV stack optimization */
	liov = wasmjit_alloc_vector(args.iovcnt,
				    sizeof(struct iovec), NULL);
	if (!liov) {
		return -SYS_ENOMEM;
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

		iov.iov_base = uint32_t_swap_bytes(iov.iov_base);
		iov.iov_len = uint32_t_swap_bytes(iov.iov_len);

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

		freeMemory(ctx, envPtr);

		if (_wasmjit_emscripten_copy_from_user(funcinst,
						       &poolPtr,
						       envPtr,
						       sizeof(poolPtr)))
			wasmjit_trap(WASMJIT_TRAP_MEMORY_OVERFLOW);

		freeMemory(ctx, poolPtr);
	}

	n_envs = 0;
	total_pool_size = 0;
	for (env = ctx->environ; *env; ++env, ++i) {
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

	if (_wasmjit_emscripten_copy_to_user(funcinst,
					     environ_arg,
					     &envPtr,
					     sizeof(envPtr)))
		wasmjit_trap(WASMJIT_TRAP_MEMORY_OVERFLOW);

	for (env = ctx->environ, i = 0; *env; ++env, ++i) {
		size_t len = strlen(*env);
		/* NB: these memcpys are checked above */
		memcpy(base + poolPtr, *env, len + 1);
		memcpy(base + envPtr + i * sizeof(uint32_t),
		       &poolPtr, sizeof(poolPtr));
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

#if !defined(__INT_WIDTH__) && defined(__LP64__)
#define __INT_WIDTH__ 32
#endif

#if __INT_WIDTH__ < 32
#error This runtime requires at least 32-bit ints
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
			return -SYS_EINVAL;
		return 0;
	}
}

#endif

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__ && (defined(__linux__) || defined(__KERNEL__))

static long finish_bind(int fd, void *addr, size_t len)
{
	return sys_bind(fd, addr, len);
}

#else

/* need to byte swap and adapt sockaddr to current platform */
static long finish_bind(int fd, char *addr, size_t len)
{
	uint16_t family;
	union {
		struct sockaddr_un un;
		struct sockaddr_in in;
		struct sockaddr_in6 in6;
	} sa;
	void *ptr;
	size_t ptr_size;

#define FAS 2
	assert(sizeof(family) == FAS);

	if (len < FAS)
		return -SYS_EINVAL;

	memcpy(&family, addr, sizeof(family));

	switch (family) {
	case SYS_AF_UNIX: {
		struct em_sockaddr_un {
			uint16_t sun_family;
			char buf[108];
		};

		sa.un.sun_family = AF_UNIX;
		memcpy(sa.un.sun_path, addr + FAS, len - FAS);
		ptr = &sa.un;
		ptr_size = len;
		break;
	}
	case SYS_AF_INET: {
		if (len < 8)
			return -SYS_EINVAL;
		memset(&sa.in, 0, sizeof(struct sockaddr_in));
		sa.in.sin_family = AF_INET;
		/* these are in network order so they don't need to be swapped */
		memcpy(&sa.in.sin_port, addr + 2, 2);
		memcpy(&sa.in.sin_addr, addr + 4, 4);
		ptr = &sa.in;
		ptr_size = sizeof(struct sockaddr_in);
		break;
	}
	case SYS_AF_INET6: {
		if (len < 28)
			return -SYS_EINVAL;

		memset(&sa.in6, 0, sizeof(struct sockaddr_in6));

		sa.in6.sin6_family = AF_INET6;

		/* these are in network order so they don't need to be swapped */
		memcpy(&sa.in6.sin6_port, addr + 2, 2);
		memcpy(&sa.in6.sin6_addr, addr + 8, 16);

		memcpy(&sa.in6.sin6_flowinfo, addr + 4, 4);
		sa.in6.sin6_flowinfo = uint32_t_swap_bytes(sa.in6.sin6_flowinfo);
		memcpy(&sa.in6.sin6_scope_id, addr + 24, 4);
		sa.in6.sin6_scope_id = uint32_t_swap_bytes(sa.in6.sin6_scope_id);

		ptr = &sa.in6;
		ptr_size = sizeof(struct sockaddr_in6);
		break;
	}
	default: {
		/* TODO: add more support */
		return -SYS_EINVAL;
		break;
	}
	}

#undef FAS

	return sys_bind(fd, ptr, ptr_size);
}

#endif

uint32_t wasmjit_emscripten____syscall102(uint32_t which, uint32_t varargs,
					  struct FuncInst *funcinst)
{
	long ret;
	uint32_t ivargs;
	LOAD_ARGS(funcinst, varargs, 2,
		  uint32_t, call,
		  uint32_t, varargs);

	(void) which;
	ivargs = args.varargs;

	switch (args.call) {
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
	case 2: { // bind
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

		ret = finish_bind(args.fd,
				  base + args.addrp, args.addrlen);
		break;
	}
	case 3: { // connect
	}
	case 4: { // listen
	}
	case 5: { // accept
	}
	case 6: { // getsockname
	}
	case 7: { // getpeername
	}
	case 11: { // sendto
	}
	case 12: { // recvfrom
	}
	case 14: { // setsockopt
	}
	case 15: { // getsockopt
	}
	case 16: { // sendmsg
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
	struct {
		int32_t fd, cmd;
	} args;

	(void) which;

	if (_wasmjit_emscripten_copy_from_user(funcinst,
					       &args,
					       varargs,
					       sizeof(args)))
		return -SYS_EINVAL;

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

	copy_user_res = wasmjit_emscripten_copy_to_user(meminst,
							DYNAMICTOP_PTR->value.data.i32,
							&DYNAMIC_BASE,
							sizeof(DYNAMIC_BASE));
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
