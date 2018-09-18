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

#include <wasmjit/kwasmjit.h>

#include <wasmjit/high_level.h>
#include <wasmjit/runtime.h>
#include <wasmjit/sys.h>
#include <wasmjit/ktls.h>
#include <wasmjit/util.h>

#include <linux/sched/signal.h>
#include <linux/sched/mm.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/mmu_context.h>
#include <asm/fpu/api.h>
#include <asm/fpu/internal.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Rian Hunter");
MODULE_DESCRIPTION("Executes WASM files natively.");
MODULE_VERSION("0.01");

static void *kvmemdup_user(const void __user *src, size_t len, gfp_t flags)
{
	void *p;

	p = kvmalloc(len, flags);
	if (!p)
		return ERR_PTR(-ENOMEM);

	if (copy_from_user(p, src, len)) {
		kvfree(p);
		return ERR_PTR(-EFAULT);
	}

	return p;
}

static char *kvstrndup_user(const char __user *s, long n, gfp_t flags)
{
	char *p;
	long length;

	length = strnlen_user(s, n);

	if (!length)
		return ERR_PTR(-EFAULT);

	if (length > n)
		return ERR_PTR(-EINVAL);

	p = kvmemdup_user(s, length, flags);

	if (IS_ERR(p))
		return p;

	p[length - 1] = '\0';

	return p;
}

struct kwasmjit_private {
	struct WasmJITHigh high;
};

static int kwasmjit_instantiate(struct kwasmjit_private *self,
				struct kwasmjit_instantiate_args *arg)
{
	int retval;
	char *module_name = NULL;
	char *file_name = NULL;

	/*
	  I know it's bad to read files from the kernel
	  but our intent is to have an interface similar to execve()
	  which does read files.
	*/

	file_name = kvstrndup_user(arg->file_name, 1024, GFP_KERNEL);
	if (IS_ERR(file_name)) {
		retval = PTR_ERR(file_name);
		file_name = NULL;
		goto error;
	}

	module_name = kvstrndup_user(arg->module_name, 1024, GFP_KERNEL);
	if (IS_ERR(module_name)) {
		retval = PTR_ERR(module_name);
		module_name = NULL;
		goto error;
	}

	if (wasmjit_high_instantiate(&self->high, file_name, module_name, arg->flags)) {
		retval = -EINVAL;
		goto error;
	}

	retval = 0;

 error:
	if (module_name)
		kvfree(module_name);

	if (file_name)
		kvfree(file_name);

	return retval;
}

static int kwasmjit_instantiate_emscripten_runtime(struct kwasmjit_private *self,
						   struct kwasmjit_instantiate_emscripten_runtime_args *args)
{
	int retval;

	if (wasmjit_high_instantiate_emscripten_runtime(&self->high,
							args->static_bump,
							args->tablemin,
							args->tablemax, args->flags)) {
		retval = -EINVAL;
		goto error;
	}

	retval = 0;

 error:
	return retval;
}

static void wasmjit_set_ktls(struct KernelThreadLocal *ktls)
{
	memcpy(ptrptr(), &ktls, sizeof(ktls));
}

#define PAGE_ORDER_UP(x) ((order_base_2(x)  + (PAGE_SHIFT - 1)) / PAGE_SHIFT)

static void *alloc_stack(size_t requested_size, size_t *resulting_size)
{
#if !defined(CONFIG_THREAD_INFO_IN_TASK)
	/* NB: if thread info is stored on the stack, we cannot change the size
	   of the stack, otherwise the accessor functions won't work */
	return NULL;
#elif defined(CONFIG_VMAP_STACK) && defined(__x86_64__)
	/* NB: arm64's version of CONFIG_VMAP_STACK uses alignment to check
	   for corrupted stack, but this doesn't work if stack is larger than
	   THREAD_SIZE, which is the point here.
	 */
	size_t stack_pages;
	stack_pages = (requested_size >> PAGE_SHIFT) + ((requested_size & PAGE_MASK) ? 1 : 0);
	*resulting_size = stack_pages << PAGE_SHIFT;
	return __vmalloc(*resulting_size, THREADINFO_GFP, PAGE_KERNEL);
#elif !defined(CONFIG_VMAP_STACK)
	int node = NUMA_NO_NODE;
	size_t size_order = PAGE_ORDER_UP(requested_size);
	*resulting_size = size_order << (PAGE_SHIFT * size_order);
	struct page *page = alloc_pages_node(node, THREADINFO_GFP, size_order);
	return page ? page_address(page) : NULL;
#else
	return NULL;
#endif
}

static void free_stack(void *ptr, size_t size)
{
	BUG_ON(!ptr);
#ifdef CONFIG_VMAP_STACK
	vfree(ptr);
#else
	__free_pages(virt_to_page(ptr), PAGE_ORDER_UP(size));
#endif
}

#define MAX_STACK (8 * 1024 * 1024)

struct InvokeMainArgs {
	struct WasmJITHigh *high;
	const char *module_name;
	int argc;
	char **argv;
	char **envp;
	int flags;
};

static int handler(void *ctx)
{
	struct InvokeMainArgs *arg = ctx;
	return wasmjit_high_emscripten_invoke_main(arg->high,
						   arg->module_name,
						   arg->argc,
						   arg->argv,
						   arg->envp,
						   arg->flags);
}

int invoke_on_stack(void *stack, void *fptr, void *ctx);

static int invoke_main_on_stack(void *stack,
				struct WasmJITHigh *high,
				const char *module_name,
				int argc, char **argv, char **envp,
				int flags)
{
	struct InvokeMainArgs args = {
		.high = high,
		.module_name = module_name,
		.argc = argc,
		.argv = argv,
		.envp = envp,
		.flags = flags,
	};
#if defined(CONFIG_VMAP_STACK) && defined(__x86_64__)
	/* fault in vmalloc area to pgd before jumping off */
	READ_ONCE(*((char *)stack - PAGE_SIZE));
#endif
	return invoke_on_stack(stack, &handler, &args);
}

static int kwasmjit_emscripten_invoke_main(struct kwasmjit_private *self,
					   struct kwasmjit_emscripten_invoke_main_args *arg)
{
	int retval, i;
	char **argv = NULL, *module_name = NULL, **envp = NULL;

	argv = kvzalloc(arg->argc * sizeof(char *), GFP_KERNEL);
	if (IS_ERR(argv)) {
		retval = PTR_ERR(argv);
		argv = NULL;
		goto error;
	}

	for (i = 0; i < arg->argc; ++i) {
		char __user *argp;
		get_user(argp, arg->argv + i);
		argv[i] = kvstrndup_user(argp, 1024, GFP_KERNEL);
		if (IS_ERR(argv[i])) {
			retval = PTR_ERR(argv[i]);
			argv[i] = NULL;
			goto error;
		}
	}

	for (i = 0; ; ++i) {
		char __user *env;
		get_user(env, arg->envp + i);
		if (!env) break;
	}

	envp = kvzalloc((i + 1) * sizeof(char *), GFP_KERNEL);
	if (IS_ERR(envp)) {
		retval = PTR_ERR(envp);
		envp = NULL;
		goto error;
	}

	for (i = 0; ; ++i) {
		char __user *env;
		get_user(env, arg->envp + i);
		if (!env) break;
		envp[i] = kvstrndup_user(env, 1024, GFP_KERNEL);
		if (IS_ERR(envp[i])) {
			retval = PTR_ERR(envp[i]);
			envp[i] = NULL;
			goto error;
		}
	}

	module_name = kvstrndup_user(arg->module_name, 1024, GFP_USER);
	if (IS_ERR(module_name)) {
		retval = PTR_ERR(module_name);
		module_name = NULL;
		goto error;
	}

	{
		mm_segment_t old_fs = get_fs();
		size_t real_size;
		void *stack;
		struct mm_struct *saved_mm;

		/* set base address once so it's a quick load in the runtime */
		{
			size_t i;
			for (i = 0; i < self->high.n_modules; ++i) {
				if (!strcmp("env", self->high.modules[i].name)) {
					struct ModuleInst *inst = self->high.modules[i].module;
					wasmjit_get_ktls()->mem_inst = inst->mems.elts[0];
					break;
				}
			}
			if (i == self->high.n_modules) {
				retval = -EINVAL;
				goto error;
			}
		}

		stack = alloc_stack(MMAX(rlimit(RLIMIT_STACK), MAX_STACK), &real_size);
		if (stack) {
#ifdef CONFIG_STACK_GROWSUP
			wasmjit_set_stack_top(stack + real_size);
#else
			wasmjit_set_stack_top(stack);
#endif
		} else {
			void *addr = end_of_stack(current);
			/* account for STACK_END_MAGIC */
#ifdef CONFIG_STACK_GROWSUP
			addr = (unsigned long *)addr - 1;
#else
			addr = (unsigned long *)addr + 1;
#endif
			wasmjit_set_stack_top(addr);
		}

		/*
		  we only handle kernel validated memory now
		  so remove address limit
		*/
		set_fs(get_ds());

		/*
		   signal to kernel that we don't need our user mappings
		   this makes context switching much faster
		 */
		saved_mm = current->mm;
		mmgrab(saved_mm);
		unuse_mm(saved_mm);

		if (stack) {
			void *stack2 = stack;
#ifndef CONFIG_STACK_GROWSUP
			stack2 = (char *)stack + real_size;
#endif
			retval = invoke_main_on_stack(stack2,
						      &self->high,
						      module_name,
						      arg->argc, argv, envp, arg->flags);
		} else {
			retval = wasmjit_high_emscripten_invoke_main(&self->high,
								     module_name,
								     arg->argc, argv, envp, arg->flags);
		}

		/*
		  re-acquire our user mappings before returning to user space
		 */
		use_mm(saved_mm);
		mmdrop(saved_mm);

		set_fs(old_fs);

		if (stack) {
			free_stack(stack, real_size);
		}
	}

	if (retval < 0) {
		retval = -EINVAL;
	}

	/* TODO: copy back mutations to argv? */

 error:
	if (module_name)
		kvfree(module_name);

	if (argv) {
		for (i = 0; i < arg->argc; ++i) {
			if (argv[i])
				kvfree(argv[i]);
		}
		kvfree(argv);
	}

	if (envp) {
		for (i = 0; envp[i]; ++i) {
			kvfree(envp[i]);
		}
		kvfree(envp);
	}


	return retval;
}

static int kwasmjit_error_message(struct kwasmjit_private *self,
				  struct kwasmjit_error_message_args *arg)
{
	size_t src_len = strlen(self->high.error_buffer);
	size_t to_copy = MMIN(src_len, arg->size - 1);
	if (copy_to_user(arg->buffer, self->high.error_buffer, to_copy))
		return -EFAULT;
	if (put_user(0, &arg->buffer[to_copy]))
		return -EFAULT;
	return 0;
}

static int kwasmjit_open(struct inode *inode, struct file *filp)
{
	/* allocate kwasmjit_private */
	filp->private_data = kvzalloc(sizeof(struct kwasmjit_private),
				      GFP_KERNEL);
	if (!filp->private_data)
		return -ENOMEM;

	if (wasmjit_high_init(&((struct kwasmjit_private *)filp->private_data)->high)) {
		kvfree(filp->private_data);
		return -EINVAL;
	}

	return 0;
}

static void preemptible_kernel_fpu_begin(struct fpu *dest_fpu)
{
	preempt_disable();
	__kernel_fpu_begin();
	memcpy(dest_fpu, &current->thread.fpu, fpu_kernel_xstate_size);
	fpu__initialize(&current->thread.fpu);
	preempt_enable();
}

static void preemptible_kernel_fpu_end(struct fpu *src_fpu)
{
	preempt_disable();
	memcpy(&current->thread.fpu, src_fpu, fpu_kernel_xstate_size);
	__kernel_fpu_end();
	preempt_enable();
}

static long kwasmjit_unlocked_ioctl(struct file *filp,
				    unsigned int cmd,
				    unsigned long arg)
{
	struct fpu *fpu_preserve;
	long retval;
	void *parg = (void *) arg;
	struct kwasmjit_private *self = filp->private_data;
	int fpu_set = 0;
	struct KernelThreadLocal *preserve, ktls;

	preserve = wasmjit_get_ktls();

	memset(&ktls, 0, sizeof(ktls));
	wasmjit_set_ktls(&ktls);

	fpu_preserve = kvmalloc(fpu_kernel_xstate_size, GFP_KERNEL);
	if (!fpu_preserve) {
		retval = -ENOMEM;
		goto error;
	}

	preemptible_kernel_fpu_begin(fpu_preserve);
	fpu_set = 1;

	switch (cmd) {
	case KWASMJIT_INSTANTIATE: {
		struct kwasmjit_instantiate_args arg;
		unsigned version;

		get_user(version, (unsigned *) parg);
		if (version > 0) {
			retval = -EINVAL;
			goto error;
		}

		if (copy_from_user(&arg, parg, sizeof(arg))) {
			retval = -EFAULT;
			goto error;
		}

		retval = kwasmjit_instantiate(self, &arg);
		break;
	}
	case KWASMJIT_INSTANTIATE_EMSCRIPTEN_RUNTIME: {
		struct kwasmjit_instantiate_emscripten_runtime_args arg;
		unsigned version;

		get_user(version, (unsigned *) parg);
		if (version > 0) {
			retval = -EINVAL;
			goto error;
		}

		if (copy_from_user(&arg, parg, sizeof(arg))) {
			retval = -EFAULT;
			goto error;
		}

		retval = kwasmjit_instantiate_emscripten_runtime(self, &arg);
		break;
	}
	case KWASMJIT_EMSCRIPTEN_INVOKE_MAIN: {
		struct kwasmjit_emscripten_invoke_main_args arg;
		unsigned version;

		get_user(version, (unsigned *) parg);
		if (version > 0) {
			retval = -EINVAL;
			goto error;
		}

		if (copy_from_user(&arg, parg, sizeof(arg))) {
			retval = -EFAULT;
			goto error;
		}

		retval = kwasmjit_emscripten_invoke_main(self, &arg);
		break;
	}
	case KWASMJIT_ERROR_MESSAGE: {
		struct kwasmjit_error_message_args arg;
		unsigned version;

		get_user(version, (unsigned *) parg);
		if (version > 0) {
			retval = -EINVAL;
			goto error;
		}

		if (copy_from_user(&arg, parg, sizeof(arg))) {
			retval = -EFAULT;
			goto error;
		}

		retval = kwasmjit_error_message(self, &arg);
		break;
	}
	default:
		retval = -EINVAL;
		break;
	}

 error:
	if (fpu_set)
		preemptible_kernel_fpu_end(fpu_preserve);

	if (fpu_preserve)
		kvfree(fpu_preserve);

	wasmjit_set_ktls(preserve);

	return retval;
}

static int kwasmjit_release(struct inode *inode,
			    struct file *filp)
{
	struct kwasmjit_private *self = filp->private_data;
	wasmjit_high_close(&self->high);
	kvfree(self);
	return 0;
}

struct file_operations kwasmjit_ops = {
	.owner = THIS_MODULE,
	.open = kwasmjit_open,
	.unlocked_ioctl = kwasmjit_unlocked_ioctl,
	.release = kwasmjit_release,
};

#define CLASS_NAME "wasm"
#define DEVICE_NAME "wasm"
static dev_t device_number = -1;
static struct class *device_class = NULL;
static struct device *device_handle = NULL;

static void kwasmjit_cleanup_module(void)
{
	if (device_handle)
		device_destroy(device_class, MKDEV(device_number, 0));

	if (device_class)
		class_unregister(device_class);

	if (device_number >= 0)
		unregister_chrdev(device_number, DEVICE_NAME);
}

int wasmjit_emscripten_linux_kernel_init(void);

static int __init kwasmjit_init(void)
{
	if (!wasmjit_emscripten_linux_kernel_init())
		goto error;

	device_number = register_chrdev(0, DEVICE_NAME, &kwasmjit_ops);
	if (device_number < 0) {
		goto error;
	}

	device_class = class_create(THIS_MODULE, CLASS_NAME);
	if (IS_ERR(device_class)) {
		device_class = NULL;
		goto error;
	}

	device_handle = device_create(device_class, NULL, MKDEV(device_number, 0),
				      NULL, DEVICE_NAME);
	if (IS_ERR(device_handle)) {
		device_handle = NULL;
		goto error;
	}

	if (0) {
	error:
		kwasmjit_cleanup_module();
		printk(KERN_DEBUG "kwasmjit failed to load.\n");
	} else {
		printk(KERN_DEBUG "kwasmjit loaded.\n");
	}

	return 0;
}

static void __exit kwasmjit_exit(void)
{
	kwasmjit_cleanup_module();
	printk(KERN_DEBUG "kwasmjit unloaded.\n");
}

module_init(kwasmjit_init);
module_exit(kwasmjit_exit);
