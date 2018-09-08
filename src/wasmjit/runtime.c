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

#include <wasmjit/runtime.h>

#include <wasmjit/ast.h>
#include <wasmjit/util.h>

#include <wasmjit/sys.h>

DEFINE_VECTOR_GROW(func_types, struct FuncTypeVector);

/* platform specific */

#ifdef __KERNEL__

#include <linux/mm.h>
#include <linux/sched/task_stack.h>

void *wasmjit_map_code_segment(size_t code_size)
{
	return __vmalloc(code_size, GFP_KERNEL, PAGE_KERNEL_EXEC);
}

int wasmjit_mark_code_segment_executable(void *code, size_t code_size)
{
	/* TODO: mess with pte a la mprotect_fixup */
	(void)code;
	(void)code_size;
	return 1;
}

int wasmjit_unmap_code_segment(void *code, size_t code_size)
{
	(void)code_size;
	vfree(code);
	return 1;
}

static void *ptrptr(void) {
	/* NB: use space below entry of kernel stack for our jmp_buf pointer
	   if task_pt_regs(current) does not point to the bottom of the stack,
	   this will fail very badly. wasmjit_high_emscripten_invoke_main always
	   restores the original value before returning, so while we in the system
	   call it should be safe to reappropriate this space.
	 */
	return (char *)task_pt_regs(current) - sizeof(jmp_buf *);
}

jmp_buf *wasmjit_get_jmp_buf(void)
{
	jmp_buf *toret;
	memcpy(&toret, ptrptr(), sizeof(toret));
	return toret;
}

int wasmjit_set_jmp_buf(jmp_buf *jmpbuf)
{
	memcpy(ptrptr(), &jmpbuf, sizeof(jmpbuf));
	return 1;
}

#else

#include <wasmjit/tls.h>

#include <sys/mman.h>

void *wasmjit_map_code_segment(size_t code_size)
{
	void *newcode;
	newcode = mmap(NULL, code_size, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (newcode == MAP_FAILED)
		return NULL;
	return newcode;
}

int wasmjit_mark_code_segment_executable(void *code, size_t code_size)
{
	return !mprotect(code, code_size, PROT_READ | PROT_EXEC);
}


int wasmjit_unmap_code_segment(void *code, size_t code_size)
{
	return !munmap(code, code_size);
}

wasmjit_tls_key_t jmp_buf_key;

__attribute__((constructor))
static void _init_jmp_buf(void)
{
	wasmjit_init_tls_key(&jmp_buf_key, NULL);
}

jmp_buf *wasmjit_get_jmp_buf(void)
{
	jmp_buf *toret;
	int ret;
	ret = wasmjit_get_tls_key(jmp_buf_key, &toret);
	if (!ret) return NULL;
	return toret;
}

int wasmjit_set_jmp_buf(jmp_buf *jmpbuf)
{
	return wasmjit_set_tls_key(jmp_buf_key, jmpbuf);
}

#endif

/* end platform specific */

union ExportPtr wasmjit_get_export(const struct ModuleInst *module_inst,
				   const char *name,
				   wasmjit_desc_t type) {
	size_t i;
	union ExportPtr ret;

	for (i = 0; i < module_inst->exports.n_elts; ++i) {
		if (strcmp(module_inst->exports.elts[i].name, name))
			continue;

		if (module_inst->exports.elts[i].type != type)
			break;

		return module_inst->exports.elts[i].value;
	}

	switch (type) {
	case IMPORT_DESC_TYPE_FUNC:
		ret.func = NULL;
		break;
	case IMPORT_DESC_TYPE_TABLE:
		ret.table = NULL;
		break;
	case IMPORT_DESC_TYPE_MEM:
		ret.mem = NULL;
		break;
	case IMPORT_DESC_TYPE_GLOBAL:
		ret.global = NULL;
		break;
	}

	return ret;
}

void wasmjit_free_module_inst(struct ModuleInst *module)
{
	size_t i;
	free(module->types.elts);
	for (i = module->n_imported_funcs; i < module->funcs.n_elts; ++i) {
		if (module->funcs.elts[i]->compiled_code)
			wasmjit_unmap_code_segment(module->funcs.elts[i]->compiled_code,
						   module->funcs.elts[i]->compiled_code_size);
		free(module->funcs.elts[i]);
	}
	free(module->funcs.elts);
	for (i = module->n_imported_tables; i < module->tables.n_elts; ++i) {
		free(module->tables.elts[i]->data);
		free(module->tables.elts[i]);
	}
	free(module->tables.elts);
	for (i = module->n_imported_mems; i < module->mems.n_elts; ++i) {
		free(module->mems.elts[i]->data);
		free(module->mems.elts[i]);
	}
	free(module->mems.elts);
	for (i = module->n_imported_globals; i < module->globals.n_elts; ++i) {
		free(module->globals.elts[i]);
	}
	free(module->globals.elts);
	for (i = 0; i < module->exports.n_elts; ++i) {
		if (module->exports.elts[i].name)
			free(module->exports.elts[i].name);
	}
	free(module->exports.elts);
	free(module);
}

int wasmjit_typecheck_func(const struct FuncType *type,
			   const struct FuncInst *funcinst)
{
	return wasmjit_typelist_equal(type->n_inputs, type->input_types,
				      funcinst->type.n_inputs,
				      funcinst->type.input_types) &&
		wasmjit_typelist_equal(FUNC_TYPE_N_OUTPUTS(type),
				       FUNC_TYPE_OUTPUT_TYPES(type),
				       FUNC_TYPE_N_OUTPUTS(&funcinst->type),
				       FUNC_TYPE_OUTPUT_TYPES(&funcinst->type));
}

int wasmjit_typecheck_table(const struct TableType *type,
			    const struct TableInst *tableinst)
{
	return (tableinst->elemtype == type->elemtype &&
		tableinst->length >= type->limits.min &&
		(!type->limits.max ||
		 (type->limits.max && tableinst->max &&
		  tableinst->max <= type->limits.max)));
}

int wasmjit_typecheck_memory(const struct MemoryType *type,
			     const struct MemInst *meminst)
{
	size_t msize = meminst->size / WASM_PAGE_SIZE;
	size_t mmax = meminst->max / WASM_PAGE_SIZE;
	return (msize >= type->limits.min &&
		(!type->limits.max ||
		 (type->limits.max && mmax &&
		  mmax <= type->limits.max)));
}

int wasmjit_typecheck_global(const struct GlobalType *globaltype,
			     const struct GlobalInst *globalinst)
{
	return globalinst->value.type == globaltype->valtype &&
		globalinst->mut == globaltype->mut;
}

int _wasmjit_create_func_type(struct FuncType *ft,
			      size_t n_inputs,
			      wasmjit_valtype_t *input_types,
			      size_t n_outputs,
			      wasmjit_valtype_t *output_types)
{
	assert(n_outputs <= 1);
	assert(n_inputs <= sizeof(ft->input_types) / sizeof(ft->input_types[0]));
	memset(ft, 0, sizeof(*ft));

	ft->n_inputs = n_inputs;
	memcpy(ft->input_types, input_types, sizeof(input_types[0]) * n_inputs);

	if (n_outputs) {
		ft->output_type = output_types[0];
	} else {
		ft->output_type = VALTYPE_NULL;
	}

	return 1;
}

__attribute__((noreturn))
void wasmjit_trap(int reason)
{
	longjmp(*wasmjit_get_jmp_buf(), reason + 1);
}

void *wasmjit_resolve_indirect_call(const struct TableInst *tableinst,
				    const struct FuncType *expected_type,
				    uint32_t idx)
{
	struct FuncInst *funcinst;

	if (idx >= tableinst->length)
		wasmjit_trap(WASMJIT_TRAP_TABLE_OVERFLOW);

	funcinst = tableinst->data[idx];
	if (!funcinst)
		wasmjit_trap(WASMJIT_TRAP_UNINITIALIZED_TABLE_ENTRY);

	if (!wasmjit_typecheck_func(expected_type, funcinst))
		wasmjit_trap(WASMJIT_TRAP_BAD_FUNCTION_TYPE);

	return funcinst->compiled_code;
}
