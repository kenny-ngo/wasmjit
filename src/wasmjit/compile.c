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

#include <wasmjit/ast.h>
#include <wasmjit/util.h>
#include <wasmjit/vector.h>
#include <wasmjit/runtime.h>

#include <wasmjit/sys.h>

#define FUNC_EXIT_CONT SIZE_MAX

static DEFINE_VECTOR_GROW(memrefs, struct MemoryReferences);

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
				size_t arity;
				size_t continuation_idx;
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

__attribute__((unused))
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
		assert((b) <= 127 && ((intmax_t)(b)) >= -128);  \
		__b = (b);				   \
		if (!output_buf(output, &__b, 1))	   \
			goto error;			   \
	}						   \
	while (0)

#define OUTNULL(n)					\
	do {						\
		memset(buf, 0, (n));			\
		if (!output_buf(output, buf, (n)))	\
			goto error;			\
	}						\
	while (0)

#define INC_LABELS()				\
	do {					\
		int res;			\
		res = labels_grow(labels, 1);	\
		if (!res)			\
			goto error;		\
	}					\
	while (0)

struct LocalsMD {
	wasmjit_valtype_t valtype;
	int32_t fp_offset;
};

static int emit_br_code(struct SizedBuffer *output,
			struct StaticStack *sstack,
			struct BranchPoints *branches,
			uint32_t labelidx)
{
	char buf[sizeof(uint32_t)];
	size_t arity;
	size_t je_offset_2, j;
	int32_t stack_shift;
	uint32_t olabelidx = labelidx;
	/* find out bottom of stack to L */
	j = sstack->n_elts;
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
	assert(sstack->n_elts >= j + (olabelidx + 1) + arity);
	if (__builtin_mul_overflow(sstack->n_elts - j - (olabelidx + 1) - arity,
				   8, &stack_shift))
		goto error;

	if (arity) {
		int32_t off;
		if (__builtin_mul_overflow(arity - 1, 8, &off))
			goto error;

		/* move top <arity> values for Lth label to
		   bottom of stack where Lth label is */

		/* LOGIC: memmove(sp + stack_shift * 8, sp, arity * 8); */

		/* mov %rsp, %rsi */
		OUTS("\x48\x89\xe6");

		if (arity - 1) {
			/* add <(arity - 1) * 8>, %rsi */
			OUTS("\x48\x03\x34\x25");
			encode_le_uint32_t(off, buf);
			if (!output_buf
			    (output, buf,
			     sizeof(uint32_t)))
				goto error;
		}

		/* mov %rsp, %rdi */
		OUTS("\x48\x89\xe7");

		/* add <(arity - 1 + stack_shift) * 8>, %rdi */
		if (arity - 1 + stack_shift) {
			/* (arity - 1 +  stack_shift) * 8 */
			int32_t si;
			if (__builtin_add_overflow(off, stack_shift, &si))
				goto error;

			OUTS("\x48\x81\xc7");
			encode_le_uint32_t(si, buf);
			if (!output_buf
			    (output, buf,
			     sizeof(uint32_t)))
				goto error;
		}

		/* mov <arity>, %rcx */
		OUTS("\x48\xc7\xc1");
		if (arity > INT32_MAX)
			goto error;
		encode_le_uint32_t(arity, buf);
		if (!output_buf
		    (output, buf, sizeof(uint32_t)))
			goto error;

		/* std */
		OUTS("\xfd");

		/* rep movsq */
		OUTS("\x48\xa5");

		/* cld */
		OUTS("\xfc");
	}

	/* increment esp to Lth label (simulating pop) */
	/* add <stack_shift * 8>, %rsp */
	if (stack_shift) {
		OUTS("\x48\x81\xc4");
		encode_le_uint32_t(stack_shift, buf);
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

	return 1;

 error:
	return 0;
}

struct InstructionMD {
	const struct Instr *initiator;
	size_t cont;
	const struct Instr *instructions;
	size_t n_instructions;
	union {
		struct {
			size_t label_idx;
			size_t stack_idx;
			size_t output_idx;
		} block;
		struct {
			size_t label_idx;
			size_t stack_idx;
			size_t jump_to_else_offset;
			size_t jump_to_after_else_offset;
			int did_else;
		} if_;
	} data;
};

static int wasmjit_compile_instruction(const struct FuncType *func_types,
				       const struct ModuleTypes *module_types,
				       const struct FuncType *type,
				       struct SizedBuffer *output,
				       struct BranchPoints *branches,
				       struct MemoryReferences *memrefs,
				       struct LocalsMD *locals_md,
				       size_t n_locals,
				       size_t n_frame_locals,
				       struct StaticStack *sstack,
				       const struct Instr *instruction)
{
	char buf[sizeof(uint64_t)];

	(void)n_locals;

	switch (instruction->opcode) {
	case OPCODE_UNREACHABLE:
		/* ud2 */
		OUTS("\x0f\x0b");
		break;
	case OPCODE_NOP:
		break;
	case OPCODE_BLOCK:
	case OPCODE_LOOP: {
		/* handled by wasmjit_compile_instructions */
		assert(0);
		break;
	}
	case OPCODE_IF: {
		/* handled by wasmjit_compile_instructions */
		assert(0);
		break;
	}
	case OPCODE_BR_IF:
	case OPCODE_BR: {
		size_t je_offset;
		const struct BrIfExtra *extra;

		if (instruction->opcode == OPCODE_BR_IF) {
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

			extra = &instruction->data.br_if;
		}
		else {
			extra = &instruction->data.br;;
			/* appease gcc */
			je_offset = 0;
		}

		if (!emit_br_code(output, sstack, branches, extra->labelidx))
			goto error;

		if (instruction->opcode == OPCODE_BR_IF) {
			/* update je operand in previous if block */
			int ret;
			size_t offset =
				output->n_elts - je_offset - 2;
			assert(offset < 128 && offset > 0);
			assert(sizeof(buf) >= 2);
			ret =
				snprintf(buf, sizeof(buf), "\x74%c",
					 (int)offset);
			if (ret < 0)
				goto error;
			assert(strlen(buf) == 2);
			memcpy(&output->elts[je_offset], buf,
			       2);
		}

		break;
	}
	case OPCODE_BR_TABLE: {
		size_t table_offset, i, default_branch_offset;

		/* jump to the right code based on the input value */

		/* pop %rax */
		OUTS("\x58");
		if (!pop_stack(sstack))
			goto error;

		/* cmp $const, %eax */
		OUTS("\x48\x3d");
		/* const = instruction->data.br_table.n_labelidxs */
		encode_le_uint32_t(instruction->data.br_table.n_labelidxs,
				   buf);
		if (!output_buf(output, buf, sizeof(uint32_t)))
			goto error;

		/* jae default_branch */
		OUTS("\x0f\x83\x90\x90\x90\x90");
		default_branch_offset = output->n_elts;

		/* lea 9(%rip), %rdx */
		OUTS("\x48\x8d\x15\x09");
		OUTB(0); OUTB(0); OUTB(0);
		/* movsxl (%rdx, %rax, 4), %rax */
		OUTS("\x48\x63\x04\x82");
		/* add %rdx, %rax */
		OUTS("\x48\x01\xd0");
		/* jmp *%rax */
		OUTS("\xff\xe0");

		/* output nop for each branch */
		table_offset = output->n_elts;
		for (i = 0; i < instruction->data.br_table.n_labelidxs; ++i) {
			OUTS("\x90\x90\x90\x90");
		}

		for (i = 0; i < instruction->data.br_table.n_labelidxs; ++i) {
			/* store ip offset */
			uint32_t ip_offset = output->n_elts - table_offset;
			encode_le_uint32_t(ip_offset,
					   &output->elts[table_offset + i * sizeof(uint32_t)]);

			/* output branch */
			if (!emit_br_code(output, sstack, branches,
					  instruction->data.br_table.labelidxs[i]))
				goto error;
		}

		/* store ip offset, output default branch */
		encode_le_uint32_t(output->n_elts - default_branch_offset,
				   &output->elts[default_branch_offset - 4]);
		if (!emit_br_code(output, sstack, branches,
				  instruction->data.br_table.labelidx))
			goto error;

		break;
	}
	case OPCODE_RETURN:
		/* shift $arity values from top of stock to below */

		if (FUNC_TYPE_N_OUTPUTS(type)) {
			int32_t out;

			/* lea (arity - 1)*8(%rsp), %rsi */
			OUTS("\x48\x8d\x74\x24");
			OUTB(((intmax_t) (FUNC_TYPE_N_OUTPUTS(type) - 1)) * 8);

			/* lea (-8 * (n_frame_locals + 1))(%rbp), %rdi */
			OUTS("\x48\x8d\xbd");
			if (n_frame_locals == SIZE_MAX)
				goto error;
			if (__builtin_mul_overflow(n_frame_locals + 1, -8, &out))
				goto error;
			encode_le_uint32_t(out, buf);
			if (!output_buf(output, buf, sizeof(uint32_t)))
				goto error;

			/* mov $arity, %rcx */
			OUTS("\x48\xc7\xc1");
			encode_le_uint32_t(FUNC_TYPE_N_OUTPUTS(type), buf);
			if (!output_buf(output, buf, sizeof(uint32_t)))
				goto error;

			/* std */
			OUTS("\xfd");

			/* rep movsq */
			OUTS("\x48\xa5");

			/* cld */
			OUTS("\xfc");
		}

		/* adjust stack to top of arity */
		/* lea (arity + n_frame_locals)*-8(%rbp), %rsp */
		OUTS("\x48\x8d\xa5");
		if (n_frame_locals > SIZE_MAX - FUNC_TYPE_N_OUTPUTS(type))
			goto error;
		{
			int32_t out;
			if (__builtin_mul_overflow(n_frame_locals + FUNC_TYPE_N_OUTPUTS(type), -8, &out))
				goto error;
			encode_le_uint32_t(out, buf);
			if (!output_buf(output, buf, sizeof(uint32_t)))
				goto error;
		}

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
	case OPCODE_CALL_INDIRECT: {
		size_t i;
		size_t n_movs, n_xmm_movs, n_stack;
		int aligned = 0;
		const struct FuncType *ft;
		size_t cur_stack_depth = n_frame_locals;

		/* add current stack depth */
		for (i = sstack->n_elts; i;) {
			i -= 1;
			if (sstack->elts[i].type != STACK_LABEL) {
				cur_stack_depth += 1;
			}
		}

		if (instruction->opcode == OPCODE_CALL_INDIRECT) {
			ft = &func_types[instruction->data.call_indirect.typeidx];
			assert(peek_stack(sstack) == STACK_I32);
			if (!pop_stack(sstack))
				goto error;
			cur_stack_depth -= 1;

			/* mov $const, %rdi */
			OUTS("\x48\xbf");
			OUTNULL(8);
			{
				size_t memref_idx;
				memref_idx = memrefs->n_elts;
				if (!memrefs_grow(memrefs, 1))
					goto error;

				memrefs->elts[memref_idx].type =
					MEMREF_TABLE;
				memrefs->elts[memref_idx].code_offset =
					output->n_elts - 8;
				memrefs->elts[memref_idx].idx =
					0;
			}

			/* mov $const, %rsi */
			OUTS("\x48\xbe");
			OUTNULL(8);
			{
				size_t memref_idx;
				memref_idx = memrefs->n_elts;
				if (!memrefs_grow(memrefs, 1))
					goto error;

				memrefs->elts[memref_idx].type =
					MEMREF_TYPE;
				memrefs->elts[memref_idx].code_offset =
					output->n_elts - 8;
				memrefs->elts[memref_idx].idx =
					instruction->data.call_indirect.typeidx;
			}

			/* pop %rdx */
			OUTS("\x5a");

			/* mov $const, %rax */
			OUTS("\x48\xb8");
			OUTNULL(8);
			// address of _resolve_indirect_call
			{
				size_t memref_idx;
				memref_idx = memrefs->n_elts;
				if (!memrefs_grow(memrefs, 1))
					goto error;

				memrefs->elts[memref_idx].type =
					MEMREF_RESOLVE_INDIRECT_CALL;
				memrefs->elts[memref_idx].code_offset =
					output->n_elts - 8;
			}

			/* align to 16 bytes */
			if (cur_stack_depth % 2)
				/* sub $8, %rsp */
				OUTS("\x48\x83\xec\x08");

			/* call *%rax */
			OUTS("\xff\xd0");

			if (cur_stack_depth % 2)
				/* add $8, %rsp */
				OUTS("\x48\x83\xc4\x08");
		} else {
			uint32_t fidx =
				instruction->data.call.funcidx;
			ft = &module_types->functypes[fidx];

			/* movq $const, %rax */
			OUTS("\x48\xb8");
			OUTNULL(8);
			{
				size_t memref_idx;

				memref_idx = memrefs->n_elts;
				if (!memrefs_grow(memrefs, 1))
					goto error;

				memrefs->elts[memref_idx].type =
					MEMREF_FUNC;
				memrefs->elts[memref_idx].code_offset =
					output->n_elts - 8;
				memrefs->elts[memref_idx].idx = fidx;
			}

			/* mov compiled_code_off(%rax), %rax */
			OUTS("\x48\x8b\x40");
			OUTB(offsetof(struct FuncInst, compiled_code));
		}

		/* align stack to 16-byte boundary */
		{
			/* add stack contribution from spilled arguments */
			n_movs = 0;
			n_xmm_movs = 0;
			for (i = 0; i < ft->n_inputs; ++i) {
				if ((ft->input_types[i] == VALTYPE_I32 ||
				     ft->input_types[i] == VALTYPE_I64)
				    && n_movs < 6) {
					n_movs += 1;
				} else if (ft->input_types[i] ==
					   VALTYPE_F32
					   && n_xmm_movs < 8) {
					n_xmm_movs += 1;
				} else if (ft->input_types[i] ==
					   VALTYPE_F64
					   && n_xmm_movs < 8) {
					n_xmm_movs += 1;
				} else {
					cur_stack_depth += 1;
				}
			}


			aligned = cur_stack_depth % 2;
			if (aligned)
				/* sub $8, %rsp */
				OUTS("\x48\x83\xec\x08");
		}

		n_movs = 0;
		n_xmm_movs = 0;
		n_stack = 0;
		for (i = 0; i < ft->n_inputs; ++i) {
			static const char *const movs[] = {
				"\x48\x8b\xbc\x24",	/* mov N(%rsp), %rdi */
				"\x48\x8b\xb4\x24",	/* mov N(%rsp), %rsi */
				"\x48\x8b\x94\x24",	/* mov N(%rsp), %rdx */
				"\x48\x8b\x8c\x24",	/* mov N(%rsp), %rcx */
				"\x4c\x8b\x84\x24",	/* mov N(%rsp), %r8 */
				"\x4c\x8b\x8c\x24",	/* mov N(%rsp), %r9 */
			};

			static const char *const f32_movs[] = {
				"\xf3\x0f\x10\x84\x24",	/* movss N(%rsp), %xmm0 */
				"\xf3\x0f\x10\x8c\x24",	/* movss N(%rsp), %xmm1 */
				"\xf3\x0f\x10\x94\x24",	/* movss N(%rsp), %xmm2 */
				"\xf3\x0f\x10\x9c\x24",	/* movss N(%rsp), %xmm3 */
				"\xf3\x0f\x10\xa4\x24",	/* movss N(%rsp), %xmm4 */
				"\xf3\x0f\x10\xac\x24",	/* movss N(%rsp), %xmm5 */
				"\xf3\x0f\x10\xb4\x24",	/* movss N(%rsp), %xmm6 */
				"\xf3\x0f\x10\xbc\x24",	/* movss N(%rsp), %xmm7 */
			};

			static const char *const f64_movs[] = {
				"\xf2\x0f\x10\x84\x24",	/* movsd N(%rsp), %xmm0 */
				"\xf2\x0f\x10\x8c\x24",	/* movsd N(%rsp), %xmm1 */
				"\xf2\x0f\x10\x94\x24",	/* movsd N(%rsp), %xmm2 */
				"\xf2\x0f\x10\x9c\x24",	/* movsd N(%rsp), %xmm3 */
				"\xf2\x0f\x10\xa4\x24",	/* movsd N(%rsp), %xmm4 */
				"\xf2\x0f\x10\xac\x24",	/* movsd N(%rsp), %xmm5 */
				"\xf2\x0f\x10\xb4\x24",	/* movsd N(%rsp), %xmm6 */
				"\xf2\x0f\x10\xbc\x24",	/* movsd N(%rsp), %xmm7 */
			};

			intmax_t stack_offset;
			assert(sstack->
			       elts[sstack->n_elts - ft->n_inputs +
				    i].type ==
			       ft->input_types[i]);

			stack_offset =
				(ft->n_inputs - i - 1 + n_stack + aligned) * 8;

			/* mov -n_inputs + i(%rsp), %rdi */
			if ((ft->input_types[i] == VALTYPE_I32 ||
			     ft->input_types[i] == VALTYPE_I64)
			    && n_movs < 6) {
				OUTS(movs[n_movs]);
				n_movs += 1;
			} else if (ft->input_types[i] ==
				   VALTYPE_F32
				   && n_xmm_movs < 8) {
				OUTS(f32_movs[n_xmm_movs]);
				n_xmm_movs += 1;
			} else if (ft->input_types[i] ==
				   VALTYPE_F64
				   && n_xmm_movs < 8) {
				OUTS(f64_movs[n_xmm_movs]);
				n_xmm_movs += 1;
			} else {
				stack_offset =
					(i - (ft->n_inputs - 1) + n_stack + aligned) * 8;
				OUTS("\xff\xb4\x24");	/* push N(%rsp) */
				n_stack += 1;
			}

			encode_le_uint32_t(stack_offset, buf);
			if (!output_buf(output, buf, sizeof(uint32_t)))
				goto error;
		}

		/* call *%rax */
		OUTS("\xff\xd0");

		/* clean up stack */
		/* add (n_stack + n_inputs + aligned) * 8, %rsp */
		OUTS("\x48\x81\xc4");
		encode_le_uint32_t((n_stack + ft->n_inputs + aligned) * 8,
				   buf);
		if (!output_buf(output, buf, sizeof(uint32_t)))
			goto error;

		if (!stack_truncate(sstack,
				    sstack->n_elts -
				    ft->n_inputs))
			goto error;

		if (FUNC_TYPE_N_OUTPUTS(ft)) {
			assert(FUNC_TYPE_N_OUTPUTS(ft) == 1);
			if (FUNC_TYPE_OUTPUT_TYPES(ft)[0] == VALTYPE_F32) {
				/* movd %xmm0, %eax */
				OUTS("\x66\x0f\x7e\xc0");
			} else if (FUNC_TYPE_OUTPUT_TYPES(ft)[0] == VALTYPE_F64) {
				/* movq %xmm0, %rax */
				OUTS("\x66\x48\x0f\x7e\xc0");
			}
			/* push %rax */
			OUTS("\x50");

			if (!push_stack(sstack, FUNC_TYPE_OUTPUT_TYPES(ft)[0]))
				goto error;
		}
		break;
	}
	case OPCODE_DROP:
		/* add $8, %rsp */
		OUTS("\x48\x83\xc4\x08");
		if (!pop_stack(sstack))
			goto error;
		break;

	case OPCODE_SELECT: {
		assert(peek_stack(sstack) == STACK_I32);
		if (!pop_stack(sstack))
			goto error;

		if (!pop_stack(sstack))
			goto error;

		/* pop %rax */
		OUTS("\x58");

		/* pop %rdx */
		OUTS("\x5a");

		/* test %eax, %eax */
		OUTS("\x85\xc0");

		/* jnz +4 */
		OUTS("\x75\x04");

		/* mov %rdx, (%rsp) */
		OUTS("\x48\x89\x14\x24");

		break;
	}
	case OPCODE_GET_LOCAL:
		assert(instruction->data.get_local.localidx < n_locals);
		push_stack(sstack,
			   locals_md[instruction->data.
				     get_local.localidx].valtype);

		/* push fp_offset(%rbp) */
		OUTS("\xff\xb5");
		encode_le_uint32_t(locals_md
				   [instruction->data.get_local.localidx]
				   .fp_offset, buf);
		if (!output_buf(output, buf, sizeof(uint32_t)))
			goto error;
		break;
	case OPCODE_SET_LOCAL:
		assert(peek_stack(sstack) ==
		       locals_md[instruction->data.
				 set_local.localidx].valtype);

		/* pop fp_offset(%rbp) */
		OUTS("\x8f\x85");
		encode_le_uint32_t(locals_md
				   [instruction->data.set_local.localidx]
				   .fp_offset, buf);
		if (!output_buf(output, buf, sizeof(uint32_t)))
			goto error;
		pop_stack(sstack);
		break;
	case OPCODE_TEE_LOCAL:
		assert(peek_stack(sstack) ==
		       locals_md[instruction->data.
				 tee_local.localidx].valtype);

		/* mov (%rsp), %rax */
		OUTS("\x48\x8b\x04\x24");
		/* movq %rax, fp_offset(%rbp) */
		OUTS("\x48\x89\x85");
		encode_le_uint32_t(locals_md
				   [instruction->data.tee_local.localidx]
				   .fp_offset, buf);
		if (!output_buf(output, buf, sizeof(uint32_t)))
			goto error;
		break;
	case OPCODE_GET_GLOBAL: {
		uint32_t gidx = instruction->data.get_global.globalidx;
		unsigned type;

		/* movq $const, %rax */
		OUTS("\x48\xb8");
		OUTNULL(8);
		{
			size_t memref_idx;

			memref_idx = memrefs->n_elts;
			if (!memrefs_grow(memrefs, 1))
				goto error;

			memrefs->elts[memref_idx].type =
				MEMREF_GLOBAL;
			memrefs->elts[memref_idx].code_offset =
				output->n_elts - 8;
			memrefs->elts[memref_idx].idx =
				gidx;
		}

		type = module_types->globaltypes[gidx].valtype;
		switch (type) {
		case VALTYPE_I32:
		case VALTYPE_F32:
			/* mov offset(%rax), %eax */
			OUTS("\x8b\x40");
			if (type == VALTYPE_I32) {
				OUTB(offsetof(struct GlobalInst, value) +
				     offsetof(struct Value, data) +
				     offsetof(union ValueUnion, i32));
			} else {
				OUTB(offsetof(struct GlobalInst, value) +
				     offsetof(struct Value, data) +
				     offsetof(union ValueUnion, f32));
			}
			break;
		case VALTYPE_I64:
		case VALTYPE_F64:
			/* mov offset(%rax), %rax */
			OUTS("\x48\x8b\x40");
			if (type == VALTYPE_I64) {
				OUTB(offsetof(struct GlobalInst, value) +
				     offsetof(struct Value, data) +
				     offsetof(union ValueUnion, i64));
			} else {
				OUTB(offsetof(struct GlobalInst, value) +
				     offsetof(struct Value, data) +
				     offsetof(union ValueUnion, f64));
			}
			break;
		default:
			assert(0);
			break;
		}


		/* push %rax*/
		OUTS("\x50");
		push_stack(sstack, type);

		break;
	}
	case OPCODE_SET_GLOBAL: {
		uint32_t gidx = instruction->data.get_global.globalidx;
		unsigned type = module_types->globaltypes[gidx].valtype;

		/* pop %rdx */
		OUTS("\x5a");

		assert(peek_stack(sstack) == type);
		if (!pop_stack(sstack))
			goto error;

		/* movq $const, %rax */
		OUTS("\x48\xb8");
		OUTNULL(8);
		{
			size_t memref_idx;

			memref_idx = memrefs->n_elts;
			if (!memrefs_grow(memrefs, 1))
				goto error;

			memrefs->elts[memref_idx].type =
				MEMREF_GLOBAL;
			memrefs->elts[memref_idx].code_offset =
				output->n_elts - 8;
			memrefs->elts[memref_idx].idx =
				gidx;
		}

		type = module_types->globaltypes[gidx].valtype;
		switch (type) {
		case VALTYPE_I32:
		case VALTYPE_F32:
			/* mov %edx, offset(%rax) */
			OUTS("\x89\x50");
			if (type == VALTYPE_I32) {
				OUTB(offsetof(struct GlobalInst, value) +
				     offsetof(struct Value, data) +
				     offsetof(union ValueUnion, i32));
			} else {
				OUTB(offsetof(struct GlobalInst, value) +
				     offsetof(struct Value, data) +
				     offsetof(union ValueUnion, f32));
			}
			break;
		case VALTYPE_I64:
		case VALTYPE_F64:
			/* mov %rdx, offset(%rax) */
			OUTS("\x48\x89\x50");
			if (type == VALTYPE_I64) {
				OUTB(offsetof(struct GlobalInst, value) +
				     offsetof(struct Value, data) +
				     offsetof(union ValueUnion, i64));
			} else {
				OUTB(offsetof(struct GlobalInst, value) +
				     offsetof(struct Value, data) +
				     offsetof(union ValueUnion, f64));
			}
			break;
		default:
			assert(0);
			break;
		}

		break;
	}
	case OPCODE_I32_LOAD:
	case OPCODE_I64_LOAD:
	case OPCODE_F32_LOAD:
	case OPCODE_F64_LOAD:
	case OPCODE_I32_LOAD8_S:
	case OPCODE_I32_STORE:
	case OPCODE_F32_STORE:
	case OPCODE_I64_STORE:
	case OPCODE_F64_STORE:
	case OPCODE_I32_STORE8:
	case OPCODE_I32_STORE16: {
		const struct LoadStoreExtra *extra;

		switch (instruction->opcode) {
		case OPCODE_I32_LOAD:
			extra = &instruction->data.i32_load;
			break;
		case OPCODE_I64_LOAD:
			extra = &instruction->data.i64_load;
			break;
		case OPCODE_F32_LOAD:
			extra = &instruction->data.f32_load;
			break;
		case OPCODE_F64_LOAD:
			extra = &instruction->data.f64_load;
			break;
		case OPCODE_I32_LOAD8_S:
			extra = &instruction->data.i32_load8_s;
			break;
		case OPCODE_I32_STORE:
			assert(peek_stack(sstack) == STACK_I32);
			extra = &instruction->data.i32_store;
			goto after;
		case OPCODE_I32_STORE8:
			assert(peek_stack(sstack) == STACK_I32);
			extra = &instruction->data.i32_store8;
			goto after;
		case OPCODE_I32_STORE16:
			assert(peek_stack(sstack) == STACK_I32);
			extra = &instruction->data.i32_store16;
			goto after;
		case OPCODE_I64_STORE:
			assert(peek_stack(sstack) == STACK_I64);
			extra = &instruction->data.i64_store;
			goto after;
		case OPCODE_F32_STORE:
			assert(peek_stack(sstack) == STACK_F32);
			extra = &instruction->data.f32_store;
			goto after;
		case OPCODE_F64_STORE:
			assert(peek_stack(sstack) == STACK_F64);
			extra = &instruction->data.f64_store;
		after:
			if (!pop_stack(sstack))
				goto error;

			/* pop rdi */
			OUTS("\x5f");
			break;
		default:
			assert(0);
			__builtin_unreachable();
			break;
		}

		/* LOGIC: ea = pop_stack() */

		/* pop %rsi */
		assert(peek_stack(sstack) == STACK_I32);
		if (!pop_stack(sstack))
			goto error;
		OUTS("\x5e");

		if (4 + extra->offset != 0) {
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

			/* movq $const, %rax */
			OUTS("\x48\xb8");
			OUTNULL(8);

			/* add reference to max */
			{
				size_t memref_idx;

				memref_idx = memrefs->n_elts;
				if (!memrefs_grow(memrefs, 1))
					goto error;

				memrefs->elts[memref_idx].type =
					MEMREF_MEM;
				memrefs->elts[memref_idx].code_offset =
					output->n_elts - 8;
				memrefs->elts[memref_idx].idx =
					0;
			}

			/* mov size_offset(%rax), %rax */
			OUTS("\x48\x8b\x40");
			OUTB(offsetof(struct MemInst, size));

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
			/* movq $const, %rax */
			OUTS("\x48\xb8");
			OUTNULL(8);

			/* add reference to data */
			{
				size_t memref_idx;

				memref_idx = memrefs->n_elts;
				if (!memrefs_grow(memrefs, 1))
					goto error;

				memrefs->elts[memref_idx].type =
					MEMREF_MEM;
				memrefs->elts[memref_idx].code_offset =
					output->n_elts - 8;
				memrefs->elts[memref_idx].idx =
					0;
			}

			/* mov data_off(%rax), %rax */
			OUTS("\x48\x8b\x40");
			OUTB(offsetof(struct MemInst, data));
		}


		switch (instruction->opcode) {
		case OPCODE_I32_LOAD:
		case OPCODE_I32_LOAD8_S:
		case OPCODE_F32_LOAD:
		case OPCODE_F64_LOAD:
		case OPCODE_I64_LOAD: {
			unsigned valtype;

			/* LOGIC: push_stack(data[ea - 4]) */
			switch (instruction->opcode) {
			case OPCODE_I32_LOAD8_S:
				/* movsbl -4(%rax, %rsi), %eax */
				OUTS("\x0f\xbe\x44\x30\xfc");
				valtype = STACK_I32;
				break;
			case OPCODE_I32_LOAD:
			case OPCODE_F32_LOAD:
				/* movl -4(%rax, %rsi), %eax */
				OUTS("\x8b\x44\x30\xfc");
				switch (instruction->opcode) {
				case OPCODE_I32_LOAD: valtype = STACK_I32; break;
				case OPCODE_F32_LOAD: valtype = STACK_F32; break;
				default: assert(0); __builtin_unreachable(); break;
				}
				break;
			case OPCODE_I64_LOAD:
			case OPCODE_F64_LOAD:
				/* movq -4(%rax, %rsi), %rax */
				OUTS("\x48\x8b\x44\x30\xfc");
				switch (instruction->opcode) {
				case OPCODE_I64_LOAD: valtype = STACK_I64; break;
				case OPCODE_F64_LOAD: valtype = STACK_F64; break;
				default: assert(0); __builtin_unreachable(); break;
				}
				break;
			default:
				assert(0);
				__builtin_unreachable();
				break;
			}

			/* push %rax */
			OUTS("\x50");
			if (!push_stack(sstack, valtype))
				goto error;

			break;
		}
		case OPCODE_I32_STORE:
		case OPCODE_F32_STORE:
			/* LOGIC: data[ea - 4] = pop_stack() */
			/* movl %edi, -4(%rax, %rsi) */
			OUTS("\x89\x7c\x30\xfc");
			break;
		case OPCODE_I32_STORE8:
			/* LOGIC: data[ea - 4] = pop_stack() */
			/* movb %dil, -4(%rax, %rsi) */
			OUTS("\x40\x88\x7c\x30\xfc");
			break;
		case OPCODE_I32_STORE16:
			/* movw %di, -4(%rax, %rsi) */
			OUTS("\x66\x89\x7c\x30\xfc");
			break;
		case OPCODE_I64_STORE:
		case OPCODE_F64_STORE:
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
		/* mov $value, %eax */
		OUTS("\xb8");
		encode_le_uint32_t(instruction->data.i32_const.value,
				   buf);
		if (!output_buf(output, buf, sizeof(uint32_t)))
			goto error;

		/* push %rax */
		OUTS("\x50");

		push_stack(sstack, STACK_I32);
		break;
	case OPCODE_I64_CONST:
		/* movq $value, %rax */
		OUTS("\x48\xb8");
		encode_le_uint64_t(instruction->data.i64_const.value,
				   buf);
		if (!output_buf(output, buf, sizeof(uint64_t)))
			goto error;

		/* push %rax */
		OUTS("\x50");

		push_stack(sstack, STACK_I64);
		break;
	case OPCODE_F32_CONST: {
		uint32_t bitrepr;
		/* mov $value, %eax */
		OUTS("\xb8");
#ifndef	IEC559_FLOAT_ENCODING
#error We dont support non-IEC 449 floats
#endif

		memcpy(&bitrepr, &instruction->data.f32_const.value,
		       sizeof(uint32_t));

		encode_le_uint32_t(bitrepr, buf);
		if (!output_buf(output, buf, sizeof(uint32_t)))
			goto error;

		/* push %rax */
		OUTS("\x50");

		push_stack(sstack, STACK_F32);
		break;
	}
	case OPCODE_F64_CONST: {
		uint64_t bitrepr;
		/* movq $value, %rax */
		OUTS("\x48\xb8");
#ifndef	IEC559_FLOAT_ENCODING
#error We dont support non-IEC 449 floats
#endif

		memcpy(&bitrepr, &instruction->data.f64_const.value,
		       sizeof(uint64_t));

		encode_le_uint64_t(bitrepr, buf);
		if (!output_buf(output, buf, sizeof(uint64_t)))
			goto error;

		/* push %rax */
		OUTS("\x50");

		push_stack(sstack, STACK_F64);
		break;
	}
	case OPCODE_I32_EQZ:
		assert(peek_stack(sstack) == STACK_I32);
		/* xor %eax, %eax */
		OUTS("\x31\xc0");
		/* cmpl $0, (%rsp) */
		OUTS("\x83\x3c\x24");
		OUTB(0);
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
	case OPCODE_I32_LE_S:
	case OPCODE_I32_LE_U:
	case OPCODE_I32_GE_S:
	case OPCODE_I32_GE_U:
	case OPCODE_I64_EQ:
	case OPCODE_I64_NE:
	case OPCODE_I64_LT_S:
	case OPCODE_I64_LT_U:
	case OPCODE_I64_GT_U: {
		unsigned stack_type;

		switch (instruction->opcode) {
		case OPCODE_I64_EQ:
		case OPCODE_I64_NE:
		case OPCODE_I64_LT_S:
		case OPCODE_I64_LT_U:
		case OPCODE_I64_GT_U:
			stack_type = STACK_I64;
			break;
		default:
			stack_type = STACK_I32;
			break;
		}

		assert(peek_stack(sstack) == stack_type);
		pop_stack(sstack);

		assert(peek_stack(sstack) == stack_type);
		pop_stack(sstack);

		/* popq %rdi */
		OUTS("\x5f");

		/* xor %(e|r)ax, %(e|r)ax */
		if (stack_type == STACK_I64)
			OUTS("\x48");
		OUTS("\x31\xc0");

		/* cmp %(r|e)di, (%rsp) */
		if (stack_type == STACK_I64)
			OUTS("\x48");
		OUTS("\x39\x3c\x24");

		switch (instruction->opcode) {
		case OPCODE_I32_EQ:
		case OPCODE_I64_EQ:
			/* sete %al */
			OUTS("\x0f\x94\xc0");
			break;
		case OPCODE_I32_NE:
		case OPCODE_I64_NE:
			OUTS("\x0f\x95\xc0");
			break;
		case OPCODE_I32_LT_S:
		case OPCODE_I64_LT_S:
			/* setl %al */
			OUTS("\x0f\x9c\xc0");
			break;
		case OPCODE_I32_LT_U:
		case OPCODE_I64_LT_U:
			/* setb %al */
			OUTS("\x0f\x92\xc0");
			break;
		case OPCODE_I32_GT_S:
			/* setg %al */
			OUTS("\x0f\x9f\xc0");
			break;
		case OPCODE_I32_GT_U:
		case OPCODE_I64_GT_U:
			/* seta %al */
			OUTS("\x0f\x97\xc0");
			break;
		case OPCODE_I32_LE_S:
			/* setle  %al */
			OUTS("\x0f\x9e\xc0");
			break;
		case OPCODE_I32_LE_U:
			/* setbe %al */
			OUTS("\x0f\x96\xc0");
			break;
		case OPCODE_I32_GE_S:
			/* setge %al */
			OUTS("\x0f\x9d\xc0");
			break;
		case OPCODE_I32_GE_U:
			/* setae %al */
			OUTS("\x0f\x93\xc0");
			break;
		default:
			assert(0);
			break;
		}

		/* mov %(r|e)ax, (%rsp) */
		if (stack_type == STACK_I64)
			OUTS("\x48");
		OUTS("\x89\x04\x24");

		push_stack(sstack, STACK_I32);
		break;
	}
	case OPCODE_F64_EQ:
	case OPCODE_F64_NE: {
		assert(peek_stack(sstack) == STACK_F64);
		pop_stack(sstack);

		assert(peek_stack(sstack) == STACK_F64);
		pop_stack(sstack);

		/* movsd (%rsp), %xmm0 */
		OUTS("\xf2\x0f\x10\x04\x24");
		/* add $8, %rsp */
		OUTS("\x48\x83\xc4\x08");
		/* xor %eax, %eax */
		OUTS("\x31\xc0");

		switch (instruction->opcode) {
		case OPCODE_F64_EQ:
			/* xor %edx, %edx */
			OUTS("\x31\xd2");
			break;
		case OPCODE_F64_NE:
			/* mov $1, %edx */
			OUTS("\xba\x01");
			OUTB(0); OUTB(0); OUTB(0);
			break;
		}

		/* ucomisd (%rsp), %xmm0 */
		OUTS("\x66\x0f\x2e\x04\x24");

		switch (instruction->opcode) {
		case OPCODE_F64_EQ:
			/* setnp %al */
			OUTS("\x0f\x9b\xc0");
			/* cmovne %edx, %eax */
			OUTS("\x0f\x45\xc2");
			break;
		case OPCODE_F64_NE:
			/* setp %al */
			OUTS("\x0f\x9a\xc0");
			/* cmovne %edx, %eax */
			OUTS("\x0f\x45\xc2");
			break;
		}

		/* mov %rax, (%rsp) */
		OUTS("\x48\x89\x04\x24");

		push_stack(sstack, STACK_I32);

		break;
	}
	case OPCODE_I32_SUB:
	case OPCODE_I32_ADD:
	case OPCODE_I32_MUL:
	case OPCODE_I32_AND:
	case OPCODE_I32_OR:
	case OPCODE_I32_XOR:
	case OPCODE_I64_ADD:
	case OPCODE_I64_SUB:
	case OPCODE_I64_MUL:
	case OPCODE_I64_AND:
	case OPCODE_I64_OR:
	case OPCODE_I64_XOR: {
		unsigned stack_type;

		switch (instruction->opcode) {
		case OPCODE_I64_ADD:
		case OPCODE_I64_SUB:
		case OPCODE_I64_MUL:
		case OPCODE_I64_AND:
		case OPCODE_I64_OR:
		case OPCODE_I64_XOR:
			stack_type = STACK_I64;
			break;
		default:
			stack_type = STACK_I32;
			break;
		}

		/* popq %rax */
		assert(peek_stack(sstack) == stack_type);
		pop_stack(sstack);
		OUTS("\x58");

		assert(peek_stack(sstack) == stack_type);

		if (stack_type == STACK_I64)
			OUTS("\x48");

		switch (instruction->opcode) {
		case OPCODE_I32_SUB:
		case OPCODE_I64_SUB:
			/* sub    %(r|e)ax,(%rsp) */
			OUTS("\x29\x04\x24");
			break;
		case OPCODE_I64_ADD:
		case OPCODE_I32_ADD:
			/* add    %eax,(%rsp) */
			OUTS("\x01\x04\x24");
			break;
		case OPCODE_I32_MUL:
		case OPCODE_I64_MUL:
			/* mul(q|l) (%rsp) */
			OUTS("\xf7\x24\x24");
			if (stack_type == STACK_I64)
				OUTS("\x48");
			/* mov    %(r|e)ax,(%rsp) */
			OUTS("\x89\x04\x24");
			break;
		case OPCODE_I32_AND:
		case OPCODE_I64_AND:
			/* and    %eax,(%rsp) */
			OUTS("\x21\x04\x24");
			break;
		case OPCODE_I32_OR:
		case OPCODE_I64_OR:
			/* or    %eax,(%rsp) */
			OUTS("\x09\x04\x24");
			break;
		case OPCODE_I32_XOR:
		case OPCODE_I64_XOR:
			/* xor    %eax,(%rsp) */
			OUTS("\x31\x04\x24");
			break;
		default:
			assert(0);
			break;
		}

		break;
	}
	case OPCODE_I32_DIV_S:
	case OPCODE_I32_DIV_U:
	case OPCODE_I32_REM_S:
	case OPCODE_I32_REM_U:
	case OPCODE_I64_DIV_S:
	case OPCODE_I64_DIV_U:
	case OPCODE_I64_REM_S:
	case OPCODE_I64_REM_U: {
		unsigned stack_type;
		switch (instruction->opcode) {
		case OPCODE_I32_DIV_S:
		case OPCODE_I32_DIV_U:
		case OPCODE_I32_REM_S:
		case OPCODE_I32_REM_U:
			stack_type = STACK_I32;
			break;
		case OPCODE_I64_DIV_S:
		case OPCODE_I64_DIV_U:
		case OPCODE_I64_REM_S:
		case OPCODE_I64_REM_U:
			stack_type = STACK_I64;
			break;
		default:
			assert(0);
			__builtin_unreachable();
			break;
		}

		assert(peek_stack(sstack) == stack_type);
		pop_stack(sstack);

		assert(peek_stack(sstack) == stack_type);

		/* pop %rdi */
		OUTS("\x5f");

		/* mov (%rsp), %(r|e)ax */
		if (stack_type == STACK_I64)
			OUTS("\x48");
		OUTS("\x8b\x04\x24");

		if (stack_type == STACK_I64)
			OUTS("\x48");

		switch (instruction->opcode) {
		case OPCODE_I32_DIV_S:
		case OPCODE_I32_REM_S:
		case OPCODE_I64_DIV_S:
		case OPCODE_I64_REM_S:
			/* cld|cqto */
			OUTS("\x99");
			/* idiv %(r|e)di */
			if (stack_type == STACK_I64)
				OUTS("\x48");
			OUTS("\xf7\xff");
			break;
		case OPCODE_I32_DIV_U:
		case OPCODE_I32_REM_U:
		case OPCODE_I64_DIV_U:
		case OPCODE_I64_REM_U:
			/* xor %(r|e)dx, %(r|e)dx */
			OUTS("\x31\xd2");
			/* div %(r|e)di */
			if (stack_type == STACK_I64)
				OUTS("\x48");
			OUTS("\xf7\xf7");
			break;
		}

		if (stack_type == STACK_I64)
			OUTS("\x48");

		switch (instruction->opcode) {
		case OPCODE_I32_REM_S:
		case OPCODE_I32_REM_U:
		case OPCODE_I64_REM_S:
		case OPCODE_I64_REM_U:
			/* mov %(r|e)dx, (%rsp) */
			OUTS("\x89\x14\x24");
			break;
		default:
			/* mov %(e|r)ax, (%rsp) */
			OUTS("\x89\x04\x24");
			break;
		}

		break;
	}
	case OPCODE_I32_SHL:
	case OPCODE_I32_SHR_S:
	case OPCODE_I32_SHR_U:
	case OPCODE_I64_SHL:
	case OPCODE_I64_SHR_S:
	case OPCODE_I64_SHR_U: {
		unsigned stack_type;

		switch (instruction->opcode) {
		case OPCODE_I64_SHL:
		case OPCODE_I64_SHR_S:
		case OPCODE_I64_SHR_U:
			stack_type = STACK_I64;
			break;
		default:
			stack_type = STACK_I32;
			break;
		}

		/* pop %rcx */
		OUTS("\x59");
		assert(peek_stack(sstack) == stack_type);
		pop_stack(sstack);

		assert(peek_stack(sstack) == stack_type);

		if (stack_type == STACK_I64)
			OUTS("\x48");

		switch (instruction->opcode) {
		case OPCODE_I32_SHL:
		case OPCODE_I64_SHL:
			/* shl(l|q)   %cl,(%rsp) */
			OUTS("\xd3\x24\x24");
			break;
		case OPCODE_I32_SHR_S:
		case OPCODE_I64_SHR_S:
			/* sar(l|q) %cl, (%rsp) */
			OUTS("\xd3\x3c\x24");
			break;
		case OPCODE_I32_SHR_U:
		case OPCODE_I64_SHR_U:
			/* shr(l|q) %cl, (%rsp) */
			OUTS("\xd3\x2c\x24");
			break;
		}

		break;
	}
	case OPCODE_F64_NEG:
		assert(peek_stack(sstack) == STACK_F64);
		/* btcq   $0x3f,(%rsp)  */
		OUTS("\x48\x0f\xba\x3c\x24\x3f");
		break;
	case OPCODE_F64_ADD:
	case OPCODE_F64_SUB:
	case OPCODE_F64_MUL:
		assert(peek_stack(sstack) == STACK_F64);
		pop_stack(sstack);

 		assert(peek_stack(sstack) == STACK_F64);

		/* movsd (%rsp), %xmm0 */
		OUTS("\xf2\x0f\x10\x04\x24");
		/* add $8, %rsp */
		OUTS("\x48\x83\xc4\x08");

		switch (instruction->opcode) {
		case OPCODE_F64_ADD:
			/* addsd (%rsp), %xmm0 */
			OUTS("\xf2\x0f\x58\x04\x24");
			break;
		case OPCODE_F64_SUB:
			/* subsd (%rsp), %xmm0 */
			OUTS("\xf2\x0f\x5c\x04\x24");
			break;
		case OPCODE_F64_MUL:
			/* mulsd (%rsp), %xmm0 */
			OUTS("\xf2\x0f\x59\x04\x24");
			break;
		default:
			assert(0);
			break;
		}
		/* movsd %xmm0,(%rsp) */
		OUTS("\xf2\x0f\x11\x04\x24");
		break;
	case OPCODE_I32_WRAP_I64:
		assert(peek_stack(sstack) == STACK_I64);
		pop_stack(sstack);

		/* mov $0xffffffff,%eax */
		OUTS("\xb8\xff\xff\xff\xff");
		/* and %rax,(%rsp) */
		OUTS("\x48\x21\x04\x24");

		if (!push_stack(sstack, STACK_I32))
			goto error;

		break;
	case OPCODE_I32_TRUNC_U_F64:
	case OPCODE_I32_TRUNC_S_F64:
		assert(peek_stack(sstack) == STACK_F64);
		pop_stack(sstack);

		/* cvttsd2si (%rsp), %eax */
		OUTS("\xf2\x0f\x2c\x04\x24");

		/* mov %rax, (%rsp) */
		OUTS("\x48\x89\x04\x24");

		if (!push_stack(sstack, STACK_I32))
			goto error;
		break;
	case OPCODE_I64_EXTEND_S_I32:
		assert(peek_stack(sstack) == STACK_I32);
		pop_stack(sstack);

		/* movsxl (%rsp), %rax */
		OUTS("\x48\x63\x04\x24");
		/* mov %rax, (%rsp) */
		OUTS("\x48\x89\x04\x24");

		if (!push_stack(sstack, STACK_I64))
			goto error;

		break;
	case OPCODE_I64_EXTEND_U_I32:
		assert(peek_stack(sstack) == STACK_I32);
		pop_stack(sstack);

		/* NB: don't need to do anything,
		   we store 32-bits as zero-extended 64-bits
		 */

		if (!push_stack(sstack, STACK_I64))
			goto error;
		break;
	case OPCODE_F64_CONVERT_S_I32:
	case OPCODE_F64_CONVERT_U_I32:
		assert(peek_stack(sstack) == STACK_I32);
		pop_stack(sstack);

		switch (instruction->opcode) {
		case OPCODE_F64_CONVERT_S_I32:
			/* cvtsi2sdl (%rsp),%xmm0 */
			OUTS("\xf2\x0f\x2a\x04\x24");
			break;
		case OPCODE_F64_CONVERT_U_I32:
			/* mov (%rsp), %eax */
			OUTS("\x8b\x04\x24");
			/* cvtsi2sd %rax,%xmm0 */
			OUTS("\xf2\x48\x0f\x2a\xc0");
			break;
		}

		/* movsd %xmm0,(%rsp) */
		OUTS("\xf2\x0f\x11\x04\x24");

		if (!push_stack(sstack, STACK_F64))
			goto error;
		break;
	case OPCODE_F64_PROMOTE_F32:
		assert(peek_stack(sstack) == STACK_F32);
		pop_stack(sstack);


		/* movd (%rsp), %xmm0 */
		OUTS("\x66\x0f\x6e\x04\x24");

		/* cvtss2sd %xmm0, %xmm1 */
		OUTS("\xf3\x0f\x5a\xc8");

		/* movsd %xmm1, (%rsp) */
		OUTS("\xf2\x0f\x11\x0c\x24");


		if (!push_stack(sstack, STACK_F64))
			goto error;

		break;
	case OPCODE_I64_REINTERPRET_F64:
		assert(peek_stack(sstack) == STACK_F64);
		pop_stack(sstack);

		/* no need to do anything */

		if (!push_stack(sstack, STACK_I64))
			goto error;
		break;
	case OPCODE_F64_REINTERPRET_I64:
		assert(peek_stack(sstack) == STACK_I64);
		pop_stack(sstack);

		/* no need to do anything */

		if (!push_stack(sstack, STACK_F64))
			goto error;
		break;
	default:
#ifndef __KERNEL__
		fprintf(stderr, "Unhandled Opcode: 0x%" PRIx8 "\n", instruction->opcode);
#endif
		assert(0);
		break;
	}

	return 1;

 error:
	assert(0);
	return 0;

}


static int wasmjit_compile_instructions(const struct FuncType *func_types,
					const struct ModuleTypes *module_types,
					const struct FuncType *type,
					struct SizedBuffer *output,
					struct LabelContinuations *labels,
					struct BranchPoints *branches,
					struct MemoryReferences *memrefs,
					struct LocalsMD *locals_md,
					size_t n_locals,
					size_t n_frame_locals,
					struct StaticStack *sstack,
					const struct Instr *instructions,
					size_t n_instructions)
{
	int ret;
	size_t i;
	size_t stack_sz;
	struct InstructionMD *stack;

	stack_sz = 1;
	stack = malloc(stack_sz * sizeof(stack[0]));
	if (!stack)
		goto error;

	stack[0].instructions = instructions;
	stack[0].n_instructions = n_instructions;
	stack[0].initiator = NULL;
	stack[0].cont = 0;

	while (stack_sz) {
		struct InstructionMD imd, *new_stack;

		/* pop instruction stream off stack */

		stack_sz -= 1;
		memcpy(&imd, &stack[stack_sz], sizeof(imd));
		new_stack = realloc(stack, stack_sz * sizeof(stack[0]));
		if (!new_stack && stack_sz)
			goto error;
		stack = new_stack;

		for (i = imd.cont; i < imd.n_instructions; ++i) {
			struct InstructionMD imd2;
			const struct Instr *instruction = &imd.instructions[i];

			switch (instruction->opcode) {
			case OPCODE_BLOCK:
			case OPCODE_LOOP: {
				size_t arity;

				arity = instruction->data.block.blocktype !=
					VALTYPE_NULL ? 1 : 0;

				imd2.data.block.label_idx = labels->n_elts;
				INC_LABELS();

				imd2.data.block.stack_idx = sstack->n_elts;
				if (!stack_grow(sstack, 1))
					goto error;

				{
					struct StackElt *elt =
						&sstack->elts[imd2.data.block.stack_idx];
					elt->type = STACK_LABEL;
					elt->data.label.arity = arity;
					elt->data.label.continuation_idx = imd2.data.block.label_idx;
				}

				imd2.data.block.output_idx = output->n_elts;

				imd2.instructions = instruction->data.block.instructions;
				imd2.n_instructions = instruction->data.block.n_instructions;
				break;
			}
			case OPCODE_IF: {
				int arity =
					instruction->data.if_.blocktype !=
					VALTYPE_NULL ? 1 : 0;

				/* test top of stack */
				assert(peek_stack(sstack) == STACK_I32);
				pop_stack(sstack);
				/* pop %rax */
				OUTS("\x58");

				/* if not true jump to else case */
				/* test %eax, %eax */
				OUTS("\x85\xc0");

				imd2.data.if_.jump_to_else_offset = output->n_elts + 2;
				/* je else_offset */
				OUTS("\x0f\x84\x90\x90\x90\x90");

				/* output then case */
				imd2.data.if_.label_idx = labels->n_elts;
				INC_LABELS();

				imd2.data.if_.stack_idx = sstack->n_elts;
				if (!stack_grow(sstack, 1))
					goto error;

				{
					struct StackElt *elt =
						&sstack->elts[imd2.data.if_.stack_idx];
					elt->type = STACK_LABEL;
					elt->data.label.arity = arity;
					elt->data.label.continuation_idx = imd2.data.if_.label_idx;
				}

				imd2.data.if_.did_else = 0;

				imd2.instructions = instruction->data.if_.instructions_then;
				imd2.n_instructions = instruction->data.if_.n_instructions_then;
				break;
			}
			default:
				if (!wasmjit_compile_instruction(func_types,
								 module_types,
								 type,
								 output,
								 branches,
								 memrefs,
								 locals_md,
								 n_locals,
								 n_frame_locals,
								 sstack,
								 instruction))
					goto error;
				break;
			}

			if (instruction->opcode == OPCODE_BLOCK ||
			    instruction->opcode == OPCODE_LOOP ||
			    instruction->opcode == OPCODE_IF) {
				/* push onto stack, break loop */

				stack_sz += 2;
				new_stack = realloc(stack, stack_sz * sizeof(stack[0]));
				if (!new_stack)
					goto error;
				stack = new_stack;

				imd.cont = i + 1;
				memcpy(&stack[stack_sz - 2], &imd, sizeof(imd));

				imd2.initiator = instruction;
				imd2.cont = 0;
				memcpy(&stack[stack_sz - 1], &imd2, sizeof(imd2));

				break;
			}
		}

		if (i != imd.n_instructions)
			continue;

		/* do footer logic */
		if (imd.initiator) {
			const struct Instr *instruction = imd.initiator;
			switch (instruction->opcode) {
			case OPCODE_BLOCK:
			case OPCODE_LOOP: {
				size_t arity =
					instruction->data.block.blocktype !=
					VALTYPE_NULL ? 1 : 0;

				/* shift stack results over label */
				memmove(&sstack->elts[imd.data.block.stack_idx],
					&sstack->elts[sstack->n_elts - arity],
					arity * sizeof(sstack->elts[0]));
				if (!stack_truncate(sstack, imd.data.block.stack_idx + arity))
					goto error;

				switch (instruction->opcode) {
				case OPCODE_BLOCK:
					labels->elts[imd.data.block.label_idx] =
						output->n_elts;
					break;
				case OPCODE_LOOP:
					labels->elts[imd.data.block.label_idx] =
						imd.data.block.output_idx;
					break;
				default:
					assert(0);
					break;
				}

				break;
			}
			case OPCODE_IF: {
				size_t arity =
					instruction->data.if_.blocktype !=
					VALTYPE_NULL ? 1 : 0;

				if (!imd.data.if_.did_else) {
					/* if (else_exist) {
					   jump after else
					   }
					*/
					if (instruction->data.if_.n_instructions_else) {
						imd.data.if_.jump_to_after_else_offset = output->n_elts + 1;
						/* jmp after_else_offset */
						OUTS("\xe9\x90\x90\x90\x90");
					}

					/* fix up jump_to_else_offset */
					imd.data.if_.jump_to_else_offset = output->n_elts - imd.data.if_.jump_to_else_offset;
					encode_le_uint32_t(imd.data.if_.jump_to_else_offset - 4,
							   &output->elts[output->n_elts - imd.data.if_.jump_to_else_offset]);

				} else {
					/* fix up jump_to_after_else_offset */
					imd.data.if_.jump_to_after_else_offset = output->n_elts - imd.data.if_.jump_to_after_else_offset;
					encode_le_uint32_t(imd.data.if_.jump_to_after_else_offset - 4,
							   &output->elts[output->n_elts - imd.data.if_.jump_to_after_else_offset]);
				}

				if (!imd.data.if_.did_else &&
				    instruction->data.if_.n_instructions_else) {
					/* if (else_exist) {
					   output else case
					   }
					*/

					/* push else chain onto stack */
					stack_sz += 1;
					new_stack = realloc(stack, stack_sz * sizeof(stack[0]));
					if (!new_stack)
						goto error;
					stack = new_stack;

					imd.cont = 0;
					imd.instructions = instruction->data.if_.instructions_else;
					imd.n_instructions = instruction->data.if_.n_instructions_else;
					imd.data.if_.did_else = 1;

					memcpy(&stack[stack_sz - 1],
					       &imd,
					       sizeof(imd));
				} else {
					/* fix up static stack */
					/* shift stack results over label */
					memmove(&sstack->elts[imd.data.if_.stack_idx],
						&sstack->elts[sstack->n_elts - arity],
						arity * sizeof(sstack->elts[0]));
					if (!stack_truncate(sstack, imd.data.if_.stack_idx + arity))
						goto error;

					/* set labels position */
					labels->elts[imd.data.if_.label_idx] = output->n_elts;
				}
				break;
			}
			}
		}
	}

	ret = 1;

	if (0) {
	error:
		ret = 0;
	}

	if (stack)
		free(stack);

	return ret;
}

char *wasmjit_compile_function(const struct FuncType *func_types,
			       const struct ModuleTypes *module_types,
			       const struct FuncType *type,
			       const struct CodeSectionCode *code,
			       struct MemoryReferences *memrefs, size_t *out_size)
{
	char buf[sizeof(uint32_t)];
	struct SizedBuffer outputv = { 0, NULL };
	struct SizedBuffer *output = &outputv;
	struct BranchPoints branches = { 0, NULL };
	struct StaticStack sstack = { 0, NULL };
	struct LabelContinuations labels = { 0, NULL };
	struct LocalsMD *locals_md = NULL;
	size_t n_frame_locals;
	size_t n_locals;
	char *out;

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
		if (n_locals && !locals_md)
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
				int32_t off = 2 * 8;
				int32_t si; /* (n_stack + 2) * 8) */
				if (__builtin_mul_overflow(n_stack, 8, &si))
					goto error;
				if (__builtin_add_overflow(si, off, &si))
					goto error;
				locals_md[i].fp_offset = si;
				n_stack += 1;
			}
			locals_md[i].valtype = type->input_types[i];
		}

		for (i = 0; i < n_locals - type->n_inputs; ++i) {
			int32_t off = -(1 + n_movs + n_xmm_movs) * 8;
			int32_t si; /* -(1 + n_movs + n_xmm_movs + i) * 8; */
			if (__builtin_mul_overflow(-8, i, &si))
				goto error;
			if (__builtin_add_overflow(si, off, &si))
				goto error;
			locals_md[i + type->n_inputs].fp_offset = si;
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

		if (n_locals - type->n_inputs > SIZE_MAX - (n_movs + n_xmm_movs))
			goto error;
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
			int32_t out;
			OUTS("\x48\x81\xec");
			if (__builtin_mul_overflow(n_frame_locals, 8, &out))
				goto error;
			encode_le_uint32_t(out, buf);
			if (!output_buf(output, buf, sizeof(uint32_t)))
				goto error;
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
				/* mov %rsp, %rdi */
				OUTS("\x48\x89\xe7");
				/* xor %rax, %rax */
				OUTS("\x48\x31\xc0");
				/* mov $n_locals, %rcx */
				OUTS("\x48\xc7\xc1");
				if (n_locals - type->n_inputs > INT32_MAX)
					goto error;
				encode_le_uint32_t(n_locals - type->n_inputs, buf);
				if (!output_buf(output, buf, sizeof(uint32_t)))
					goto error;
				/* rep stosq */
				OUTS("\xf3\x48\xab");
			}
		}
	}

	if (!wasmjit_compile_instructions(func_types, module_types, type,
					  output, &labels, &branches, memrefs,
					  locals_md, n_locals, n_frame_locals, &sstack,
					  code->instructions, code->n_instructions))
		goto error;

	/* fix branch points */
	{
		size_t i;
		for (i = 0; i < branches.n_elts; ++i) {
			char buf2[1 + sizeof(uint32_t)] = { 0xe9 };
			struct BranchPointElt *branch = &branches.elts[i];
			size_t continuation_offset = (branch->continuation_idx == FUNC_EXIT_CONT)
				? output->n_elts
				: labels.elts[branch->continuation_idx];
			uint32_t rel =
			    continuation_offset - branch->branch_offset -
			    sizeof(buf2);
			encode_le_uint32_t(rel, &buf2[1]);
			memcpy(&output->elts[branch->branch_offset], buf2,
			       sizeof(buf2));
		}
	}

	/* output epilogue */
	assert(sstack.n_elts == FUNC_TYPE_N_OUTPUTS(type));

	if (FUNC_TYPE_N_OUTPUTS(type)) {
		assert(FUNC_TYPE_N_OUTPUTS(type) == 1);
		assert(peek_stack(&sstack) == FUNC_TYPE_OUTPUT_TYPES(type)[0]);
		pop_stack(&sstack);

		/* mov to xmm0 if float return */
		if (FUNC_TYPE_OUTPUT_TYPES(type)[0] == VALTYPE_F32) {
			/* movss (%rsp), %xmm0 */
			OUTS("\xf3\x0f\x10\x04\x24");
			/* add $8, %rsp */
			OUTS("\x48\x8d\x47\x08");
		} else if (FUNC_TYPE_OUTPUT_TYPES(type)[0] == VALTYPE_F64) {
			/* movsd (%rsp), %xmm0 */
			OUTS("\xf2\x0f\x10\x04\x24");
			/* add $8, %rsp */
			OUTS("\x48\x83\xc4\x08");
		} else {
			/* pop %rax */
			OUTS("\x58");
		}
	}

	/* add $(8 * (n_frame_locals)), %rsp */
	if (n_frame_locals) {
		int32_t out;
		OUTS("\x48\x81\xc4");
		if (__builtin_mul_overflow(n_frame_locals, 8, &out))
			goto error;
		encode_le_uint32_t(out, buf);
		if (!output_buf(output, buf, sizeof(uint32_t)))
			goto error;
	}

	/* pop %rbp */
	OUTS("\x5d");

	/* retq */
	OUTS("\xc3");

	if (0) {
	error:
		free(output->elts);
		out = NULL;
	}
	else {
		out = output->elts;
		if (out_size)
			*out_size = output->n_elts;
	}

	if (locals_md) {
		free(locals_md);
	}

	if (branches.elts) {
		free(branches.elts);
	}

	if (sstack.elts) {
		free(sstack.elts);
	}

	if (labels.elts) {
		free(labels.elts);
	}

	return out;
}

char *wasmjit_compile_hostfunc(struct FuncType *type,
			       void *hostfunc,
			       void *funcinst_ptr,
			       size_t *out_size)
{
	char *out;
	char *movabs_str = NULL;
	size_t i;
	size_t n_movs = 0, n_xmm_movs = 0, n_stack = 0;

	/* count integer inputs */
	for (i = 0; i < type->n_inputs; ++i) {
		if ((type->input_types[i] == VALTYPE_I32 ||
		     type->input_types[i] == VALTYPE_I64) &&
		    n_movs < 6) {
			n_movs += 1;
		} else if ((type->input_types[i] == VALTYPE_F32 ||
			    type->input_types[i] == VALTYPE_F64) &&
			   n_xmm_movs < 8) {
			n_xmm_movs += 1;
		} else {
			n_stack += 1;
		}
	}

	switch (n_movs) {
	case 0:
		/* mov $const, %rdi */
		movabs_str = "\x48\xbf";
		break;
	case 1:
		/* mov $const, %rsi */
		movabs_str = "\x48\xbe";
		break;
	case 2:
		/* mov $const, %rdx */
		movabs_str = "\x48\xba";
		break;
	case 3:
		/* mov $const, %rcx */
		movabs_str = "\x48\xb9";
		break;
	case 4:
		/* mov $const, %r8 */
		movabs_str = "\x49\xb8";
		break;
	case 5:
		/* mov $const, %r9 */
		movabs_str = "\x49\xb9";
		break;
	default: {
		size_t off = 0;
		*out_size =
			/* align stack */
			((n_stack % 2) ? 4 : 0) +
			/* push value */
			10 + 1 +
			/* push old stack values */
			7 * n_stack +
			/* mov hostfunc to %rax and call there */
			10 + 2 +
			/* cleanup stack */
			7 +
			/* ret */
			1;
		out = malloc(*out_size);
		if (!out)
			break;

		/* keep stack aligned to 0x10 */
		if (n_stack % 2) {
			/* sub $0x8, %rsp */
			memcpy(out + off, "\x48\x83\xec\x08", 4);
			off += 4;
		}

		/* push value */
		/* mov $const, %rax */
		memcpy(out + off, "\x48\xb8", 2);
		off += 2;
		encode_le_uint64_t((uintptr_t) funcinst_ptr, out + off);
		off += 8;
		/* push %rax */
		memcpy(out + off, "\x50", 1);
		off += 1;

		/* repeat for each stack arg  */
		for (i = 0; i < n_stack; ++i) {
			/* push subbed_space + 8 * (n_stack_args + 1)(%rsp)*/
			memcpy(out + off, "\xff\xb4\x24", 3);
			off += 3;
			encode_le_uint32_t(8 * ((n_stack % 2) + n_stack + 1),
					   out + off);
			off += 4;
		}

		/* mov $const, %rax */
		memcpy(out + off, "\x48\xb8", 2);
		off += 2;
		encode_le_uint64_t((uintptr_t) hostfunc, out + off);
		off += 8;

		/* call *%rax */
		memcpy(out + off, "\xff\xd0", 2);
		off += 2;

		/* cleanup stack */
		memcpy(out + off, "\x48\x81\xc4", 3);
		off += 3;
		encode_le_uint32_t(((n_stack % 2) + 1 + n_stack) * 8,
				   out + off);
		off += 4;

		memcpy(out + off, "\xc3", 1);
		off += 1;

		assert(off == *out_size);

		break;
	}
	}

	if (movabs_str) {
		assert(strlen(movabs_str) == 2);
		*out_size = 10 + 10 + 2;
		out = malloc(*out_size);
		if (!out)
			return NULL;
		memcpy(out, movabs_str, 2);
		encode_le_uint64_t((uintptr_t) funcinst_ptr, out + 2);
		/* movabs $const, %rax */
		memcpy(out + 10, "\x48\xb8", 2);
		encode_le_uint64_t((uintptr_t) hostfunc, out + 12);
		/* jmp *%rax */
		memcpy(out + 20, "\xff\xe0", 2);
	}

	return out;
}

char *wasmjit_compile_invoker(struct FuncType *type,
			      void *compiled_code,
			      size_t *out_size)
{
	size_t i;
	size_t n_movs = 0, n_xmm_movs = 0, n_stack = 0;
	struct SizedBuffer outputv = { 0, NULL };
	struct SizedBuffer *output = &outputv;
	char buf[sizeof(uint64_t)];
	void *out = NULL;
	int aligned;
	size_t to_reserve;

	for (i = 0; i < type->n_inputs; ++i) {
		if ((type->input_types[i] == VALTYPE_I32 ||
		     type->input_types[i] == VALTYPE_I64) &&
		    n_movs < 6) {
			n_movs += 1;
		} else if ((type->input_types[i] == VALTYPE_F32 ||
			    type->input_types[i] == VALTYPE_F64) &&
			   n_xmm_movs < 8) {
			n_xmm_movs += 1;
		} else {
			n_stack += 1;
		}
	}

	to_reserve = 1 + n_stack;
	aligned = !(to_reserve % 2);
	if (aligned) {
		to_reserve += 1;
	}

	/* sub to_reserve*8, %rsp */
	OUTS("\x48\x81\xec");
	encode_le_uint32_t(to_reserve * 8, buf);
	if (!output_buf(output, buf, sizeof(uint32_t)))
		goto error;

	/* mov %rbx, (to_reserve - 1) *8(%rsp), */
	OUTS("\x48\x89\x9c\x24");
	encode_le_uint32_t((to_reserve - 1) * 8, buf);
	if (!output_buf(output, buf, sizeof(uint32_t)))
		goto error;

	/* mov %rdi, %rbx */
	OUTS("\x48\x89\xfb");

	n_movs = 0;
	n_xmm_movs = 0;
	n_stack = 0;
	for (i = 0; i < type->n_inputs; ++i) {
		static const char *const movs[] = {
			"\x48\x8b\xbb", /* mov N(%rbx), %rdi */
			"\x48\x8b\xb3", /* mov N(%rbx), %rsi */
			"\x48\x8b\x93", /* mov N(%rbx), %rdx */
			"\x48\x8b\x8b", /* mov N(%rbx), %rcx */
			"\x4c\x8b\x83", /* mov N(%rbx), %r8 */
			"\x4c\x8b\x8b", /* mov N(%rbx), %r9 */
		};

		static const char *const f32_movs[] = {
			"\xf3\x0f\x10\x83", /* movss  N(%rbx),%xmm0 */
			"\xf3\x0f\x10\x8b", /* movss  N(%rbx),%xmm1 */
			"\xf3\x0f\x10\x93", /* movss  N(%rbx),%xmm2 */
			"\xf3\x0f\x10\x9b", /* movss  N(%rbx),%xmm3 */
			"\xf3\x0f\x10\xa3", /* movss  N(%rbx),%xmm4 */
			"\xf3\x0f\x10\xab", /* movss  N(%rbx),%xmm5 */
			"\xf3\x0f\x10\xb3", /* movss  N(%rbx),%xmm6 */
			"\xf3\x0f\x10\xbb", /* movss  N(%rbx),%xmm7 */
		};

		static const char *const f64_movs[] = {
			"\xf2\x0f\x10\x83", /* movsd  N(%rbx),%xmm0 */
			"\xf2\x0f\x10\x8b", /* movsd  N(%rbx),%xmm1 */
			"\xf2\x0f\x10\x93", /* movsd  N(%rbx),%xmm2 */
			"\xf2\x0f\x10\x9b", /* movsd  N(%rbx),%xmm3 */
			"\xf2\x0f\x10\xa3", /* movsd  N(%rbx),%xmm4 */
			"\xf2\x0f\x10\xab", /* movsd  N(%rbx),%xmm5 */
			"\xf2\x0f\x10\xb3", /* movsd  N(%rbx),%xmm6 */
			"\xf2\x0f\x10\xbb", /* movsd  N(%rbx),%xmm7 */
		};

		if ((type->input_types[i] == VALTYPE_I32 ||
		     type->input_types[i] == VALTYPE_I64) &&
		    n_movs < 6) {
			OUTS(movs[n_movs]);
			encode_le_uint32_t(i * -8, buf);
			if (!output_buf(output, buf, sizeof(uint32_t)))
				goto error;
			n_movs += 1;
		} else if ((type->input_types[i] == VALTYPE_F32 ||
			    type->input_types[i] == VALTYPE_F64) &&
			   n_xmm_movs < 8) {

			if (type->input_types[i] == VALTYPE_F32) {
				OUTS(f32_movs[n_movs]);
			} else {
				OUTS(f64_movs[n_movs]);
			}

			encode_le_uint32_t(i * -8, buf);
			if (!output_buf(output, buf, sizeof(uint32_t)))
				goto error;

			n_xmm_movs += 1;
		} else {
			/* mov (-8 * i)(%rbx), %rax */
			OUTS("\x48\x8b\x83");
			encode_le_uint32_t(i * -8, buf);
			if (!output_buf(output, buf, sizeof(uint32_t)))
				goto error;

			/* mov %rax, (n_stack * 8)(%rsp) */
			OUTS("\x48\x89\x84\x24");
			encode_le_uint32_t(n_stack * 8, buf);
			if (!output_buf(output, buf, sizeof(uint32_t)))
				goto error;

			n_stack += 1;
		}
	}

	/* movabs $const, %rax */
	OUTS("\x48\xb8");
	OUTNULL(8);
	encode_le_uint64_t((uintptr_t) compiled_code,
			   &output->elts[output->n_elts - 8]);

	/* call *%rax */
	OUTS("\xff\xd0");

	/* mov (to_reserve - 1) *8(%rsp), %rbx */
	OUTS("\x48\x8b\x9c\x24");
	encode_le_uint32_t((to_reserve - 1) * 8, buf);
	if (!output_buf(output, buf, sizeof(uint32_t)))
		goto error;

	/* clean up stack */
	if (to_reserve) {
		/* add $const, %rsp */
		OUTS("\x48\x81\xc4");
		encode_le_uint32_t(to_reserve * 8, buf);
		if (!output_buf(output, buf, sizeof(uint32_t)))
			goto error;
	}

	/* return */
	OUTS("\xc3");

	if (0) {
	error:
		free(output->elts);
		out = NULL;
	}
	else {
		out = output->elts;
		if (out_size)
			*out_size = output->n_elts;
	}

	return out;
}

#undef INC_LABELS
#undef OUTNULL
#undef OUTB
#undef OUTS
