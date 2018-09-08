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

#include <linux/uaccess.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/mm.h>
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
	void *buf = NULL;
	loff_t size;
	char *module_name = NULL;

	/*
	  I know it's bad to read files from the kernel
	  but our intent is to have an interface similar to execve()
	  which does read files.
	*/

	/* TODO: do incremental reading */
	retval = kernel_read_file_from_fd(arg->fd, &buf, &size,
					  INT_MAX, READING_UNKNOWN);
	if (retval)
		goto error;


	module_name = kvstrndup_user(arg->module_name, 1024, GFP_KERNEL);
	if (IS_ERR(module_name)) {
		retval = PTR_ERR(module_name);
		module_name = NULL;
		goto error;
	}

	if (!wasmjit_high_instantiate_buf(&self->high, buf, size, module_name, 0)) {
		retval = -EINVAL;
		goto error;
	}

	retval = 0;

 error:
	if (module_name)
		kvfree(module_name);

	if (buf)
		vfree(buf);

	return retval;
}

static int kwasmjit_instantiate_emscripten_runtime(struct kwasmjit_private *self,
						   struct kwasmjit_instantiate_emscripten_runtime_args *args)
{
	int retval;

	if (!wasmjit_high_instantiate_emscripten_runtime(&self->high,
							 args->tablemin,
							 args->tablemax, 0)) {
		retval = -EINVAL;
		goto error;
	}

	retval = 0;

 error:
	return retval;
}

static int kwasmjit_emscripten_invoke_main(struct kwasmjit_private *self,
					   struct kwasmjit_emscripten_invoke_main_args *arg)
{
	int retval, i;
	char **argv = NULL, *module_name = NULL;

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

	module_name = kvstrndup_user(arg->module_name, 1024, GFP_USER);
	if (IS_ERR(module_name)) {
		retval = PTR_ERR(module_name);
		module_name = NULL;
		goto error;
	}

	{
		mm_segment_t old_fs = get_fs();
		/*
		  we only handle kernel validated memory now
		  so remove address limit
		*/
		set_fs(get_ds());
		retval = wasmjit_high_emscripten_invoke_main(&self->high,
							     module_name,
							     arg->argc, argv, 0);
		set_fs(old_fs);
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

	return retval;
}

static int kwasmjit_open(struct inode *inode, struct file *filp)
{
	/* allocate kwasmjit_private */
	filp->private_data = kvzalloc(sizeof(struct kwasmjit_private),
				      GFP_KERNEL);
	if (!filp->private_data)
		return -ENOMEM;
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
	default:
		retval = -EINVAL;
		break;
	}

 error:
	if (fpu_set)
		preemptible_kernel_fpu_end(fpu_preserve);

	if (fpu_preserve)
		kvfree(fpu_preserve);

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

static struct cdev *cdev = NULL;
static dev_t device_number = 0;

static void kwasmjit_cleanup_module(void)
{
	if (cdev)
		cdev_del(cdev);

	if (device_number)
		unregister_chrdev_region(device_number, 1);
}

static int __init kwasmjit_init(void)
{
	int retval;

	retval = alloc_chrdev_region(&device_number, 0, 1, "wasm");
	if (retval)
		goto error;

	cdev = cdev_alloc();
	if (!cdev)
		goto error;

	cdev_init(cdev, &kwasmjit_ops);

	retval = cdev_add(cdev, device_number, 1);
	if (retval)
		goto error;

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
