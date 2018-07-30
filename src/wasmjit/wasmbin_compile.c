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

#include <wasmjit/wasmbin.h>
#include <wasmjit/util.h>

#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

struct ModuleInst {
	size_t *funcaddrs;
	size_t *tableaddrs;
	size_t *memaddrs;
	size_t *globaladdrs;
};

struct Store {
	struct FuncInst {
		struct ModuleInst *module;
		void *code;
	} *funcs;
	struct MemInst {
		char *data;
		uint32_t max;
	} *mems;
};

#define DEFINE_VECTOR_GROW(name, _type)					\
	int name ## _grow (_type *sstack, size_t n_elts) {		\
		void *newstackelts;					\
		newstackelts = realloc(sstack->elts, (sstack->n_elts + n_elts) * sizeof(sstack->elts[0])); \
		if (!newstackelts) {					\
			return 0;					\
		}							\
									\
		sstack->elts = newstackelts;				\
		sstack->n_elts += n_elts;				\
									\
		return 1;						\
	}

#define DEFINE_VECTOR_TRUNCATE(name, _type)				\
	static int name ## _truncate(_type *sstack, size_t amt) {	\
		void *newstackelts;					\
									\
		assert(amt <= sstack->n_elts);				\
									\
		newstackelts = realloc(sstack->elts, amt * sizeof(sstack->elts[0])); \
		if (!newstackelts) {					\
			return 0;					\
		}							\
									\
		sstack->elts = newstackelts;				\
		sstack->n_elts = amt;					\
									\
		return 1;						\
	}

struct SizedBuffer {
	size_t n_elts;
	char *elts;
};

DEFINE_VECTOR_GROW(buffer, struct SizedBuffer);

static int output_buf(struct SizedBuffer *sstack, const char *buf,
		      size_t n_elts)
{
	if (!buffer_grow(sstack, n_elts))
		return 0;
	memcpy(sstack->elts + sstack->n_elts - n_elts, buf,
	       n_elts * sizeof(sstack->elts[0]));
	return 1;
}

struct BranchPoints {
	size_t n_elts;
	struct BranchPointElt {
		size_t branch_offset;
		size_t continuation_idx;
	} *elts;
};

DEFINE_VECTOR_GROW(bp, struct BranchPoints);

struct LabelContinuations {
	size_t n_elts;
	size_t *elts;
};

DEFINE_VECTOR_GROW(labels, struct LabelContinuations);

struct StaticStack {
	size_t n_elts;
	struct StackElt {
		enum {
			STACK_I32,
			STACK_I64,
			STACK_F32,
			STACK_F64,
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

DEFINE_VECTOR_GROW(stack, struct StaticStack);
DEFINE_VECTOR_TRUNCATE(stack, struct StaticStack);

static int push_stack(struct StaticStack *sstack, int type)
{
	if (!stack_grow(sstack, 1))
		return 0;
	sstack->elts[sstack->n_elts - 1].type = type;
	return 1;
}

static int peek_stack(struct StaticStack *sstack)
{
	assert(sstack->n_elts);
	return sstack->elts[sstack->n_elts - 1].type;
}

static int pop_stack(struct StaticStack *sstack)
{
	return stack_truncate(sstack, 1);
}

static void encode_le_uint32_t(uint32_t val, char *buf)
{
	uint32_t le_val = uint32_t_swap_bytes(val);
	memcpy(buf, &le_val, sizeof(le_val));
}

static int wasmjit_compile_instructions(const struct TypeSectionType *type,
					const struct CodeSectionCode *code,
					const struct Instr *instructions,
					size_t n_instructions,
					struct SizedBuffer *output,
					struct LabelContinuations *labels,
					struct BranchPoints *branches,
					struct StaticStack *sstack)
{
	char buf[0x100];
	size_t i;
	int n_locals;

	// TODO: assert n_locals <= INT_MAX
	n_locals = code->n_locals;

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

#define OUTS(str)					   \
	do {						   \
		if (!output_buf(output, str, strlen(str))) \
			goto error;			   \
	}						   \
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
				wasmjit_compile_instructions(type,
							     code,
							     instructions
							     [i].data.
							     block.instructions,
							     instructions
							     [i].data.
							     block.n_instructions,
							     output, labels,
							     branches, sstack);

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
		case OPCODE_BR_IF:
		case OPCODE_BR:
			{
				uint32_t j, labelidx, arity, stack_shift;
				size_t je_offset, je_offset_2;

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
					OUTS("\xeb\x01");
				}

				/* find out bottom of stack to L */
				j = sstack->n_elts;
				labelidx = instructions[i].data.br.labelidx;
				while (j--) {
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
						OUTS("\x48\x03\x3c\x25");
						encode_le_uint32_t((arity - 1 +
								    stack_shift)
								   * 8, buf);
						if (!output_buf
						    (output, buf,
						     sizeof(uint32_t)))
							goto error;
					}

					/* mov <arity>, %rcx */
					OUTS("\x48\x89\xe5");
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
				OUTS("\x48\x03\x24\x25");
				assert(stack_shift * 8 <= INT_MAX);
				encode_le_uint32_t(stack_shift * 8, buf);
				if (!output_buf(output, buf, sizeof(uint32_t)))
					goto error;

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
					    output->n_elts - je_offset;
					assert(offset < 128 && offset > 0);
					ret =
					    snprintf(buf, sizeof(buf), "\xeb%c",
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
			break;
		case OPCODE_CALL:
			// push module_inst (rsp - 8, *rsp = module_inst)
			// push arg_n...
			// jmp to function
			break;
		case OPCODE_GET_LOCAL:
			break;
		case OPCODE_SET_LOCAL:
			break;
		case OPCODE_TEE_LOCAL:
			break;
		case OPCODE_I32_LOAD:
			/* LOGIC: module = frame->module... */

			/* movq (n_locals + 1) * 8(%rbp), %rax */
			OUTS("\x48\x8b\x85");
			encode_le_uint32_t((n_locals + 1) * 8, buf);
			if (!output_buf(output, buf, sizeof(uint32_t)))
				goto error;

			/* LOGIC: maddr = module->memaddrs[0] */

			/* movq memaddrs_offset(%rax), %rax */
			assert(offsetof(struct ModuleInst, memaddrs) < 128);
			BUFFMT("\x48\x8b\x40%c",
			       (int)offsetof(struct ModuleInst, memaddrs));
			assert(strlen(buf) == 4);
			if (!output_buf(output, buf, 4))
				goto error;

			/* movq 0(%rax), %rax */
			if (!output_buf(output, "\x48\x8b\x00", 3))
				goto error;

			/* LOGIC: store == rbx */

			/* LOGIC: max = store->mems[maddr].max */

			assert(sizeof(struct MemInst) == 16);

			/* movq %rax, %rdx */
			/* shlq $4, %rdx */
			OUTS("\x48\x89\xc2\x48\xc1\xe2\x04");

			/* addq mems_offset(%rbx), %rdx */
			assert(offsetof(struct Store, mems) < 128);
			BUFFMT("\x48\x03\x53%c",
			       (int)offsetof(struct Store, mems));
			assert(strlen(buf) == 4);
			if (!output_buf(output, buf, 4))
				goto error;

			/* mov max_offset(%rdx), %edi */
			assert(offsetof(struct MemInst, max) < 128);
			BUFFMT("\x8b\x7a%c",
			       (int)offsetof(struct MemInst, max));
			assert(strlen(buf) == 3);
			if (!output_buf(output, buf, 3))
				goto error;

			/* LOGIC: ea = pop_stack() */

			/* pop %rsi */
			assert(peek_stack(sstack) == STACK_I32);
			if (!pop_stack(sstack))
				goto error;
			OUTS("\x5e");

			if (instructions[i].data.i32_load.offset) {
				/* LOGIC: ea += memarg.offset */

				/* add <VAL>, %esi */
				OUTS("\x03\x34\x25");
				encode_le_uint32_t(instructions[i].
						   data.i32_load.offset, buf);
				if (!output_buf(output, buf, sizeof(uint32_t)))
					goto error;

				/* jno AFTER_TRAP: */
				/* int $4 */
				/* AFTER_TRAP1  */
				OUTS("\x71\x02\xcd\x04");
			}

			/* LOGIC: if ea + 4 > max then trap() */

			/* sub 4, %edi */
			OUTS("\x2b\x3c\x25");
			encode_le_uint32_t(sizeof(uint32_t), buf);
			if (!output_buf(output, buf, sizeof(uint32_t)))
				goto error;

			/* jno AFTER_TRAP: */
			/* int $4 */
			/* AFTER_TRAP1  */
			OUTS("\x71\x02\xcd\x04");

			/* cmp %edi, %esi */
			OUTS("\x39\xfe");

			/* jle AFTER_TRAP: */
			/* int $4 */
			/* AFTER_TRAP1  */
			OUTS("\x7e\x02\xcd\x04");

			/* LOGIC: data = store->mems[maddr].data */
			/* mov data_offset(%rdx), %rdi */
			assert(offsetof(struct MemInst, data) < 128);
			BUFFMT("\x48\x8b\x7a%c",
			       (int)offsetof(struct MemInst, data));
			assert(strlen(buf) ==
			       (4 - (offsetof(struct MemInst, data) == 0)));
			if (!output_buf(output, buf, 4))
				goto error;

			/* LOGIC: push_stack(data[ea]) */

			/* movl (%rdi, %rsi), %esi */
			OUTS("\x8b\x34\x37");

			/* push %rsi */
			OUTS("\x56");
			if (!push_stack(sstack, STACK_I32))
				goto error;

			break;
		case OPCODE_I32_CONST:
			/* push value onto stack */
			break;
		case OPCODE_I32_LT_S:
			break;
		case OPCODE_I32_ADD:
			break;
		case OPCODE_I32_MUL:
			break;
		default:
			break;
		}
	}

#undef OUTS
#undef INC_LABELS
#undef BUFFMT

	return 1;
 error:

	return 0;
}

void wasmjit_compile_code(const struct TypeSectionType *type,
			  const struct CodeSectionCode *code)
{
	struct SizedBuffer output = { 0, NULL };
	struct BranchPoints branches = { 0, NULL };
	struct StaticStack sstack = { 0, NULL };
	struct LabelContinuations labels = { 0, NULL };

	/* TODO: output prologue, i.e. create stack frame */

	wasmjit_compile_instructions(type, code,
				     code->instructions, code->n_instructions,
				     &output, &labels, &branches, &sstack);

	/* TODO: output epilogue */
}
