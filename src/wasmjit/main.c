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

#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <sys/stat.h>

char *load_file(char *file_name, size_t * size)
{
	FILE *f = NULL;
	char *input = NULL;
	int fd = -1, ret;
	struct stat st;
	size_t rets;

	fd = open(file_name, O_RDONLY);
	if (fd < 0) {
		goto error_exit;
	}

	ret = fstat(fd, &st);
	if (ret < 0) {
		goto error_exit;
	}

	f = fdopen(fd, "r");
	if (!f) {
		goto error_exit;
	}
	fd = -1;

	*size = st.st_size;
	input = malloc(st.st_size);
	if (!input) {
		goto error_exit;
	}

	rets = fread(input, sizeof(char), st.st_size, f);
	if (rets != st.st_size) {
		goto error_exit;
	}

	goto success_exit;

 error_exit:
	if (input) {
		free(input);
	}

 success_exit:
	if (f) {
		fclose(f);
	}

	if (fd >= 0) {
		close(fd);
	}

	return input;
}

#define WASM_MAGIC 0x6d736100
#define VERSION 0x1

struct ParseState {
	int eof;
	char *input;
	size_t amt_left;
};

void init_pstate(struct ParseState *pstate)
{
	pstate->eof = 0;
}

int is_eof(struct ParseState *pstate)
{
	return pstate->eof;
}

int advance_parser(struct ParseState *pstate, size_t size)
{
	if (pstate->amt_left < size) {
		pstate->eof = 1;
		return 0;
	}
	pstate->amt_left -= size;
	pstate->input += size;
	return 1;
}

uint32_t uint32_t_swap_bytes(uint32_t data)
{
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	return data;
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	uint32_t _4 = data >> 24;
	uint32_t _3 = (data >> 16) & 0xFF;
	uint32_t _2 = (data >> 8) & 0xFF;
	uint32_t _1 = (data >> 0) & 0xFF;
	return _4 | (_3 << 8) | (_2 << 16) | (_1 << 24);
#else
#error Unsupported Architecture
#endif
}

uint8_t uint8_t_swap_bytes(uint8_t data)
{
	return data;
}

#define DEFINE_INT_READER(type) \
	int read_##type(struct ParseState *pstate, type *data)	\
	{							\
		if (pstate->amt_left < sizeof(type)) {		\
			pstate->eof = 1;			\
			return 0;				\
		}						\
								\
		memcpy(data, pstate->input, sizeof(*data));	\
		pstate->amt_left -= sizeof(type);		\
		pstate->input += sizeof(type);			\
								\
		*data = type##_swap_bytes(*data);		\
								\
		return 1;					\
	}

DEFINE_INT_READER(uint32_t);
DEFINE_INT_READER(uint8_t);

#define DEFINE_ULEB_READER(type)					\
	int read_uleb_##type(struct ParseState *pstate, type *data)	\
	{								\
		uint8_t byt;						\
		unsigned int shift;					\
									\
		*data = 0;						\
		shift = 0;						\
		while (1) {						\
			int ret;					\
			ret = read_uint8_t(pstate, &byt);		\
			if (!ret) return ret;				\
									\
			*data |= (byt & 0x7f) << shift;			\
			if (!(byt & 0x80)) {				\
				break;					\
			}						\
									\
			shift += 7;					\
		}							\
									\
		return 1;						\
	}

DEFINE_ULEB_READER(uint32_t);

char *read_string(struct ParseState *pstate)
{
	char *toret;
	int ret;
	uint32_t string_size;

	ret = read_uleb_uint32_t(pstate, &string_size);
	if (!ret)
		return NULL;

	if (pstate->amt_left < string_size) {
		pstate->eof = 1;
		return NULL;
	}

	toret = malloc(string_size + 1);
	if (!toret)
		return NULL;

	memcpy(toret, pstate->input, string_size);
	toret[string_size] = '\0';

	pstate->input += string_size;
	pstate->amt_left -= string_size;

	return toret;
}

struct Limits {
	uint32_t min, max;
	int has_max;
};

int read_limits(struct ParseState *pstate, struct Limits *limits)
{
	int ret;
	uint8_t byt;

	ret = read_uint8_t(pstate, &byt);
	if (!ret)
		return ret;

	limits->has_max = byt;

	switch (byt) {
	case 0x0:
		ret = read_uleb_uint32_t(pstate, &limits->min);
		if (!ret)
			return ret;
		break;
	case 0x1:
		ret = read_uleb_uint32_t(pstate, &limits->min);
		if (!ret)
			return ret;

		ret = read_uleb_uint32_t(pstate, &limits->max);
		if (!ret)
			return ret;

		break;
	default:
		return 0;
	}

	return 1;
}

enum {
	SECTION_ID_CUSTOM,
	SECTION_ID_TYPE,
	SECTION_ID_IMPORT,
	SECTION_ID_FUNCTION,
	SECTION_ID_TABLE,
	SECTION_ID_MEMORY,
	SECTION_ID_GLOBAL,
	SECTION_ID_EXPORT,
	SECTION_ID_START,
	SECTION_ID_ELEMENT,
	SECTION_ID_CODE,
	SECTION_ID_DATA,
};

#define FUNCTION_TYPE_ID 0x60

enum {
	VALTYPE_I32 = 0x7f,
	VALTYPE_I64 = 0x7e,
	VALTYPE_F32 = 0x7d,
	VALTYPE_F64 = 0x7c,
};

struct TypeSection {
	uint32_t n_types;
	struct TypeSectionType {
		uint32_t n_inputs, n_outputs;
		int *input_types, *output_types;
	} *types;
};

int read_type_section(struct ParseState *pstate,
		      struct TypeSection *type_section)
{
	int ret;
	uint32_t i;

	type_section->types = NULL;

	ret = read_uleb_uint32_t(pstate, &type_section->n_types);
	if (!ret)
		goto error;

	if (!type_section->n_types) {
		return 1;
	}

	type_section->types =
	    calloc(type_section->n_types, sizeof(struct TypeSectionType));
	if (!type_section->types)
		goto error;

	for (i = 0; i < type_section->n_types; ++i) {
		size_t j;
		uint8_t ft;
		struct TypeSectionType *type;

		type = &type_section->types[i];

		ret = read_uint8_t(pstate, &ft);
		if (!ret)
			goto error;

		if (ft != FUNCTION_TYPE_ID) {
			errno = EINVAL;
			goto error;
		}

		ret = read_uleb_uint32_t(pstate, &type->n_inputs);
		if (!ret)
			goto error;

		if (type->n_inputs) {
			type->input_types = calloc(type->n_inputs, sizeof(int));
			if (!ret)
				goto error;

			for (j = 0; j < type->n_inputs; ++j) {
				uint8_t valtype;
				ret = read_uint8_t(pstate, &valtype);
				if (!ret)
					goto error;
				type->input_types[j] = valtype;
			}
		}

		ret = read_uleb_uint32_t(pstate, &type->n_outputs);
		if (!ret)
			goto error;

		if (type->n_outputs) {
			type->output_types =
			    calloc(type->n_outputs, sizeof(int));
			if (!ret)
				goto error;

			for (j = 0; j < type->n_outputs; ++j) {
				uint8_t valtype;
				ret = read_uint8_t(pstate, &valtype);
				if (!ret)
					goto error;
				type->output_types[j] = valtype;
			}
		}
	}

	return 1;

 error:
	if (type_section->types) {
		for (i = 0; i < type_section->n_types; ++i) {
			struct TypeSectionType *type = &type_section->types[i];
			if (type->input_types) {
				free(type->input_types);
			}
			if (type->output_types) {
				free(type->output_types);
			}
		}
		free(type_section->types);
	}
	return 0;
}

void dump_type_section(struct TypeSection *type_section)
{
	size_t i, j;
	for (i = 0; i < type_section->n_types; ++i) {
		struct TypeSectionType *type = &type_section->types[i];

		printf("(");
		for (j = 0; j < type->n_inputs; ++j) {
			printf("%d,", type->input_types[j]);
		}
		printf(") -> (");
		for (j = 0; j < type->n_outputs; ++j) {
			printf("%d,", type->output_types[j]);
		}
		printf(")\n");
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

int read_global_type(struct ParseState *pstate, struct GlobalType *globaltype)
{
	int ret;

	ret = read_uint8_t(pstate, &globaltype->valtype);
	if (!ret)
		return 0;

	ret = read_uint8_t(pstate, &globaltype->mut);
	if (!ret)
		return 0;

	return 1;
}

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

#define TABLE_TYPE_ELEM_TYPE 0x70

int read_import_section(struct ParseState *pstate,
			struct ImportSection *import_section)
{
	int ret;
	uint32_t i;

	import_section->imports = NULL;

	ret = read_uleb_uint32_t(pstate, &import_section->n_imports);
	if (!ret)
		goto error;

	if (!import_section->n_imports) {
		return 1;
	}

	import_section->imports =
	    calloc(import_section->n_imports,
		   sizeof(struct ImportSectionImport));

	for (i = 0; i < import_section->n_imports; ++i) {
		uint8_t ft;
		struct ImportSectionImport *import;

		import = &import_section->imports[i];

		import->module = read_string(pstate);
		if (!import->module)
			goto error;

		import->name = read_string(pstate);
		if (!import->name)
			goto error;

		ret = read_uint8_t(pstate, &ft);
		if (!ret)
			goto error;
		import->desc_type = ft;

		switch (import->desc_type) {
		case IMPORT_DESC_TYPE_FUNC:
			ret = read_uleb_uint32_t(pstate, &import->desc.typeidx);
			if (!ret)
				goto error;
			break;
		case IMPORT_DESC_TYPE_TABLE:
			ret = read_uint8_t(pstate, &ft);
			if (!ret)
				goto error;
			if (ft != TABLE_TYPE_ELEM_TYPE)
				goto error;

			ret = read_limits(pstate, &import->desc.tabletype);
			if (!ret)
				goto error;

			break;
		case IMPORT_DESC_TYPE_MEM:
			ret = read_limits(pstate, &import->desc.memtype);
			if (!ret)
				goto error;
			break;
		case IMPORT_DESC_TYPE_GLOBAL:
			ret =
			    read_global_type(pstate, &import->desc.globaltype);
			if (!ret)
				goto error;
			break;
		default:
			goto error;
		}
	}

 error:
	return 1;
}

struct FunctionSection {
	uint32_t n_typeidxs;
	uint32_t *typeidxs;
};

int read_function_section(struct ParseState *pstate,
			  struct FunctionSection *function_section)
{
	int ret;

	function_section->typeidxs = NULL;

	ret = read_uleb_uint32_t(pstate, &function_section->n_typeidxs);
	if (!ret)
		goto error;

	if (function_section->n_typeidxs) {
		uint32_t i;

		function_section->typeidxs =
		    calloc(function_section->n_typeidxs, sizeof(uint32_t));
		if (!function_section->typeidxs)
			goto error;

		for (i = 0; i < function_section->n_typeidxs; ++i) {
			ret =
			    read_uleb_uint32_t(pstate,
					       &function_section->typeidxs[i]);
			if (!ret)
				goto error;
		}
	}

	return 1;

 error:
	if (function_section->typeidxs) {
		free(function_section->typeidxs);
	}

	return 0;
}

struct TableSection {
	uint32_t n_tables;
	struct TableSectionTable {
		int elemtype;
		struct Limits limits;
	} *tables;
};

int read_table_section(struct ParseState *pstate,
		       struct TableSection *table_section)
{
	int ret;

	table_section->tables = NULL;

	ret = read_uleb_uint32_t(pstate, &table_section->n_tables);
	if (!ret)
		goto error;

	if (table_section->n_tables) {
		uint32_t i;

		table_section->tables =
		    calloc(table_section->n_tables,
			   sizeof(struct TableSectionTable));
		if (!table_section->tables)
			goto error;

		for (i = 0; i < table_section->n_tables; ++i) {
			struct TableSectionTable *table =
			    &table_section->tables[i];

			uint8_t elemtype;
			ret = read_uint8_t(pstate, &elemtype);
			if (!ret)
				goto error;
			table->elemtype = elemtype;

			ret = read_limits(pstate, &table->limits);
			if (!ret)
				goto error;
		}
	}

	return 1;

 error:
	if (table_section->tables) {
		free(table_section->tables);
	}
	return 0;
}

struct MemorySection {
	uint32_t n_memories;
	struct MemorySectionMemory {
		struct Limits memtype;
	} *memories;
};

int read_memory_section(struct ParseState *pstate,
			struct MemorySection *memory_section)
{
	int ret;

	memory_section->memories = NULL;

	ret = read_uleb_uint32_t(pstate, &memory_section->n_memories);
	if (!ret)
		goto error;

	if (memory_section->n_memories) {
		uint32_t i;

		memory_section->memories =
		    calloc(memory_section->n_memories,
			   sizeof(struct MemorySectionMemory));
		if (!memory_section->memories) {
			goto error;
		}

		for (i = 0; i < memory_section->n_memories; ++i) {
			struct MemorySectionMemory *memory =
			    &memory_section->memories[i];

			ret = read_limits(pstate, &memory->memtype);
			if (!ret)
				goto error;
		}
	}

	return 1;

 error:
	if (memory_section->memories) {
		free(memory_section->memories);
	}
	return 0;
}

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

struct Instr {
	uint8_t opcode;
	union {
		struct {
			struct {
				uint8_t is_empty;
				uint8_t valtype;
			} blocktype;
			uint32_t n_instructions;
			struct Instr *instructions;
		} block, loop;
		struct {
			struct {
				uint8_t is_empty;
				uint8_t valtype;
			} blocktype;
			uint32_t n_instructions_then;
			struct Instr *instructions_then;
			uint32_t n_instructions_else;
			struct Instr *instructions_else;
		} if_;
		struct {
			int labelidx;
		} br, br_if;
		struct {
			uint32_t n_labelidxs;
			int *labelidxs;
			int labelidx;
		} br_table;
		/* TODO: add the rest */
	} data;
};

int read_instruction(struct ParseState *pstate, struct Instr *instr)
{
	(void)pstate;
	(void)instr;
	return 0;
}

struct GlobalSection {
	uint32_t n_globals;
	struct GlobalSectionGlobal {
		struct GlobalType type;
		uint32_t n_instructions;
		struct Instr *instructions;
	} *globals;
};

int read_global_section(struct ParseState *pstate,
			struct GlobalSection *global_section)
{
	int ret;

	global_section->globals = NULL;

	ret = read_uleb_uint32_t(pstate, &global_section->n_globals);
	if (!ret)
		goto error;

	if (global_section->n_globals) {
		uint32_t i;

		global_section->globals =
		    calloc(global_section->n_globals,
			   sizeof(struct GlobalSectionGlobal));
		if (!global_section->globals)
			goto error;

		for (i = 0; i < global_section->n_globals; ++i) {
			struct GlobalSectionGlobal *global =
			    &global_section->globals[i];

			ret = read_global_type(pstate, &global->type);
			if (!ret)
				goto error;

			global->n_instructions = 0;
			while (1) {
				struct Instr instruction, *next_instructions;
				ret = read_instruction(pstate, &instruction);
				if (!ret)
					goto error;

				++global->n_instructions;

				next_instructions =
				    realloc(global->instructions,
					    global->n_instructions);
				if (!next_instructions) {
					goto error;
				}

				global->instructions = next_instructions;
			}
		}
	}

	return 1;

 error:
	/* TODO: free global_section memory */
	return 0;
}

int main(int argc, char *argv[])
{
	int ret;
	struct ParseState pstate;
	struct TypeSection type_section;
	struct ImportSection import_section;
	struct FunctionSection function_section;
	struct TableSection table_section;
	struct MemorySection memory_section;
	struct GlobalSection global_section;

	init_pstate(&pstate);

	if (argc < 2) {
		printf("Need an input file\n");
		return -1;
	}

	pstate.input = load_file(argv[1], &pstate.amt_left);
	if (!pstate.input) {
		printf("Error loading file %s\n", strerror(errno));
		return -1;
	}
#define READ(msg, fn, ...)						\
	do {								\
		int ret;						\
		ret = fn(&pstate, __VA_ARGS__);				\
		if (!ret) {						\
			if (is_eof(&pstate)) {				\
				printf("EOF while reading " msg "\n");	\
			}						\
			else {						\
				printf("Error reading " msg ": %s\n", strerror(errno));	\
			}						\
			return -1;					\
		}							\
	}								\
	while (0)

	/* check magic */
	{
		uint32_t magic;

		READ("magic", read_uint32_t, &magic);

		if (magic != WASM_MAGIC) {
			printf("Bad WASM magic 0x%" PRIx32 " vs 0x%" PRIx32
			       "\n", magic, WASM_MAGIC);
			return -1;
		}
	}

	/* check version */
	{
		uint32_t version;

		READ("version", read_uint32_t, &version);

		if (version != VERSION) {
			printf("Unsupported WASM version 0x%" PRIx32 " vs 0x%"
			       PRIx32 "\n", version, VERSION);
			return -1;
		}

	}

	/* read sections */
	while (1) {
		uint8_t id;
		uint32_t size;

		{
			int ret;
			ret = read_uint8_t(&pstate, &id);
			if (!ret) {
				if (is_eof(&pstate)) {
					break;
				}
				printf("Error reading id %s\n",
				       strerror(errno));
				return -1;
			}
		}
		READ("size", read_uleb_uint32_t, &size);

		switch (id) {
		case SECTION_ID_CUSTOM:
			READ("custom section", advance_parser, size);
			break;
		case SECTION_ID_TYPE:
			READ("type section", read_type_section, &type_section);
			break;
		case SECTION_ID_IMPORT:
			READ("import section", read_import_section,
			     &import_section);
			break;
		case SECTION_ID_FUNCTION:
			READ("function section", read_function_section,
			     &function_section);
			break;
		case SECTION_ID_TABLE:
			READ("table section", read_table_section,
			     &table_section);
			break;
		case SECTION_ID_MEMORY:
			READ("memory section", read_memory_section,
			     &memory_section);
			break;
		case SECTION_ID_GLOBAL:
			/*
			   READ("global section", read_global_section,
			   &global_section);
			   break;
			 */
		case SECTION_ID_EXPORT:
		case SECTION_ID_START:
		case SECTION_ID_ELEMENT:
		case SECTION_ID_CODE:
		case SECTION_ID_DATA:
			READ("custom section", advance_parser, size);
			break;
		default:
			printf("Unsupported wasm section: 0x%" PRIx32 "\n", id);
			return -1;
		}
	}

	/* */

	return 0;
}
