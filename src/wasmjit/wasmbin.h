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

#ifndef __WASMJIT__WASMBIN_H__
#define __WASMJIT__WASMBIN_H__

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

enum {
	/* Control Instructions */
	OPCODE_UNREACHABLE = 0x00,
	OPCODE_NOP = 0x01,
	OPCODE_BLOCK = 0x02,
	OPCODE_LOOP = 0x03,
	OPCODE_IF = 0x04,
	OPCODE_BR = 0x0C,
	OPCODE_BR_IF = 0x0D,
	OPCODE_BR_TABLE = 0x0E,
	OPCODE_RETURN = 0x0F,
	OPCODE_CALL = 0x10,
	OPCODE_CALL_INDIRECT = 0x11,

	/* Parametric Instructions */
	OPCODE_DROP = 0x1A,
	OPCODE_SELECT = 0x1B,

	/* Variable Instructions */
	OPCODE_GET_LOCAL = 0x20,
	OPCODE_SET_LOCAL = 0x21,
	OPCODE_TEE_LOCAL = 0x22,
	OPCODE_GET_GLOBAL = 0x23,
	OPCODE_SET_GLOBAL = 0x24,

	/* Memory Instructions */
	OPCODE_I32_LOAD = 0x28,
	OPCODE_I64_LOAD = 0x29,
	OPCODE_F32_LOAD = 0x2A,
	OPCODE_F64_LOAD = 0x2B,
	OPCODE_I32_LOAD8_S = 0x2C,
	OPCODE_I32_LOAD8_U = 0x2D,
	OPCODE_I32_LOAD16_S = 0x2E,
	OPCODE_I32_LOAD16_U = 0x2F,
	OPCODE_I64_LOAD8_S = 0x30,
	OPCODE_I64_LOAD8_U = 0x31,
	OPCODE_I64_LOAD16_S = 0x32,
	OPCODE_I64_LOAD16_U = 0x33,
	OPCODE_I64_LOAD32_S = 0x34,
	OPCODE_I64_LOAD32_U = 0x35,
	OPCODE_I32_STORE = 0x36,
	OPCODE_I64_STORE = 0x37,
	OPCODE_F32_STORE = 0x38,
	OPCODE_F64_STORE = 0x39,
	OPCODE_I32_STORE8 = 0x3A,
	OPCODE_I32_STORE16 = 0x3B,
	OPCODE_I64_STORE8 = 0x3C,
	OPCODE_I64_STORE16 = 0x3D,
	OPCODE_I64_STORE32 = 0x3E,
	OPCODE_MEMORY_SIZE = 0x3F,
	OPCODE_MEMORY_GROW = 0x40,

	/* Numeric Instructions */
	OPCODE_I32_CONST = 0x41,
	OPCODE_I64_CONST = 0x42,
	OPCODE_F32_CONST = 0x43,
	OPCODE_F64_CONST = 0x44,

	OPCODE_I32_EQZ = 0x45,
	OPCODE_I32_EQ = 0x46,
	OPCODE_I32_NE = 0x47,
	OPCODE_I32_LT_S = 0x48,
	OPCODE_I32_LT_U = 0x49,
	OPCODE_I32_GT_S = 0x4A,
	OPCODE_I32_GT_U = 0x4B,
	OPCODE_I32_LE_S = 0x4C,
	OPCODE_I32_LE_U = 0x4D,
	OPCODE_I32_GE_S = 0x4E,
	OPCODE_I32_GE_U = 0x4F,

	OPCODE_I64_EQZ = 0x50,
	OPCODE_I64_EQ = 0x51,
	OPCODE_I64_NE = 0x52,
	OPCODE_I64_LT_S = 0x53,
	OPCODE_I64_LT_U = 0x54,
	OPCODE_I64_GT_S = 0x55,
	OPCODE_I64_GT_U = 0x56,
	OPCODE_I64_LE_S = 0x57,
	OPCODE_I64_LE_U = 0x58,
	OPCODE_I64_GE_S = 0x59,
	OPCODE_I64_GE_U = 0x5A,

	OPCODE_F32_EQ = 0x5B,
	OPCODE_F32_NE = 0x5C,
	OPCODE_F32_LT = 0x5D,
	OPCODE_F32_GT = 0x5E,
	OPCODE_F32_LE = 0x5F,
	OPCODE_F32_GE = 0x60,

	OPCODE_F64_EQ = 0x61,
	OPCODE_F64_NE = 0x62,
	OPCODE_F64_LT = 0x63,
	OPCODE_F64_GT = 0x64,
	OPCODE_F64_LE = 0x65,
	OPCODE_F64_GE = 0x66,

	OPCODE_I32_CLZ = 0x67,
	OPCODE_I32_CTZ = 0x68,
	OPCODE_I32_POPCNT = 0x69,
	OPCODE_I32_ADD = 0x6A,
	OPCODE_I32_SUB = 0x6B,
	OPCODE_I32_MUL = 0x6C,
	OPCODE_I32_DIV_S = 0x6D,
	OPCODE_I32_DIV_U = 0x6E,
	OPCODE_I32_REM_S = 0x6F,
	OPCODE_I32_REM_U = 0x70,
	OPCODE_I32_AND = 0x71,
	OPCODE_I32_OR = 0x72,
	OPCODE_I32_XOR = 0x73,
	OPCODE_I32_SHL = 0x74,
	OPCODE_I32_SHR_S = 0x75,
	OPCODE_I32_SHR_U = 0x76,
	OPCODE_I32_ROTL = 0x77,
	OPCODE_I32_ROTR = 0x78,

	OPCODE_I64_CLZ = 0x79,
	OPCODE_I64_CTZ = 0x7A,
	OPCODE_I64_POPCNT = 0x7B,
	OPCODE_I64_ADD = 0x7C,
	OPCODE_I64_SUB = 0x7D,
	OPCODE_I64_MUL = 0x7E,
	OPCODE_I64_DIV_S = 0x7F,
	OPCODE_I64_DIV_U = 0x80,
	OPCODE_I64_REM_S = 0x81,
	OPCODE_I64_REM_U = 0x82,
	OPCODE_I64_AND = 0x83,
	OPCODE_I64_OR = 0x84,
	OPCODE_I64_XOR = 0x85,
	OPCODE_I64_SHL = 0x86,
	OPCODE_I64_SHR_S = 0x87,
	OPCODE_I64_SHR_U = 0x88,
	OPCODE_I64_ROTL = 0x89,
	OPCODE_I64_ROTR = 0x8A,

	OPCODE_F32_ABS = 0x8B,
	OPCODE_F32_NEG = 0x8C,
	OPCODE_F32_CEIL = 0x8D,
	OPCODE_F32_FLOOR = 0x8E,
	OPCODE_F32_TRUNC = 0x8F,
	OPCODE_F32_NEAREST = 0x90,
	OPCODE_F32_SQRT = 0x91,
	OPCODE_F32_ADD = 0x92,
	OPCODE_F32_SUB = 0x93,
	OPCODE_F32_MUL = 0x94,
	OPCODE_F32_DIV = 0x95,
	OPCODE_F32_MIN = 0x96,
	OPCODE_F32_MAX = 0x97,
	OPCODE_F32_COPYSIGN = 0x98,

	OPCODE_F64_ABS = 0x99,
	OPCODE_F64_NEG = 0x9A,
	OPCODE_F64_CEIL = 0x9B,
	OPCODE_F64_FLOOR = 0x9C,
	OPCODE_F64_TRUNC = 0x9D,
	OPCODE_F64_NEAREST = 0x9E,
	OPCODE_F64_SQRT = 0x9F,
	OPCODE_F64_ADD = 0xA0,
	OPCODE_F64_SUB = 0xA1,
	OPCODE_F64_MUL = 0xA2,
	OPCODE_F64_DIV = 0xA3,
	OPCODE_F64_MIN = 0xA4,
	OPCODE_F64_MAX = 0xA5,
	OPCODE_F64_COPYSIGN = 0xA6,

	OPCODE_I32_WRAP_I64 = 0xA7,
	OPCODE_I32_TRUNC_S_F32 = 0xA8,
	OPCODE_I32_TRUNC_U_F32 = 0xA9,
	OPCODE_I32_TRUNC_S_F64 = 0xAA,
	OPCODE_I32_TRUNC_U_F64 = 0xAB,
	OPCODE_I64_EXTEND_S_I32 = 0xAC,
	OPCODE_I64_EXTEND_U_I32 = 0xAD,
	OPCODE_I64_TRUNC_S_F32 = 0xAE,
	OPCODE_I64_TRUNC_U_F32 = 0xAF,
	OPCODE_I64_TRUNC_S_F64 = 0xB0,
	OPCODE_I64_TRUNC_U_F64 = 0xB1,
	OPCODE_F32_CONVERT_S_I32 = 0xB2,
	OPCODE_F32_CONVERT_U_I32 = 0xB3,
	OPCODE_F32_CONVERT_U_I64 = 0xB4,
	OPCODE_F32_CONVERT_S_I64 = 0xB5,
	OPCODE_F32_DEMOTE_F64 = 0xB6,
	OPCODE_F64_CONVERT_S_I32 = 0xB7,
	OPCODE_F64_CONVERT_U_I32 = 0xB8,
	OPCODE_F64_CONVERT_U_I64 = 0xB9,
	OPCODE_F64_CONVERT_S_I64 = 0xBA,
	OPCODE_F64_PROMOTE_F32 = 0xBB,
	OPCODE_I32_REINTERPRET_F32 = 0xBC,
	OPCODE_I64_REINTERPRET_F64 = 0xBD,
	OPCODE_F32_REINTERPRET_I32 = 0xBE,
	OPCODE_F64_REINTERPRET_I46 = 0xBF,
};

enum {
	VALTYPE_NULL = 0x40,
	VALTYPE_I32 = 0x7f,
	VALTYPE_I64 = 0x7e,
	VALTYPE_F32 = 0x7d,
	VALTYPE_F64 = 0x7c,
};

__attribute__ ((unused))
static const char *wasmjit_valtype_repr(unsigned valtype)
{
	switch (valtype) {
	case VALTYPE_I32:
		return "I32";
	case VALTYPE_I64:
		return "I64";
	case VALTYPE_F32:
		return "F32";
	case VALTYPE_F64:
		return "F64";
	default:
		assert(0);
		return NULL;
	}
}

enum {
	IMPORT_DESC_TYPE_FUNC,
	IMPORT_DESC_TYPE_TABLE,
	IMPORT_DESC_TYPE_MEM,
	IMPORT_DESC_TYPE_GLOBAL,
};

struct GlobalType {
	uint8_t valtype;
	uint8_t mut;
};

struct Limits {
	uint32_t min, max;
	int has_max;
};

struct Instr {
	uint8_t opcode;
	union {
		struct BlockLoopExtra {
			uint8_t blocktype;
			size_t n_instructions;
			struct Instr *instructions;
		} block, loop;
		struct {
			uint8_t blocktype;
			size_t n_instructions_then;
			struct Instr *instructions_then;
			size_t n_instructions_else;
			struct Instr *instructions_else;
		} if_;
		struct BrIfExtra {
			uint32_t labelidx;
		} br, br_if;
		struct {
			uint32_t n_labelidxs;
			uint32_t *labelidxs;
			uint32_t labelidx;
		} br_table;
		struct {
			uint32_t funcidx;
		} call;
		struct {
			uint32_t typeidx;
		} call_indirect;
		struct LocalExtra {
			uint32_t localidx;
		} get_local, set_local, tee_local;
		struct GlobalExtra {
			uint32_t globalidx;
		} get_global, set_global;
		struct LoadStoreExtra {
			uint32_t align;
			uint32_t offset;
		} i32_load, i64_load, f32_load, f64_load,
		    i32_load8_s, i32_load8_u, i32_load16_s, i32_load16_u,
		    i64_load8_s, i64_load8_u, i64_load16_s, i64_load16_u,
		    i64_load32_s, i64_load32_u,
		    i32_store, i64_store, f32_store, f64_store,
		    i32_store8, i32_store16, i64_store8, i64_store16,
		    i64_store32;
		struct {
			uint32_t value;
		} i32_const;
		struct {
			uint64_t value;
		} i64_const;
		struct {
			float value;
		} f32_const;
		struct {
			double value;
		} f64_const;
	} data;
};

void init_instruction(struct Instr *instr);
void free_instruction(struct Instr *instr);
void free_instructions(struct Instr *instructions, size_t n_instructions);

struct TypeSection {
	uint32_t n_types;
	struct TypeSectionType {
		uint32_t n_inputs, n_outputs;
		unsigned *input_types, *output_types;
	} *types;
};

struct ImportSection {
	uint32_t n_imports;
	struct ImportSectionImport {
		char *module;
		char *name;
		int desc_type;
		union {
			uint32_t typeidx;
			struct Limits tabletype;
			struct Limits memtype;
			struct GlobalType globaltype;
		} desc;
	} *imports;
};

struct FunctionSection {
	uint32_t n_typeidxs;
	uint32_t *typeidxs;
};

struct TableSection {
	uint32_t n_tables;
	struct TableSectionTable {
		int elemtype;
		struct Limits limits;
	} *tables;
};

struct MemorySection {
	uint32_t n_memories;
	struct MemorySectionMemory {
		struct Limits memtype;
	} *memories;
};

struct GlobalSection {
	uint32_t n_globals;
	struct GlobalSectionGlobal {
		struct GlobalType type;
		size_t n_instructions;
		struct Instr *instructions;
	} *globals;
};

struct ExportSection {
	uint32_t n_exports;
	struct ExportSectionExport {
		char *name;
		uint8_t idx_type;
		uint32_t idx;
	} *exports;
};

struct StartSection {
	int has_start;
	uint32_t funcidx;
};

struct ElementSection {
	uint32_t n_elements;
	struct ElementSectionElement {
		uint32_t tableidx;
		size_t n_instructions;
		struct Instr *instructions;
		uint32_t n_funcidxs;
		uint32_t *funcidxs;
	} *elements;
};

struct CodeSection {
	uint32_t n_codes;
	struct CodeSectionCode {
		uint32_t size;
		uint32_t n_locals;
		struct CodeSectionCodeLocal {
			uint32_t count;
			uint8_t valtype;
		} *locals;
		size_t n_instructions;
		struct Instr *instructions;
	} *codes;
};

struct DataSection {
	uint32_t n_datas;
	struct DataSectionData {
		uint32_t memidx;
		size_t n_instructions;
		struct Instr *instructions;
		uint32_t buf_size;
		char *buf;
	} *datas;
};

struct Module {
	struct TypeSection type_section;
	struct ImportSection import_section;
	struct FunctionSection function_section;
	struct TableSection table_section;
	struct MemorySection memory_section;
	struct GlobalSection global_section;
	struct ExportSection export_section;
	struct StartSection start_section;
	struct ElementSection element_section;
	struct CodeSection code_section;
	struct DataSection data_section;
};

#endif
