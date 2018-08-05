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

#include <wasmjit/compile.h>

#include <wasmjit/parse.h>
#include <wasmjit/util.h>
#include <wasmjit/vector.h>
#include <wasmjit/runtime.h>

#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

struct SizedBuffer {
	size_t n_elts;
	char *elts;
};

static DEFINE_VECTOR_GROW(buffer, struct SizedBuffer);

static int output_buf(struct SizedBuffer *sstack, const char *buf,
		      size_t n_elts)
{
	if (!buffer_grow(sstack, n_elts))
		return 0;
	memcpy(sstack->elts + sstack->n_elts - n_elts, buf,
	       n_elts * sizeof(sstack->elts[0]));
	return 1;
}

#define FUNC_EXIT_CONT SIZE_MAX

struct BranchPoints {
	size_t n_elts;
	struct BranchPointElt {
		size_t branch_offset;
		size_t continuation_idx;
	} *elts;
};

static DEFINE_VECTOR_GROW(bp, struct BranchPoints);

struct LabelContinuations {
	size_t n_elts;
	size_t *elts;
};

static DEFINE_VECTOR_GROW(labels, struct LabelContinuations);

struct StaticStack {
	size_t n_elts;
	struct StackElt {
		enum {
			STACK_I32 = VALTYPE_I32,
			STACK_I64 = VALTYPE_I64,
			STACK_F32 = VALTYPE_F32,
			STACK_F64 = VALTYPE_F64,
			STACK_LABEL,
		} type;
		union {
			struct {
				int arity;
				int continuation_idx;
			} label;
		} data;
	} *elts;
};

static DEFINE_VECTOR_GROW(stack, struct StaticStack);
static DEFINE_VECTOR_TRUNCATE(stack, struct StaticStack);

static int push_stack(struct StaticStack *sstack, unsigned type)
{
	assert(type == STACK_I32 ||
	       type == STACK_I64 || type == STACK_F32 || type == STACK_F64);
	if (!stack_grow(sstack, 1))
		return 0;
	sstack->elts[sstack->n_elts - 1].type = type;
	return 1;
}

static unsigned peek_stack(struct StaticStack *sstack)
{
	assert(sstack->n_elts);
	return sstack->elts[sstack->n_elts - 1].type;
}

static int pop_stack(struct StaticStack *sstack)
{
	assert(sstack->n_elts);
	return stack_truncate(sstack, sstack->n_elts - 1);
}

static void encode_le_uint32_t(uint32_t val, char *buf)
{
	uint32_t le_val = uint32_t_swap_bytes(val);
	memcpy(buf, &le_val, sizeof(le_val));
}

#define MMIN(x, y) (((x) < (y)) ? (x) : (y))

#define OUTS(str)					   \
	do {						   \
		if (!output_buf(output, str, strlen(str))) \
			goto error;			   \
	}						   \
	while (0)

#define OUTB(b)						   \
	do {						   \
		char __b;				   \
		assert((b) <= 127 || (b) >= -128);	   \
		__b = b;				   \
		if (!output_buf(output, &__b, 1))	   \
			goto error;			   \
	}						   \
	while (0)

struct LocalsMD {
	uint8_t valtype;
	int8_t fp_offset;
};

static int wasmjit_compile_instructions(const struct Store *store,
					const struct ModuleInst *module,
					const struct TypeSectionType *type,
					const struct CodeSectionCode *code,
					const struct Instr *instructions,
					size_t n_instructions,
					struct SizedBuffer *output,
					struct LabelContinuations *labels,
					struct BranchPoints *branches,
					struct MemoryReferences *memrefs,
					struct LocalsMD *locals_md,
					size_t n_locals,
					int n_frame_locals,
					struct StaticStack *sstack)
{
	char buf[0x100];
	size_t i;

#define BUFFMT(...)						\
	do {							\
		int ret;					\
		ret = snprintf(buf, sizeof(buf), __VA_ARGS__);	\
		if (ret < 0)					\
			goto error;				\
	}							\
	while (1)

#define INC_LABELS()				\
	do {					\
		int res;			\
		res = labels_grow(labels, 1);	\
		if (!res)			\
			goto error;		\
	}					\
	while (0)

	for (i = 0; i < n_instructions; ++i) {
		switch (instructions[i].opcode) {
		case OPCODE_UNREACHABLE:
			break;
		case OPCODE_BLOCK:
		case OPCODE_LOOP:
			{
				int arity;
				size_t label_idx, stack_idx, output_idx;
				struct StackElt *elt;

				arity =
				    instructions[i].data.block.blocktype !=
				    VALTYPE_NULL ? 1 : 0;

				label_idx = labels->n_elts;
				INC_LABELS();

				stack_idx = sstack->n_elts;
				if (!stack_grow(sstack, 1))
					goto error;
				elt = &sstack->elts[stack_idx];
				elt->type = STACK_LABEL;
				elt->data.label.arity = arity;
				elt->data.label.continuation_idx = label_idx;

				output_idx = output->n_elts;
				wasmjit_compile_instructions(store,
							     module,
							     type,
							     code,
							     instructions
							     [i].data.
							     block.instructions,
							     instructions
							     [i].data.
							     block.n_instructions,
							     output, labels,
							     branches, memrefs,
							     locals_md, n_locals,
							     n_frame_locals,
							     sstack);

				/* shift stack results over label */
				memmove(&sstack->elts[stack_idx],
					&sstack->elts[sstack->n_elts - arity],
					arity * sizeof(sstack->elts[0]));
				if (!stack_truncate(sstack, stack_idx + arity))
					goto error;

				switch (instructions[i].opcode) {
				case OPCODE_BLOCK:
					labels->elts[label_idx] =
					    output->n_elts;
					break;
				case OPCODE_LOOP:
					labels->elts[label_idx] = output_idx;
					break;
				default:
					assert(0);
					break;
				}
			}
			break;
		case OPCODE_IF: {
			int arity;
			size_t jump_to_else_offset, jump_to_after_else_offset,
				stack_idx, label_idx;

			/* test top of stack */
			assert(peek_stack(sstack) == STACK_I32);
			pop_stack(sstack);
			/* pop %rax */
			OUTS("\x58");

			/* if not true jump to else case */
			/* cmp $0, %eax */
			OUTS("\x83\xf8\x00");

			jump_to_else_offset = output->n_elts + 2;
			/* je else_offset */
			OUTS("\x0f\x8f\x90\x90\x90\x90");

			/* output then case */
			label_idx = labels->n_elts;
			INC_LABELS();

			arity =
				instructions[i].data.if_.blocktype !=
				VALTYPE_NULL ? 1 : 0;
			{
				struct StackElt *elt;
				stack_idx = sstack->n_elts;
				if (!stack_grow(sstack, 1))
					goto error;
				elt = &sstack->elts[stack_idx];
				elt->type = STACK_LABEL;
				elt->data.label.arity = arity;
				elt->data.label.continuation_idx = label_idx;
			}

			wasmjit_compile_instructions(store,
						     module,
						     type,
						     code,
						     instructions
						     [i].data.
						     if_.instructions_then,
						     instructions
						     [i].data.
						     if_.n_instructions_then,
						     output, labels,
						     branches, memrefs,
						     locals_md, n_locals,
						     n_frame_locals,
						     sstack);

			/* if (else_exist) {
			   jump after else
			   }
			*/
			if (instructions[i].data.if_.n_instructions_else) {
				jump_to_after_else_offset = output->n_elts + 1;
				/* jmp after_else_offset */
				OUTS("\xe9\x90\x90\x90\x90");
			}

			/* fix up jump_to_else_offset */
			jump_to_else_offset = output->n_elts - jump_to_else_offset;
			encode_le_uint32_t(jump_to_else_offset - 4,
					   &output->elts[output->n_elts - jump_to_else_offset]);

			/* if (else_exist) {
			   output else case
			   }
			*/
			if (instructions[i].data.if_.n_instructions_else) {
				wasmjit_compile_instructions(store,
							     module,
							     type,
							     code,
							     instructions
							     [i].data.
							     if_.instructions_else,
							     instructions
							     [i].data.
							     if_.n_instructions_else,
							     output, labels,
							     branches, memrefs,
							     locals_md, n_locals,
							     n_frame_locals,
							     sstack);

				/* fix up jump_to_after_else_offset */
				jump_to_after_else_offset = output->n_elts - jump_to_after_else_offset;
				encode_le_uint32_t(jump_to_after_else_offset - 4,
						   &output->elts[output->n_elts - jump_to_after_else_offset]);
			}

			/* fix up static stack */
			/* shift stack results over label */
			memmove(&sstack->elts[stack_idx],
				&sstack->elts[sstack->n_elts - arity],
				arity * sizeof(sstack->elts[0]));
			if (!stack_truncate(sstack, stack_idx + arity))
				goto error;

			/* set labels position */
			labels->elts[label_idx] = output->n_elts;;
			break;
		}
		case OPCODE_BR_IF:
		case OPCODE_BR:
			{
				uint32_t labelidx, arity, stack_shift;
				size_t je_offset, je_offset_2, j;

				if (instructions[i].opcode == OPCODE_BR_IF) {
					/* LOGIC: v = pop_stack() */

					/* pop %rsi */
					assert(peek_stack(sstack) == STACK_I32);
					if (!pop_stack(sstack))
						goto error;
					OUTS("\x5e");

					/* LOGIC: if (v) br(); */

					/* testl %esi, %esi */
					OUTS("\x85\xf6");

					/* je AFTER_BR */
					je_offset = output->n_elts;
					OUTS("\x74\x01");
				}

				/* find out bottom of stack to L */
				j = sstack->n_elts;
				labelidx = instructions[i].data.br.labelidx;
				while (j) {
					j -= 1;
					if (sstack->elts[j].type == STACK_LABEL) {
						if (!labelidx) {
							break;
						}
						labelidx--;
					}
				}

				arity = sstack->elts[j].data.label.arity;
				stack_shift =
				    sstack->n_elts - j - (labelidx + 1) - arity;

				if (arity) {
					/* move top <arity> values for Lth label to
					   bottom of stack where Lth label is */

					/* LOGIC: memmove(sp + stack_shift * 8, sp, arity * 8); */

					/* mov %rsp, %rsi */
					OUTS("\x48\x89\xe6");

					if (arity - 1) {
						/* add <(arity - 1) * 8>, %rsi */
						assert((arity - 1) * 8 <=
						       INT_MAX);
						OUTS("\x48\x03\x34\x25");
						encode_le_uint32_t((arity -
								    1) * 8,
								   buf);
						if (!output_buf
						    (output, buf,
						     sizeof(uint32_t)))
							goto error;
					}

					/* mov %rsp, %rdi */
					OUTS("\x48\x89\xe7");

					/* add <(arity - 1 + stack_shift) * 8>, %rdi */
					if (arity - 1 + stack_shift) {
						assert((arity - 1 +
							stack_shift) * 8 <=
						       INT_MAX);
						OUTS("\x48\x81\xc7");
						encode_le_uint32_t((arity - 1 +
								    stack_shift)
								   * 8, buf);
						if (!output_buf
						    (output, buf,
						     sizeof(uint32_t)))
							goto error;
					}

					/* mov <arity>, %rcx */
					OUTS("\x48\xc7\xc1");
					assert(arity <= INT_MAX);
					encode_le_uint32_t(arity, buf);
					if (!output_buf
					    (output, buf, sizeof(uint32_t)))
						goto error;

					/* std */
					OUTS("\xfd");

					/* rep movsq */
					OUTS("\x48\xa5");
				}

				/* increment esp to Lth label (simulating pop) */
				/* add <stack_shift * 8>, %rsp */
				if (stack_shift) {
					OUTS("\x48\x81\xc4");
					assert(stack_shift * 8 <= INT_MAX);
					encode_le_uint32_t(stack_shift * 8,
							   buf);
					if (!output_buf
					    (output, buf, sizeof(uint32_t)))
						goto error;
				}

				/* place jmp to Lth label */

				/* jmp <BRANCH POINT> */
				je_offset_2 = output->n_elts;
				OUTS("\xe9\x90\x90\x90\x90");

				/* add jmp offset to branches list */
				{
					size_t branch_idx;

					branch_idx = branches->n_elts;
					if (!bp_grow(branches, 1))
						goto error;

					branches->
					    elts[branch_idx].branch_offset =
					    je_offset_2;
					branches->
					    elts[branch_idx].continuation_idx =
					    sstack->elts[j].data.
					    label.continuation_idx;
				}

				if (instructions[i].opcode == OPCODE_BR_IF) {
					/* update je operand in previous if block */
					int ret;
					size_t offset =
					    output->n_elts - je_offset - 2;
					assert(offset < 128 && offset > 0);
					ret =
					    snprintf(buf, sizeof(buf), "\x74%c",
						     (int)offset);
					if (ret < 0)
						goto error;
					assert(strlen(buf) == 2);
					memcpy(&output->elts[je_offset], buf,
					       2);
				}
			}

			break;
		case OPCODE_RETURN:
			/* shift $arity values from top of stock to below */

			/* lea (arity - 1)*8(%rsp), %rsi */
			OUTS("\x48\x8d\x74\x24");
			OUTB((intmax_t) ((type->n_outputs - 1) * 8));

			/* lea -8(%rbp), %rdi */
			OUTS("\x48\x8d\x7d\xf8");

			/* mov $arity, %rcx */
			OUTS("\x48\xc7\xc1");
			encode_le_uint32_t(type->n_outputs, buf);
			if (!output_buf(output, buf, sizeof(uint32_t)))
				goto error;

			/* std */
			OUTS("\xfd");

			/* rep movsq */
			OUTS("\x48\xa5");

			/* adjust stack to top of arity */
			/* lea -arity * 8(%rbp), %rsp */
			OUTS("\x48\x8d\x65");
			OUTB((intmax_t) (type->n_outputs * -8));

			/* jmp <EPILOGUE> */
			{
				size_t branch_idx;

				branch_idx = branches->n_elts;
				if (!bp_grow(branches, 1))
					goto error;

				branches->elts[branch_idx].branch_offset =
				    output->n_elts;
				branches->elts[branch_idx].continuation_idx =
				    FUNC_EXIT_CONT;

				OUTS("\xe9\x90\x90\x90\x90");
			}

			break;
		case OPCODE_CALL:
			{
				uint32_t fidx =
				    instructions[i].data.call.funcidx;
				size_t faddr = module->funcaddrs.elts[fidx];
				size_t n_inputs =
				    store->funcs.elts[faddr].type.n_inputs;
				size_t i;
				size_t n_movs, n_xmm_movs, n_stack;
				int aligned = 0;

				static const char *const movs[] = {
					"\x48\x8b\x7c\x24",	/* mov N(%rsp), %rdi */
					"\x48\x8b\x74\x24",	/* mov N(%rsp), %rsi */
					"\x48\x8b\x54\x24",	/* mov N(%rsp), %rdx */
					"\x48\x8b\x4c\x24",	/* mov N(%rsp), %rcx */
					"\x4c\x8b\x44\x24",	/* mov N(%rsp), %r8 */
					"\x4c\x8b\x4c\x24",	/* mov N(%rsp), %r9 */
				};

				static const char *const f32_movs[] = {
					"\xf3\x0f\x10\x44\x24",	/* movss N(%rsp), %xmm0 */
					"\xf3\x0f\x10\x4c\x24",	/* movss N(%rsp), %xmm1 */
					"\xf3\x0f\x10\x54\x24",	/* movss N(%rsp), %xmm2 */
					"\xf3\x0f\x10\x5c\x24",	/* movss N(%rsp), %xmm3 */
					"\xf3\x0f\x10\x64\x24",	/* movss N(%rsp), %xmm4 */
					"\xf3\x0f\x10\x6c\x24",	/* movss N(%rsp), %xmm5 */
					"\xf3\x0f\x10\x74\x24",	/* movss N(%rsp), %xmm6 */
					"\xf3\x0f\x10\x7c\x24",	/* movss N(%rsp), %xmm7 */
				};

				static const char *const f64_movs[] = {
					"\xf2\x0f\x10\x44\x24",	/* movsd N(%rsp), %xmm0 */
					"\xf2\x0f\x10\x4c\x24",	/* movsd N(%rsp), %xmm1 */
					"\xf2\x0f\x10\x54\x24",	/* movsd N(%rsp), %xmm2 */
					"\xf2\x0f\x10\x5c\x24",	/* movsd N(%rsp), %xmm3 */
					"\xf2\x0f\x10\x64\x24",	/* movsd N(%rsp), %xmm4 */
					"\xf2\x0f\x10\x6c\x24",	/* movsd N(%rsp), %xmm5 */
					"\xf2\x0f\x10\x74\x24",	/* movsd N(%rsp), %xmm6 */
					"\xf2\x0f\x10\x7c\x24",	/* movsd N(%rsp), %xmm7 */
				};

				/* align stack to 16-byte boundary */
				{
					size_t cur_stack_depth = n_frame_locals;

					/* add current stack depth */
					for (i = sstack->n_elts; i;) {
						i -= 1;
						if (sstack->elts[i].type != STACK_LABEL) {
							cur_stack_depth += 1;
						}
					}

					/* add stack contribution from spilled arguments */
					n_movs = 0;
					n_xmm_movs = 0;
					for (i = 0; i < n_inputs; ++i) {
						if ((store->funcs.elts[faddr].type.
						     input_types[i] == VALTYPE_I32
						     || store->funcs.elts[faddr].type.
						     input_types[i] == VALTYPE_I64)
						    && n_movs < 6) {
							n_movs += 1;
						} else if (store->funcs.elts[faddr].
							   type.input_types[i] ==
							   VALTYPE_F32
							   && n_xmm_movs < 8) {
							n_xmm_movs += 1;
						} else if (store->funcs.elts[faddr].
							   type.input_types[i] ==
							   VALTYPE_F64
							   && n_xmm_movs < 8) {
							n_xmm_movs += 1;
						} else {
							cur_stack_depth += 1;
						}
					}


					aligned = cur_stack_depth % 2;
					if (aligned)
						OUTS("\x48\x83\xec\x08");
				}

				n_movs = 0;
				n_xmm_movs = 0;
				n_stack = 0;
				for (i = 0; i < n_inputs; ++i) {
					intmax_t stack_offset;
					assert(sstack->
					       elts[sstack->n_elts - n_inputs +
						    i].type ==
					       store->funcs.elts[faddr].type.
					       input_types[i]);

					stack_offset =
					    (n_inputs - i - 1 + n_stack + aligned) * 8;
					if (stack_offset > 127
					    || stack_offset < -128)
						goto error;

					/* mov -n_inputs + i(%rsp), %rdi */
					if ((store->funcs.elts[faddr].type.
					     input_types[i] == VALTYPE_I32
					     || store->funcs.elts[faddr].type.
					     input_types[i] == VALTYPE_I64)
					    && n_movs < 6) {
						OUTS(movs[n_movs]);
						n_movs += 1;
					} else if (store->funcs.elts[faddr].
						   type.input_types[i] ==
						   VALTYPE_F32
						   && n_xmm_movs < 8) {
						OUTS(f32_movs[n_xmm_movs]);
						n_xmm_movs += 1;
					} else if (store->funcs.elts[faddr].
						   type.input_types[i] ==
						   VALTYPE_F64
						   && n_xmm_movs < 8) {
						OUTS(f64_movs[n_xmm_movs]);
						n_xmm_movs += 1;
					} else {
						OUTS("\xff\x74\x24");	/* push N(%rsp) */
						n_stack += 1;
					}

					OUTB(stack_offset);
				}

				/* set memory base address in known location */
				if (IS_HOST(&store->funcs.elts[faddr])) {
					size_t memref_idx;

					/* movq $const, %rax */
					memref_idx = memrefs->n_elts;
					if (!memrefs_grow(memrefs, 1))
						goto error;

					memrefs->elts[memref_idx].type =
					    MEMREF_MEM;
					memrefs->elts[memref_idx].code_offset =
					    output->n_elts + 2;
					memrefs->elts[memref_idx].addr =
					    module->memaddrs.elts[0];

					OUTS("\x48\xb8\x90\x90\x90\x90\x90\x90\x90\x90");

					/* movq %rax, (const) */
					memref_idx = memrefs->n_elts;
					if (!memrefs_grow(memrefs, 1))
						goto error;

					memrefs->elts[memref_idx].type =
					    MEMREF_MEM_BOX;
					memrefs->elts[memref_idx].code_offset =
					    output->n_elts + 2;

					OUTS("\x48\xa3\x90\x90\x90\x90\x90\x90\x90\x90");
				}

				/* movq $const, %rax */
				{
					size_t memref_idx;

					memref_idx = memrefs->n_elts;
					if (!memrefs_grow(memrefs, 1))
						goto error;

					memrefs->elts[memref_idx].type =
					    MEMREF_CALL;
					memrefs->elts[memref_idx].code_offset =
					    output->n_elts + 2;
					memrefs->elts[memref_idx].addr = faddr;

					OUTS("\x48\xb8\x90\x90\x90\x90\x90\x90\x90\x90");
				}

				/* call *%rax */
				OUTS("\xff\xd0");

				/* clean up stack */
				/* add (n_stack + n_inputs + aligned) * 8, %rsp */
				OUTS("\x48\x81\xc4");
				encode_le_uint32_t((n_stack + n_inputs + aligned) * 8,
						   buf);
				if (!output_buf(output, buf, sizeof(uint32_t)))
					goto error;

				if (!stack_truncate(sstack,
						    sstack->n_elts -
						    store->funcs.elts[faddr].type.
						    n_inputs))
					goto error;

				if (store->funcs.elts[faddr].type.n_outputs) {
					assert(store->funcs.elts[faddr].type.
					       n_outputs == 1);
					if (store->funcs.elts[faddr].type.
					    output_types[0] == VALTYPE_F32
					    || store->funcs.elts[faddr].type.
					    output_types[0] == VALTYPE_F64) {
						/* movq %xmm0, %rax */
						OUTS("\x66\x48\x0f\x7e\xc0");
					}
					/* push %rax */
					OUTS("\x50");

					if (!push_stack(sstack,
							store->funcs.elts[faddr].type.
							output_types[0]))
						goto error;
				}
			}
			break;
		case OPCODE_DROP:
			/* add $8, %rsp */
			OUTS("\x48\x83\xc4\x08");
			if (!pop_stack(sstack))
				goto error;
			break;
		case OPCODE_GET_LOCAL:
			assert(instructions[i].data.get_local.localidx < n_locals);
			push_stack(sstack,
				   locals_md[instructions[i].data.
					     get_local.localidx].valtype);

			/* push 8*(offset + 1)(%rbp) */
			OUTS("\xff\x75");
			OUTB(locals_md
			     [instructions[i].data.get_local.localidx]
			     .fp_offset);
			break;
		case OPCODE_SET_LOCAL:
			assert(peek_stack(sstack) ==
			       locals_md[instructions[i].data.
					 set_local.localidx].valtype);

			/* pop 8*(offset + 1)(%rbp) */
			OUTS("\x8f\x45");
			OUTB(locals_md
			     [instructions[i].data.set_local.localidx]
			     .fp_offset);
			pop_stack(sstack);
			break;
		case OPCODE_TEE_LOCAL:
			assert(peek_stack(sstack) ==
			       locals_md[instructions[i].data.
					 tee_local.localidx].valtype);

			/* movq (%rsp), %rax */
			OUTS("\x48\x8b\x04\x24");
			/* movq %rax, 8*(offset + 1)(%rbp) */
			OUTS("\x48\x89\x45");
			OUTB(locals_md
			     [instructions[i].data.tee_local.localidx]
			     .fp_offset);
			break;
		case OPCODE_GET_GLOBAL: {
			wasmjit_addr_t globaladdr;
			unsigned type;

			assert(instructions[i].data.get_global.globalidx < module->globaladdrs.n_elts);
			globaladdr = module->globaladdrs.elts[instructions[i].data.get_global.globalidx];

			type = store->globals.elts[globaladdr].value.type;
			switch (type) {
			case VALTYPE_I32:
			case VALTYPE_F32:
				/* mov (const), %eax */
				OUTS("\xa1\x90\x90\x90\x90\x90\x90\x90\x90");
				break;
			case VALTYPE_I64:
			case VALTYPE_F64:
				/* mov (const), %rax */
				OUTS("\x48\xa1\x90\x90\x90\x90\x90\x90\x90\x90");
				break;
			default:
				assert(0);
				break;
			}

			{
				size_t memref_idx;

				memref_idx = memrefs->n_elts;
				if (!memrefs_grow(memrefs, 1))
					goto error;

				memrefs->elts[memref_idx].type =
					MEMREF_GLOBAL_ADDR;
				memrefs->elts[memref_idx].code_offset =
					output->n_elts - 8;
				memrefs->elts[memref_idx].addr =
					globaladdr;
			}

			/* push %rax*/
			OUTS("\x50");
			push_stack(sstack, type);

			break;
		}
		case OPCODE_SET_GLOBAL: {
			wasmjit_addr_t globaladdr;

			assert(instructions[i].data.get_global.globalidx < module->globaladdrs.n_elts);
			globaladdr = module->globaladdrs.elts[instructions[i].data.get_global.globalidx];

			/* pop %rax */
			OUTS("\x58");

			assert(peek_stack(sstack) == store->globals.elts[globaladdr].value.type);
			if (!pop_stack(sstack))
				goto error;

			switch (store->globals.elts[globaladdr].value.type) {
			case VALTYPE_I32:
			case VALTYPE_F32:
				/* movl %eax, (const) */
				OUTS("\xa3\x90\x90\x90\x90\x90\x90\x90\x90");
				break;
			case VALTYPE_I64:
			case VALTYPE_F64:
				/* movq %rax, (const) */
				OUTS("\x48\xa3\x90\x90\x90\x90\x90\x90\x90\x90");
				break;
			default:
				assert(0);
				break;
			}

			{
				size_t memref_idx;

				memref_idx = memrefs->n_elts;
				if (!memrefs_grow(memrefs, 1))
					goto error;

				memrefs->elts[memref_idx].type =
					MEMREF_GLOBAL_ADDR;
				memrefs->elts[memref_idx].code_offset =
					output->n_elts - 8;
				memrefs->elts[memref_idx].addr =
					globaladdr;
			}

			break;
		}
		case OPCODE_I32_LOAD:
		case OPCODE_I64_LOAD:
		case OPCODE_I32_LOAD8_S:
		case OPCODE_I32_STORE:
		case OPCODE_I64_STORE:
		case OPCODE_I32_STORE8: {
			const struct LoadStoreExtra *extra;

			switch (instructions[i].opcode) {
			case OPCODE_I32_LOAD:
				extra = &instructions[i].data.i32_load;
				break;
			case OPCODE_I64_LOAD:
				extra = &instructions[i].data.i64_load;
				break;
			case OPCODE_I32_LOAD8_S:
				extra = &instructions[i].data.i32_load8_s;
				break;
			case OPCODE_I32_STORE:
				assert(peek_stack(sstack) == STACK_I32);
				extra = &instructions[i].data.i32_store;
				goto after;
			case OPCODE_I32_STORE8:
				assert(peek_stack(sstack) == STACK_I32);
				extra = &instructions[i].data.i32_store8;
				goto after;
			case OPCODE_I64_STORE:
				assert(peek_stack(sstack) == STACK_I64);
				extra = &instructions[i].data.i64_store;
			after:
				if (!pop_stack(sstack))
					goto error;

				/* pop rdi */
				OUTS("\x5f");
				break;
			default:
				assert(0);
				break;
			}

			/* LOGIC: ea = pop_stack() */

			/* pop %rsi */
			assert(peek_stack(sstack) == STACK_I32);
			if (!pop_stack(sstack))
				goto error;
			OUTS("\x5e");

			if (4 + instructions[i].data.i32_load.offset != 0) {
				/* LOGIC: ea += memarg.offset + 4 */

				/* add <VAL>, %rsi */
				OUTS("\x48\x81\xc6");
				encode_le_uint32_t(4 +
						   extra->offset, buf);
				if (!output_buf(output, buf, sizeof(uint32_t)))
					goto error;
			}

			{
				/* LOGIC: size = store->mems.elts[maddr].size */

				/* movq (const), %rax */
				OUTS("\x48\xa1\x90\x90\x90\x90\x90\x90\x90\x90");

				/* add reference to max */
				{
					size_t memref_idx;

					memref_idx = memrefs->n_elts;
					if (!memrefs_grow(memrefs, 1))
						goto error;

					memrefs->elts[memref_idx].type =
					    MEMREF_MEM_SIZE;
					memrefs->elts[memref_idx].code_offset =
					    output->n_elts - 8;
					memrefs->elts[memref_idx].addr =
					    module->memaddrs.elts[0];
				}

				/* LOGIC: if ea > size then trap() */

				/* cmp %rax, %rsi */
				OUTS("\x48\x39\xc6");

				/* jle AFTER_TRAP: */
				/* int $4 */
				/* AFTER_TRAP1  */
				OUTS("\x7e\x02\xcd\x04");
			}

			/* LOGIC: data = store->mems.elts[maddr].data */
			{
				/* movq (const), %rax */
				OUTS("\x48\xa1\x90\x90\x90\x90\x90\x90\x90\x90");

				/* add reference to data */
				{
					size_t memref_idx;

					memref_idx = memrefs->n_elts;
					if (!memrefs_grow(memrefs, 1))
						goto error;

					memrefs->elts[memref_idx].type =
					    MEMREF_MEM_ADDR;
					memrefs->elts[memref_idx].code_offset =
					    output->n_elts - 8;
					memrefs->elts[memref_idx].addr =
					    module->memaddrs.elts[0];
				}
			}


			switch (instructions[i].opcode) {
			case OPCODE_I32_LOAD:
			case OPCODE_I32_LOAD8_S:
			case OPCODE_I64_LOAD: {
				unsigned valtype;

				/* LOGIC: push_stack(data[ea - 4]) */
				switch (instructions[i].opcode) {
				case OPCODE_I32_LOAD8_S:
					/* movsbl -4(%rax, %rsi), %eax */
					OUTS("\x0f\xbe\x44\x30\xfc");
					valtype = STACK_I32;
					break;
				case OPCODE_I32_LOAD:
					/* movl -4(%rax, %rsi), %eax */
					OUTS("\x8b\x44\x30\xfc");
					valtype = STACK_I32;
					break;
				case OPCODE_I64_LOAD:
					/* movq -4(%rax, %rsi), %rax */
					OUTS("\x48\x8b\x44\x30\xfc");
					valtype = STACK_I64;
					break;
				default:
					assert(0);
					break;
				}

				/* push %rax */
				OUTS("\x50");
				if (!push_stack(sstack, valtype))
					goto error;

				break;
			}
			case OPCODE_I32_STORE:
				/* LOGIC: data[ea - 4] = pop_stack() */
				/* movl %edi, -4(%rax, %rsi) */
				OUTS("\x89\x7c\x30\xfc");
				break;
			case OPCODE_I32_STORE8:
				/* LOGIC: data[ea - 4] = pop_stack() */
				/* movb %dil, -4(%rax, %rsi) */
				OUTS("\x40\x88\x7c\x30\xfc");
				break;
			case OPCODE_I64_STORE:
				/* LOGIC: data[ea - 4] = pop_stack() */
				/* movq %rdi, -4(%rax, %rsi) */
				OUTS("\x48\x89\x7c\x30\xfc");
				break;
			default:
				assert(0);
				break;
			}

			break;
		}
		case OPCODE_I32_CONST:
			/* push $value */
			OUTS("\x68");
			encode_le_uint32_t(instructions[i].data.i32_const.value,
					   buf);
			if (!output_buf(output, buf, sizeof(uint32_t)))
				goto error;
			push_stack(sstack, STACK_I32);
			break;
		case OPCODE_I64_CONST:
			/* movq $value, %rax */
			OUTS("\x48\xb8");
			encode_le_uint64_t(instructions[i].data.i64_const.value,
					   buf);
			if (!output_buf(output, buf, sizeof(uint64_t)))
				goto error;

			/* push %rax */
			OUTS("\x50");

			push_stack(sstack, STACK_I64);
			break;
		case OPCODE_I32_EQZ:
			assert(peek_stack(sstack) == STACK_I32);
			/* xor %eax, %eax */
			OUTS("\x31\xc0");
			/* cmpl $0, (%rsp) */
			OUTS("\x83\x3c\x24\x00");
			/* sete %al */
			OUTS("\x0f\x94\xc0");
			/* mov %eax, (%rsp) */
			OUTS("\x89\x04\x24");
			break;
		case OPCODE_I32_EQ:
		case OPCODE_I32_NE:
		case OPCODE_I32_LT_S:
		case OPCODE_I32_LT_U:
		case OPCODE_I32_GT_S:
		case OPCODE_I32_GT_U:
		case OPCODE_I32_LE_U:
		case OPCODE_I32_GE_S:
			/* popq %rdi */
			assert(peek_stack(sstack) == STACK_I32);
			pop_stack(sstack);
			OUTS("\x5f");
			assert(peek_stack(sstack) == STACK_I32);
			/* xor %eax, %eax */
			OUTS("\x31\xc0");
			/* cmpl %edi, (%rsp) */
			OUTS("\x39\x3c\x24");
			switch (instructions[i].opcode) {
			case OPCODE_I32_EQ:
				/* sete %al */
				OUTS("\x0f\x94\xc0");
				break;
			case OPCODE_I32_NE:
				OUTS("\x0f\x95\xc0");
				break;
			case OPCODE_I32_LT_S:
				/* setl %al */
				OUTS("\x0f\x9c\xc0");
				break;
			case OPCODE_I32_LT_U:
				/* setb %al */
				OUTS("\x0f\x92\xc0");
				break;
			case OPCODE_I32_GT_S:
				/* setg %al */
				OUTS("\x0f\x9f\xc0");
				break;
			case OPCODE_I32_GT_U:
				/* seta %al */
				OUTS("\x0f\x97\xc0");
				break;
			case OPCODE_I32_LE_U:
				/* setbe %al */
				OUTS("\x0f\x96\xc0");
				break;
			case OPCODE_I32_GE_S:
				/* setge %al */
				OUTS("\x0f\x9d\xc0");
				break;
			default:
				assert(0);
				break;
			}
			/* mov %eax, (%rsp) */
			OUTS("\x89\x04\x24");
			break;
		case OPCODE_I32_SUB:
		case OPCODE_I32_ADD:
		case OPCODE_I32_MUL:
		case OPCODE_I32_AND:
		case OPCODE_I32_OR:
		case OPCODE_I32_XOR:
			/* popq %rax */
			assert(peek_stack(sstack) == STACK_I32);
			pop_stack(sstack);
			OUTS("\x5f");

			assert(peek_stack(sstack) == STACK_I32);

			switch (instructions[i].opcode) {
			case OPCODE_I32_SUB:
				OUTS("\x29\x04\x24");
				break;
			case OPCODE_I32_ADD:
				/* add    %eax,(%rsp) */
				OUTS("\x01\x04\x24");
				break;
			case OPCODE_I32_MUL:
				/* mull (%rsp) */
				OUTS("\xf7\x24\x24");
				/* mov    %eax,(%rsp) */
				OUTS("\x89\x04\x24");
				break;
			case OPCODE_I32_AND:
				/* and    %eax,(%rsp) */
				OUTS("\x21\x04\x24");
				break;
			case OPCODE_I32_OR:
				/* or    %eax,(%rsp) */
				OUTS("\x09\x04\x24");
				break;
			case OPCODE_I32_XOR:
				/* xor    %eax,(%rsp) */
				OUTS("\x31\x04\x24");
				break;
			default:
				assert(0);
				break;
			}

			break;
		case OPCODE_I32_SHL:
		case OPCODE_I32_SHR_S:
		case OPCODE_I32_SHR_U:
			/* pop %rcx */
			OUTS("\x59");
			assert(peek_stack(sstack) == STACK_I32);
			pop_stack(sstack);

			assert(peek_stack(sstack) == STACK_I32);

			switch (instructions[0].opcode) {
			case OPCODE_I32_SHL:
				/* shll   %cl,(%rsp) */
				OUTS("\xd3x24\x24");
				break;
			case OPCODE_I32_SHR_S:
				/* sarl %cl, (%rsp) */
				OUTS("\xd3\x3c\x24");
				break;
			case OPCODE_I32_SHR_U:
				/* shrl %cl, (%rsp) */
				OUTS("\xd3\x2c\x24");
				break;
			}

			break;
		default:
			fprintf(stderr, "Unhandled Opcode: 0x%" PRIx8 "\n", instructions[i].opcode);
			assert(0);
			break;
		}
	}

#undef INC_LABELS
#undef BUFFMT

	return 1;
 error:

	return 0;
}

char *wasmjit_compile_code(const struct Store *store,
			   const struct ModuleInst *module,
			   const struct TypeSectionType *type,
			   const struct CodeSectionCode *code,
			   struct MemoryReferences *memrefs, size_t *out_size)
{
	struct SizedBuffer outputv = { 0, NULL };
	struct SizedBuffer *output = &outputv;
	struct BranchPoints branches = { 0, NULL };
	struct StaticStack sstack = { 0, NULL };
	struct LabelContinuations labels = { 0, NULL };
	struct LocalsMD *locals_md;
	int n_frame_locals;
	unsigned n_locals;

	{
		size_t i;
		n_locals = type->n_inputs;
		for (i = 0; i < code->n_locals; ++i) {
			n_locals += code->locals[i].count;
		}
	}

	{
		size_t n_movs = 0, n_xmm_movs = 0, n_stack = 0, i;

		locals_md = calloc(n_locals, sizeof(locals_md[0]));
		if (!locals_md)
			goto error;

		for (i = 0; i < type->n_inputs; ++i) {
			if ((type->input_types[i] == VALTYPE_I32 ||
			     type->input_types[i] == VALTYPE_I64) &&
			    n_movs < 6) {
				locals_md[i].fp_offset =
				    -(1 + n_movs + n_xmm_movs) * 8;
				n_movs += 1;
			} else if ((type->input_types[i] == VALTYPE_F32 ||
				    type->input_types[i] == VALTYPE_F64) &&
				   n_xmm_movs < 8) {
				locals_md[i].fp_offset =
				    -(1 + n_movs + n_xmm_movs) * 8;
				n_xmm_movs += 1;
			} else {
				locals_md[i].fp_offset = (2 + n_stack) * 8;
				n_stack += 1;
			}
			locals_md[i].valtype = type->input_types[i];
		}

		for (i = 0; i < n_locals - type->n_inputs; ++i) {
			locals_md[i + type->n_inputs].fp_offset =
			    -(1 + n_movs + n_xmm_movs + i) * 8;
		}

		{
			size_t off = type->n_inputs;
			for (i = 0; i < code->n_locals; ++i) {
				size_t j;
				for (j = 0; j < code->locals[i].count; j++) {
					locals_md[off].valtype =  code->locals[i].valtype;
					off += 1;
				}
			}
		}

		assert(n_movs + n_xmm_movs +  (n_locals - type->n_inputs) <= INT_MAX);
		n_frame_locals = n_movs + n_xmm_movs + (n_locals - type->n_inputs);
	}

	/* output prologue, i.e. create stack frame */
	{
		size_t n_movs = 0, n_xmm_movs = 0, i;

		static char *const movs[] = {
			"\x48\x89\x7d",	/* mov %rdi, N(%rbp) */
			"\x48\x89\x75",	/* mov %rsi, N(%rbp) */
			"\x48\x89\x55",	/* mov %rdx, N(%rbp) */
			"\x48\x89\x4d",	/* mov %rcx, N(%rbp) */
			"\x4c\x89\x45",	/* mov %r8, N(%rbp) */
			"\x4c\x89\x4d",	/* mov %r9, N(%rbp) */
		};

		static const char *const f32_movs[] = {
			"\xf3\x0f\x11\x45",	/* movss %xmm0, N(%rbp) */
			"\xf3\x0f\x11\x4d",	/* movss %xmm1, N(%rbp) */
			"\xf3\x0f\x11\x55",	/* movss %xmm2, N(%rbp) */
			"\xf3\x0f\x11\x5d",	/* movss %xmm3, N(%rbp) */
			"\xf3\x0f\x11\x65",	/* movss %xmm4, N(%rbp) */
			"\xf3\x0f\x11\x6d",	/* movss %xmm5, N(%rbp) */
			"\xf3\x0f\x11\x75",	/* movss %xmm6, N(%rbp) */
			"\xf3\x0f\x11\x7d",	/* movss %xmm7, N(%rbp) */
		};

		static const char *const f64_movs[] = {
			"\xf2\x0f\x11\x45",	/* movsd %xmm0, N(%rbp) */
			"\xf2\x0f\x11\x4d",	/* movsd %xmm1, N(%rbp) */
			"\xf2\x0f\x11\x55",	/* movsd %xmm2, N(%rbp) */
			"\xf2\x0f\x11\x5d",	/* movsd %xmm3, N(%rbp) */
			"\xf2\x0f\x11\x65",	/* movsd %xmm4, N(%rbp) */
			"\xf2\x0f\x11\x6d",	/* movsd %xmm5, N(%rbp) */
			"\xf2\x0f\x11\x75",	/* movsd %xmm6, N(%rbp) */
			"\xf2\x0f\x11\x7d",	/* movsd %xmm7, N(%rbp) */
		};

		/* push %rbp */
		OUTS("\x55");

		/* mov %rsp, %rbp */
		OUTS("\x48\x89\xe5");

		/* sub $(8 * (n_frame_locals)), %rsp */
		if (n_frame_locals) {
			OUTS("\x48\x83\xec");
			OUTB(8 * n_frame_locals);
		}

		/* push args to stack */
		for (i = 0; i < type->n_inputs; ++i) {
			if (locals_md[i].fp_offset > 0)
				continue;

			if (type->input_types[i] == VALTYPE_I32 ||
			    type->input_types[i] == VALTYPE_I64) {
				OUTS(movs[n_movs]);
				n_movs += 1;
			} else {
				if (type->input_types[i] == VALTYPE_F32) {
					OUTS(f32_movs[n_xmm_movs]);
				} else {
					assert(type->input_types[i] ==
					       VALTYPE_F64);
					OUTS(f64_movs[n_xmm_movs]);
				}
				n_xmm_movs += 1;
			}
			OUTB(locals_md[i].fp_offset);
		}

		/* initialize and push locals to stack */
		if (n_locals - type->n_inputs) {
			if (n_locals - type->n_inputs == 1) {
				/* movq $0, (%rsp) */
				if (!output_buf
				    (output, "\x48\xc7\x04\x24\x00\x00\x00\x00",
				     8))
					goto error;
			} else {
				char buf[sizeof(uint32_t)];
				/* mov %rsp, %rdi */
				OUTS("\x48\x89\xe7");
				/* xor %rax, %rax */
				OUTS("\x48\x31\xc0");
				/* mov $n_locals, %rcx */
				OUTS("\x48\xc7\xc1");
				encode_le_uint32_t(n_locals - type->n_inputs, buf);
				if (!output_buf(output, buf, sizeof(uint32_t)))
					goto error;
				/* cld */
				OUTS("\xfc");
				/* rep stosq */
				OUTS("\xf3\x48\xab");
			}
		}
	}

	if (!wasmjit_compile_instructions(store, module, type, code,
					  code->instructions, code->n_instructions,
					  output, &labels, &branches, memrefs,
					  locals_md, n_locals, n_frame_locals, &sstack))
		goto error;

	/* fix branch points */
	{
		size_t i;
		for (i = 0; i < branches.n_elts; ++i) {
			char buf[1 + sizeof(uint32_t)] = { 0xe9 };
			struct BranchPointElt *branch = &branches.elts[i];
			/* TODO: handle FUNC_EXIT_CONT */
			assert(branch->continuation_idx != FUNC_EXIT_CONT);
			size_t continuation_offset =
			    labels.elts[branch->continuation_idx];
			uint32_t rel =
			    continuation_offset - branch->branch_offset -
			    sizeof(buf);
			encode_le_uint32_t(rel, &buf[1]);
			memcpy(&output->elts[branch->branch_offset], buf,
			       sizeof(buf));
		}
	}

	/* output epilogue */

	if (type->n_outputs) {
		assert(type->n_outputs == 1);
		assert(peek_stack(&sstack) == type->output_types[0]);
		pop_stack(&sstack);
		/* pop %rax */
		OUTS("\x58");
	}

	/* add $(8 * (n_frame_locals)), %rsp */
	if (n_frame_locals) {
		OUTS("\x48\x83\xc4");
		OUTB(8 * n_frame_locals);
	}

	/* pop %rbp */
	OUTS("\x5d");

	/* retq */
	OUTS("\xc3");

	*out_size = output->n_elts;

	return output->elts;

 error:
	assert(0);
	return NULL;
}

#undef OUTB
#undef OUTS
