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

#include <linux/module.h>

#include <linux/time.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/a.out.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <linux/ptrace.h>
#include <linux/user.h>
#include <linux/binfmts.h>
#include <linux/personality.h>
#include <linux/init.h>
#include <linux/coredump.h>
#include <linux/slab.h>
#include <linux/sched/task_stack.h>
#include <linux/uaccess.h>

#include <asm/cacheflush.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Rian Hunter");
MODULE_DESCRIPTION("Executes WASM files natively.");
MODULE_VERSION("0.01");

static int load_wasm_binary(struct linux_binprm *bprm);

static struct linux_binfmt wasm_format = {
	.module		= THIS_MODULE,
	.load_binary	= load_wasm_binary,
	.load_shlib	= NULL,
	.core_dump	= NULL,
	.min_coredump	= 0,
};

#define WASM_HEADER "\0\x61\x73\x6d"
#define WASM_HEADER_LEN 4

static int load_wasm_binary(struct linux_binprm *bprm)
{
	struct pt_regs *regs = current_pt_regs();
	unsigned long error;
	int retval;
	unsigned long start = 0x400000;

	retval = -ENOEXEC;

	/* First of all, some simple consistency checks */
	if (memcmp(bprm->buf, WASM_HEADER, WASM_HEADER_LEN) != 0)
		return retval;

	/* Flush all traces of the currently running executable */
	retval = flush_old_exec(bprm);
	if (retval)
		return retval;

	set_personality(PER_LINUX);

	/* bprm->mm becomes current->mm */
	setup_new_exec(bprm);

	retval = setup_arg_pages(bprm, STACK_TOP, EXSTACK_DEFAULT);
	if (retval < 0)
		return retval;

	install_exec_creds(bprm);

	/* Map and load code */
	error = vm_brk_flags(start, PAGE_SIZE, VM_EXEC);
	if (error)
		return error;
	{
		/* this simply calls exit_group(111) in x86_64 */
		char code[] = "\xbf\x6f\x00\x00\x00\xb8\xe7\x00\x00\x00\x0f\x05";
		if (copy_to_user((void __user *) start, code, sizeof(code) - 1))
			return retval;

		flush_icache_range(start, start + PAGE_SIZE);
	}

	current->mm->end_code = PAGE_SIZE +
		(current->mm->start_code = start);
	current->mm->end_data = 0 +
		(current->mm->start_data = current->mm->end_code);
	current->mm->brk = 0 +
	(current->mm->start_brk = current->mm->end_data);

	current->mm->start_stack = bprm->p;

	set_binfmt(&wasm_format);

	finalize_exec(bprm);

	start_thread(regs, start, bprm->p);

	return 0;
}

static int __init kwasmjit_init(void)
{
	printk(KERN_DEBUG "kwasmjit loaded.\n");
	register_binfmt(&wasm_format);
	return 0;
}

static void __exit kwasmjit_exit(void)
{
	printk(KERN_DEBUG "kwasmjit unloaded.\n");
	unregister_binfmt(&wasm_format);
}

module_init(kwasmjit_init);
module_exit(kwasmjit_exit);
