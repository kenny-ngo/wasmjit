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

#include <wasmjit/ast.h>
#include <wasmjit/runtime.h>
#include <wasmjit/util.h>
#include <wasmjit/sys.h>
#include <wasmjit/ktls.h>

#include <linux/uio.h>
#include <linux/sched.h>
#include <linux/kallsyms.h>

#define __KMAP0(m,...)
#define __KMAP1(m,t,...) m(t,_1)
#define __KMAP2(m,t,...) m(t,_2), __KMAP1(m,__VA_ARGS__)
#define __KMAP3(m,t,...) m(t,_3), __KMAP2(m,__VA_ARGS__)
#define __KMAP4(m,t,...) m(t,_4), __KMAP3(m,__VA_ARGS__)
#define __KMAP5(m,t,...) m(t,_5), __KMAP4(m,__VA_ARGS__)
#define __KMAP6(m,t,...) m(t,_6), __KMAP5(m,__VA_ARGS__)
#define __KMAP(n,...) __KMAP##n(__VA_ARGS__)

#define __KT(t, a) t
#define __KA(t, a) a
#define __KDECL(t, a) t a
#define __KINIT(t, a) . t = (unsigned long) a
#define __KSET(t, a) vals-> t = (unsigned long) a

#define KWSC1(name, ...) KWSCx(1, name, __VA_ARGS__)
#define KWSC3(name, ...) KWSCx(3, name, __VA_ARGS__)

#ifdef __x86_64__

#define KWSCx(x, name, ...) long (*name)(struct pt_regs *);

static struct {
#include <wasmjit/linux_syscalls.h>
} sctable;


#undef KWSCx
#define KWSCx(x, name, ...)						\
	static long sys_ ## name(__KMAP(x, __KDECL, __VA_ARGS__))	\
	{								\
		struct pt_regs *vals = &wasmjit_get_ktls()->regs;	\
		__KMAP(x, __KSET, di, si, dx, cx, r8, r9);		\
		return sctable. name (vals);				\
	}

#include <wasmjit/linux_syscalls.h>

#define SCPREFIX "__x64_sys_"

#else

#define KWSCx(x, name, ...) long (*name)(__KMAP(x, __KT, __VA_ARGS__));

static struct {
#include <wasmjit/linux_syscalls.h>
} sctable;

#undef KWSCx
#define KWSCx(x, name, ...)						\
	static long sys_ ## name(__KMAP(x, __KDECL, __VA_ARGS__)) {	\
		return sctable. name (__KMAP(x, __KA, __VA_ARGS__));	\
	}

#include <wasmjit/linux_syscalls.h>

#define SCPREFIX "sys_"

#endif

#undef KWSCx

int wasmjit_emscripten_linux_kernel_init(void) {
#define KWSCx(x, n, ...)					\
	do {							\
		sctable. n = (void *)kallsyms_lookup_name(SCPREFIX #n);	\
		if (!sctable. n)				\
			return 0;				\
	}							\
	while (0);

#include <wasmjit/linux_syscalls.h>

	return 1;
}

__attribute__((noreturn))
static void my_abort(const char *msg)
{
	printk(KERN_NOTICE "kwasmjit abort PID %d: %s", current->pid, msg);
	wasmjit_trap(WASMJIT_TRAP_ABORT);
}

char *wasmjit_emscripten_get_base_address(struct FuncInst *funcinst) {
	return funcinst->module_inst->mems.elts[0]->data;
}

void wasmjit_emscripten_abortStackOverflow(uint32_t allocSize, struct FuncInst *funcinst)
{
	(void)funcinst;
	(void)allocSize;
	my_abort("Stack overflow!");
}

uint32_t wasmjit_emscripten_abortOnCannotGrowMemory(struct FuncInst *funcinst)
{
	(void)funcinst;
	my_abort("Cannot enlarge memory arrays.");
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
	my_abort("Invalid function pointer called with signature 'ii'. Perhaps this is an invalid value (e.g. caused by calling a virtual method on a NULL pointer)? Or calling a function with an incorrect type, which will fail? (it is worth building your source files with -Werror (warnings are errors), as warnings can indicate undefined behavior which can cause this)");
}

void wasmjit_emscripten_nullFunc_iiii(uint32_t x, struct FuncInst *funcinst)
{
	(void)funcinst;
	(void)x;
	my_abort("Invalid function pointer called with signature 'iiii'. Perhaps this is an invalid value (e.g. caused by calling a virtual method on a NULL pointer)? Or calling a function with an incorrect type, which will fail? (it is worth building your source files with -Werror (warnings are errors), as warnings can indicate undefined behavior which can cause this)");

}

void wasmjit_emscripten____lock(uint32_t x, struct FuncInst *funcinst)
{
	(void)funcinst;
	(void)x;
}

void wasmjit_emscripten____setErrNo(uint32_t value, struct FuncInst *funcinst)
{
	(void)funcinst;
	(void)value;
	// TODO: get errno location from memory
	// struct Value value;
	// wasmjit_get_global("env", "___errno_location", &value);
	// store value in errno location
	my_abort("failed to set errno from JS");
}

/*  _llseek */
uint32_t wasmjit_emscripten____syscall140(uint32_t which, uint32_t varargs, struct FuncInst *funcinst)
{
	char *base;
	struct {
		uint32_t fd, offset_high, offset_low,
			result, whence;
	} args;
	long rret;

	(void)which;
	assert(which == 140);

	base = wasmjit_emscripten_get_base_address(funcinst);

	memcpy(&args, base + varargs, sizeof(args));
	// emscripten off_t is 32-bits, offset_high is useless
	assert(!args.offset_high);

	rret = sys_lseek(args.fd, args.offset_low, args.whence);

	if (rret < 0) {
		/* TODO: set errno */
		return -1;
	} else {
		int32_t ret;
		assert(rret <= INT32_MAX && rret >= INT32_MIN);
		ret = rret;
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
	assert(which == 146);
	base = wasmjit_emscripten_get_base_address(funcinst);

	memcpy(&args, base + varargs, sizeof(args));

	/* TODO: do UIO_FASTIOV stack optimization */
	liov = wasmjit_alloc_vector(args.iovcnt,
				    sizeof(struct iovec), NULL);
	if (!liov) {
		/* TODO: set errno */
		return -1;
	}

	for (i = 0; i < args.iovcnt; ++i) {
		struct em_iovec {
			uint32_t iov_base;
			uint32_t iov_len;
		} iov;
		memcpy(&iov, base + args.iov + sizeof(struct em_iovec) * i,
		       sizeof(struct em_iovec));

		liov[i].iov_base = base + iov.iov_base;
		liov[i].iov_len = iov.iov_len;
	}

	rret = sys_writev(args.fd, liov, args.iovcnt);
	free(liov);

	if (rret < 0) {
		/* TODO: set errno */
		return -1;
	} else {
		int32_t ret;
		assert(rret <= INT32_MAX && rret >= INT32_MIN);
		ret = rret;
		return ret;
	}
}

/* write */
uint32_t wasmjit_emscripten____syscall4(uint32_t which, uint32_t varargs, struct FuncInst *funcinst)
{
	char *base;
	struct {
		uint32_t fd, buf, count;
	} args;
	long rret;

	(void)which;
	assert(which == 4);
	base = wasmjit_emscripten_get_base_address(funcinst);

	memcpy(&args, base + varargs, sizeof(args));

	rret = sys_write(args.fd, base + args.buf, args.count);
	if (rret < 0) {
		/* TODO: set errno */
		return -1;
	} else {
		int32_t ret;
		assert(rret <= INT32_MAX && rret >= INT32_MIN);
		ret = rret;
		return ret;
	}
}

/* ioctl */
uint32_t wasmjit_emscripten____syscall54(uint32_t which, uint32_t varargs, struct FuncInst *funcinst)
{
	(void)funcinst;
	/* TODO: need to define non-no filesystem case */
	assert(which == 54);
	(void)which;
	(void)varargs;
	return 0;
}

/* close */
uint32_t wasmjit_emscripten____syscall6(uint32_t which, uint32_t varargs, struct FuncInst *funcinst)
{
	/* TODO: need to define non-no filesystem case */
	char *base;
	struct {
		uint32_t fd;
	} args;
	long ret;

	assert(which == 6);
	(void)which;

	base = wasmjit_emscripten_get_base_address(funcinst);

	memcpy(&args, base + varargs, sizeof(args));
	ret = sys_close(args.fd);
	return ret ? -1 : 0;
}

void wasmjit_emscripten____unlock(uint32_t x, struct FuncInst *funcinst)
{
	(void)funcinst;
	(void)x;
}

uint32_t wasmjit_emscripten__emscripten_memcpy_big(uint32_t dest, uint32_t src, uint32_t num, struct FuncInst *funcinst)
{
	char *base = wasmjit_emscripten_get_base_address(funcinst);
	memcpy(dest + base, src + base, num);
	return dest;
}

void wasmjit_emscripten_cleanup(struct ModuleInst *moduleinst) {
	(void)moduleinst;
	/* TODO: implement */
}
