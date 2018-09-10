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

.global invoke_on_stack
.type invoke_on_stack,@function
invoke_on_stack:
        push %rbp
        mov %rsp, %rbp
        mov %rdi, %rsp
        mov %rdx, %rdi
        call *%rsi
        mov %rbp, %rsp
        pop %rbp
        ret

/* The following code is Copyright 2011-2012 Nicholas J. Kain, licensed under standard MIT license */

.global setjmp
.type setjmp,@function
setjmp:
        mov %rbx,(%rdi)         /* rdi is jmp_buf, move registers onto it */
        mov %rbp,8(%rdi)
        mov %r12,16(%rdi)
        mov %r13,24(%rdi)
        mov %r14,32(%rdi)
        mov %r15,40(%rdi)
        lea 8(%rsp),%rdx        /* this is our rsp WITHOUT current ret addr */
        mov %rdx,48(%rdi)
        mov (%rsp),%rdx         /* save return addr ptr for new rip */
        mov %rdx,56(%rdi)
        xor %rax,%rax           /* always return 0 */
        ret

.global longjmp
.type longjmp,@function
longjmp:
        mov %rsi,%rax           /* val will be longjmp return */
        test %rax,%rax
        jnz 1f
        inc %rax                /* if val==0, val=1 per longjmp semantics */
1:
        mov (%rdi),%rbx         /* rdi is the jmp_buf, restore regs from it */
        mov 8(%rdi),%rbp
        mov 16(%rdi),%r12
        mov 24(%rdi),%r13
        mov 32(%rdi),%r14
        mov 40(%rdi),%r15
        mov 48(%rdi),%rdx       /* this ends up being the stack pointer */
        mov %rdx,%rsp
        mov 56(%rdi),%rdx       /* this is the instruction pointer */
        jmp *%rdx               /* goto saved address without altering rsp */
