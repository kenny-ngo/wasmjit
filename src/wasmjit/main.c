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

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <sys/stat.h>

char *load_file(char *file_name, size_t *size)
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
int init_pstate(struct ParseState *pstate, char *file_name)
{
	pstate->eof = 0;
	pstate->input = load_file(file_name, &pstate->amt_left);
	return pstate->input;
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

float float_swap_bytes(float data)
{
	return data;
}

double double_swap_bytes(double data)
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
DEFINE_INT_READER(float);
DEFINE_INT_READER(double);

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
			*data |= ((type) (byt & 0x7f)) << shift;	\
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
DEFINE_ULEB_READER(uint64_t);

int read_float(struct ParseState *pstate, float *data);
int read_double(struct ParseState *pstate, double *data);

char *read_buf_internal(struct ParseState *pstate, uint32_t *buf_size,
			int zero_term)
{
	char *toret;
	int ret;
	uint32_t string_size;
	int as_string = zero_term ? 1 : 0;

	ret = read_uleb_uint32_t(pstate, &string_size);
	if (!ret)
		return NULL;

	if (pstate->amt_left < string_size) {
		pstate->eof = 1;
		return NULL;
	}

	toret = malloc(string_size + as_string);
	if (!toret)
		return NULL;

	memcpy(toret, pstate->input, string_size);
	if (as_string) {
		toret[string_size] = '\0';
	}

	if (buf_size) {
		*buf_size = string_size;
	}

	pstate->input += string_size;
	pstate->amt_left -= string_size;

	return toret;
}

char *read_string(struct ParseState *pstate)
{
	return read_buf_internal(pstate, NULL, 1);
}

char *read_buffer(struct ParseState *pstate, uint32_t *buf_size)
{
	return read_buf_internal(pstate, buf_size, 0);
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

#define BLOCK_TERMINAL 0x0B
#define ELSE_TERMINAL 0x05

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

void init_instruction(struct Instr *instr)
{
	memset(instr, 0, sizeof(*instr));
}

void free_instruction(struct Instr *instr)
{
}

struct Instr *read_instructions(struct ParseState *pstate,
				size_t *n_instructions, int allow_else,
				int allow_block);

int read_instruction(struct ParseState *pstate, struct Instr *instr,
		     int allow_else, int allow_block)
{
	int ret;
	struct BlockLoopExtra *block;
	struct BrIfExtra *brextra;
	struct LocalExtra *local;
	struct GlobalExtra *gextra;
	struct LoadStoreExtra *lsextra;

	init_instruction(instr);

	ret = read_uint8_t(pstate, &instr->opcode);
	if (!ret)
		return ret;

	switch (instr->opcode) {
	case BLOCK_TERMINAL:
		return allow_block;
	case ELSE_TERMINAL:
		return allow_else;
	case OPCODE_BLOCK:
	case OPCODE_LOOP:
		block = instr->opcode == OPCODE_BLOCK
		    ? &instr->data.block : &instr->data.loop;

		ret = read_uint8_t(pstate, &block->blocktype);
		if (!ret)
			goto error;

		block->instructions =
		    read_instructions(pstate, &block->n_instructions, 0, 1);
		if (!block->instructions)
			goto error;

		break;
	case OPCODE_IF:
		ret = read_uint8_t(pstate, &instr->data.if_.blocktype);
		if (!ret)
			goto error;

		instr->data.if_.instructions_then =
		    read_instructions(pstate,
				      &instr->data.if_.n_instructions_then, 1,
				      1);
		if (!instr->data.if_.instructions_then)
			goto error;

		instr->data.if_.instructions_else =
		    read_instructions(pstate,
				      &instr->data.if_.n_instructions_else, 0,
				      1);
		if (!instr->data.if_.instructions_else)
			goto error;

		break;
	case OPCODE_BR:
	case OPCODE_BR_IF:
		brextra = instr->opcode == OPCODE_BR
		    ? &instr->data.br : &instr->data.br_if;

		ret = read_uleb_uint32_t(pstate, &brextra->labelidx);
		if (!ret)
			goto error;

		break;
	case OPCODE_BR_TABLE:
		ret =
		    read_uleb_uint32_t(pstate,
				       &instr->data.br_table.n_labelidxs);
		if (!ret)
			goto error;

		if (instr->data.br_table.n_labelidxs) {
			uint32_t i;

			instr->data.br_table.labelidxs =
			    calloc(instr->data.br_table.n_labelidxs,
				   sizeof(int));
			if (!instr->data.br_table.labelidxs)
				goto error;

			for (i = 0; i < instr->data.br_table.n_labelidxs; ++i) {
				ret =
				    read_uleb_uint32_t(pstate,
						       &instr->data.
						       br_table.labelidxs[i]);
				if (!ret)
					goto error;
			}
		}

		ret =
		    read_uleb_uint32_t(pstate, &instr->data.br_table.labelidx);
		if (!ret)
			goto error;

		break;
	case OPCODE_CALL:
		ret = read_uleb_uint32_t(pstate, &instr->data.call.funcidx);
		if (!ret)
			goto error;

		break;
	case OPCODE_CALL_INDIRECT:
		ret =
		    read_uleb_uint32_t(pstate,
				       &instr->data.call_indirect.typeidx);
		if (!ret)
			goto error;

		break;
	case OPCODE_GET_LOCAL:
	case OPCODE_SET_LOCAL:
	case OPCODE_TEE_LOCAL:
		switch (instr->opcode) {
		case OPCODE_GET_LOCAL:
			local = &instr->data.get_local;
			break;
		case OPCODE_SET_LOCAL:
			local = &instr->data.set_local;
			break;
		case OPCODE_TEE_LOCAL:
			local = &instr->data.tee_local;
			break;
		default:
			assert(0);
		}

		ret = read_uleb_uint32_t(pstate, &local->localidx);
		if (!ret)
			goto error;

		break;
	case OPCODE_GET_GLOBAL:
	case OPCODE_SET_GLOBAL:
		gextra = instr->opcode == OPCODE_GET_GLOBAL
		    ? &instr->data.get_global : &instr->data.set_global;

		ret = read_uleb_uint32_t(pstate, &gextra->globalidx);
		if (!ret)
			goto error;

		break;
	case OPCODE_I32_LOAD:
	case OPCODE_I64_LOAD:
	case OPCODE_F32_LOAD:
	case OPCODE_F64_LOAD:
	case OPCODE_I32_LOAD8_S:
	case OPCODE_I32_LOAD8_U:
	case OPCODE_I32_LOAD16_S:
	case OPCODE_I32_LOAD16_U:
	case OPCODE_I64_LOAD8_S:
	case OPCODE_I64_LOAD8_U:
	case OPCODE_I64_LOAD16_S:
	case OPCODE_I64_LOAD16_U:
	case OPCODE_I64_LOAD32_S:
	case OPCODE_I64_LOAD32_U:
	case OPCODE_I32_STORE:
	case OPCODE_I64_STORE:
	case OPCODE_F32_STORE:
	case OPCODE_F64_STORE:
	case OPCODE_I32_STORE8:
	case OPCODE_I32_STORE16:
	case OPCODE_I64_STORE8:
	case OPCODE_I64_STORE16:
	case OPCODE_I64_STORE32:
		switch (instr->opcode) {
		case OPCODE_I32_LOAD:
			lsextra = &instr->data.i32_load;
			break;
		case OPCODE_I64_LOAD:
			lsextra = &instr->data.i64_load;
			break;
		case OPCODE_F32_LOAD:
			lsextra = &instr->data.f32_load;
			break;
		case OPCODE_F64_LOAD:
			lsextra = &instr->data.f64_load;
			break;
		case OPCODE_I32_LOAD8_S:
			lsextra = &instr->data.i32_load8_s;
			break;
		case OPCODE_I32_LOAD8_U:
			lsextra = &instr->data.i32_load8_u;
			break;
		case OPCODE_I32_LOAD16_S:
			lsextra = &instr->data.i32_load16_s;
			break;
		case OPCODE_I32_LOAD16_U:
			lsextra = &instr->data.i32_load16_u;
			break;
		case OPCODE_I64_LOAD8_S:
			lsextra = &instr->data.i64_load8_s;
			break;
		case OPCODE_I64_LOAD8_U:
			lsextra = &instr->data.i64_load8_u;
			break;
		case OPCODE_I64_LOAD16_S:
			lsextra = &instr->data.i64_load16_s;
			break;
		case OPCODE_I64_LOAD16_U:
			lsextra = &instr->data.i64_load16_u;
			break;
		case OPCODE_I64_LOAD32_S:
			lsextra = &instr->data.i64_load32_s;
			break;
		case OPCODE_I64_LOAD32_U:
			lsextra = &instr->data.i64_load32_u;
			break;
		case OPCODE_I32_STORE:
			lsextra = &instr->data.i32_store;
			break;
		case OPCODE_I64_STORE:
			lsextra = &instr->data.i64_store;
			break;
		case OPCODE_F32_STORE:
			lsextra = &instr->data.f32_store;
			break;
		case OPCODE_F64_STORE:
			lsextra = &instr->data.f64_store;
			break;
		case OPCODE_I32_STORE8:
			lsextra = &instr->data.i32_store8;
			break;
		case OPCODE_I32_STORE16:
			lsextra = &instr->data.i32_store16;
			break;
		case OPCODE_I64_STORE8:
			lsextra = &instr->data.i64_store8;
			break;
		case OPCODE_I64_STORE16:
			lsextra = &instr->data.i64_store16;
			break;
		case OPCODE_I64_STORE32:
			lsextra = &instr->data.i64_store32;
			break;
		default:
			assert(0);
			break;
		}

		ret = read_uleb_uint32_t(pstate, &lsextra->align);
		if (!ret)
			goto error;

		ret = read_uleb_uint32_t(pstate, &lsextra->offset);
		if (!ret)
			goto error;

		break;
	case OPCODE_I32_CONST:
		ret = read_uleb_uint32_t(pstate, &instr->data.i32_const.value);
		if (!ret)
			goto error;
		break;
	case OPCODE_I64_CONST:
		ret = read_uleb_uint64_t(pstate, &instr->data.i64_const.value);
		if (!ret)
			goto error;
		break;
	case OPCODE_F32_CONST:
		ret = read_float(pstate, &instr->data.f32_const.value);
		if (!ret)
			goto error;
		break;
	case OPCODE_F64_CONST:
		ret = read_double(pstate, &instr->data.f64_const.value);
		if (!ret)
			goto error;
		break;
	case OPCODE_UNREACHABLE:
	case OPCODE_NOP:
	case OPCODE_RETURN:
	case OPCODE_DROP:
	case OPCODE_SELECT:
	case OPCODE_MEMORY_SIZE:
	case OPCODE_MEMORY_GROW:
	case OPCODE_I32_EQZ:
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
	case OPCODE_I64_EQZ:
	case OPCODE_I64_EQ:
	case OPCODE_I64_NE:
	case OPCODE_I64_LT_S:
	case OPCODE_I64_LT_U:
	case OPCODE_I64_GT_S:
	case OPCODE_I64_GT_U:
	case OPCODE_I64_LE_S:
	case OPCODE_I64_LE_U:
	case OPCODE_I64_GE_S:
	case OPCODE_I64_GE_U:
	case OPCODE_F32_EQ:
	case OPCODE_F32_NE:
	case OPCODE_F32_LT:
	case OPCODE_F32_GT:
	case OPCODE_F32_LE:
	case OPCODE_F32_GE:
	case OPCODE_F64_EQ:
	case OPCODE_F64_NE:
	case OPCODE_F64_LT:
	case OPCODE_F64_GT:
	case OPCODE_F64_LE:
	case OPCODE_F64_GE:
	case OPCODE_I32_CLZ:
	case OPCODE_I32_CTZ:
	case OPCODE_I32_POPCNT:
	case OPCODE_I32_ADD:
	case OPCODE_I32_SUB:
	case OPCODE_I32_MUL:
	case OPCODE_I32_DIV_S:
	case OPCODE_I32_DIV_U:
	case OPCODE_I32_REM_S:
	case OPCODE_I32_REM_U:
	case OPCODE_I32_AND:
	case OPCODE_I32_OR:
	case OPCODE_I32_XOR:
	case OPCODE_I32_SHL:
	case OPCODE_I32_SHR_S:
	case OPCODE_I32_SHR_U:
	case OPCODE_I32_ROTL:
	case OPCODE_I32_ROTR:
	case OPCODE_I64_CLZ:
	case OPCODE_I64_CTZ:
	case OPCODE_I64_POPCNT:
	case OPCODE_I64_ADD:
	case OPCODE_I64_SUB:
	case OPCODE_I64_MUL:
	case OPCODE_I64_DIV_S:
	case OPCODE_I64_DIV_U:
	case OPCODE_I64_REM_S:
	case OPCODE_I64_REM_U:
	case OPCODE_I64_AND:
	case OPCODE_I64_OR:
	case OPCODE_I64_XOR:
	case OPCODE_I64_SHL:
	case OPCODE_I64_SHR_S:
	case OPCODE_I64_SHR_U:
	case OPCODE_I64_ROTL:
	case OPCODE_I64_ROTR:
	case OPCODE_F32_ABS:
	case OPCODE_F32_NEG:
	case OPCODE_F32_CEIL:
	case OPCODE_F32_FLOOR:
	case OPCODE_F32_TRUNC:
	case OPCODE_F32_NEAREST:
	case OPCODE_F32_SQRT:
	case OPCODE_F32_ADD:
	case OPCODE_F32_SUB:
	case OPCODE_F32_MUL:
	case OPCODE_F32_DIV:
	case OPCODE_F32_MIN:
	case OPCODE_F32_MAX:
	case OPCODE_F32_COPYSIGN:
	case OPCODE_F64_ABS:
	case OPCODE_F64_NEG:
	case OPCODE_F64_CEIL:
	case OPCODE_F64_FLOOR:
	case OPCODE_F64_TRUNC:
	case OPCODE_F64_NEAREST:
	case OPCODE_F64_SQRT:
	case OPCODE_F64_ADD:
	case OPCODE_F64_SUB:
	case OPCODE_F64_MUL:
	case OPCODE_F64_DIV:
	case OPCODE_F64_MIN:
	case OPCODE_F64_MAX:
	case OPCODE_F64_COPYSIGN:
	case OPCODE_I32_WRAP_I64:
	case OPCODE_I32_TRUNC_S_F32:
	case OPCODE_I32_TRUNC_U_F32:
	case OPCODE_I32_TRUNC_S_F64:
	case OPCODE_I32_TRUNC_U_F64:
	case OPCODE_I64_EXTEND_S_I32:
	case OPCODE_I64_EXTEND_U_I32:
	case OPCODE_I64_TRUNC_S_F32:
	case OPCODE_I64_TRUNC_U_F32:
	case OPCODE_I64_TRUNC_S_F64:
	case OPCODE_I64_TRUNC_U_F64:
	case OPCODE_F32_CONVERT_S_I32:
	case OPCODE_F32_CONVERT_U_I32:
	case OPCODE_F32_CONVERT_U_I64:
	case OPCODE_F32_CONVERT_S_I64:
	case OPCODE_F32_DEMOTE_F64:
	case OPCODE_F64_CONVERT_S_I32:
	case OPCODE_F64_CONVERT_U_I32:
	case OPCODE_F64_CONVERT_U_I64:
	case OPCODE_F64_CONVERT_S_I64:
	case OPCODE_F64_PROMOTE_F32:
	case OPCODE_I32_REINTERPRET_F32:
	case OPCODE_I64_REINTERPRET_F64:
	case OPCODE_F32_REINTERPRET_I32:
	case OPCODE_F64_REINTERPRET_I46:
		break;
	default:
		goto error;
	}

	return 1;

 error:
	free_instruction(instr);
	return 0;
}

void free_instructions(struct Instr *instructions, size_t n_instructions)
{
	size_t i;
	for (i = 0; i < n_instructions; ++i) {
		free_instruction(&instructions[i]);
	}
	free(instructions);
}

struct Instr *read_instructions(struct ParseState *pstate,
				size_t *n_instructions, int allow_else,
				int allow_block)
{
	struct Instr *instructions = NULL;

	assert(!*n_instructions);

	while (1) {
		int ret;
		struct Instr instruction, *next_instructions;
		size_t new_len;

		ret =
		    read_instruction(pstate, &instruction, allow_else,
				     allow_block);
		if (!ret)
			goto error;

		if (instruction.opcode == BLOCK_TERMINAL
		    || instruction.opcode == ELSE_TERMINAL) {
			free_instruction(&instruction);
			break;
		}

		new_len = *n_instructions + 1;

		size_t size;
		if (__builtin_umull_overflow
		    (new_len, sizeof(struct Instr), &size)) {
			goto error;
		}

		next_instructions = realloc(instructions, size);
		if (!next_instructions) {
			free_instruction(&instruction);
			goto error;
		}

		next_instructions[new_len - 1] = instruction;

		instructions = next_instructions;
		*n_instructions = new_len;
	}

	return instructions;

 error:
	if (instructions) {
		free_instructions(instructions, *n_instructions);
	}
	return NULL;
}

struct GlobalSection {
	uint32_t n_globals;
	struct GlobalSectionGlobal {
		struct GlobalType type;
		size_t n_instructions;
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

			global->instructions = read_instructions(pstate,
								 &global->n_instructions,
								 0, 1);
			if (!global->instructions) {
				goto error;
			}
		}
	}

	return 1;

 error:
	/* TODO: free global_section memory */
	return 0;
}

struct ExportSection {
	uint32_t n_exports;
	struct ExportSectionExport {
		char *name;
		uint8_t idx_type;
		uint32_t idx;
	} *exports;
};

int read_export_section(struct ParseState *pstate,
			struct ExportSection *export_section)
{
	int ret;

	export_section->exports = NULL;

	ret = read_uleb_uint32_t(pstate, &export_section->n_exports);
	if (!ret)
		goto error;

	if (export_section->n_exports) {
		uint32_t i;

		export_section->exports =
		    calloc(export_section->n_exports,
			   sizeof(struct ExportSectionExport));
		if (!export_section->exports)
			goto error;

		for (i = 0; i < export_section->n_exports; ++i) {
			struct ExportSectionExport *export =
			    &export_section->exports[i];

			export->name = read_string(pstate);
			if (!export->name)
				goto error;

			ret = read_uint8_t(pstate, &export->idx_type);
			if (!ret)
				goto error;

			ret = read_uleb_uint32_t(pstate, &export->idx);
			if (!ret)
				goto error;
		}
	}

	return 1;

 error:
	if (export_section->exports) {
		uint32_t i;
		for (i = 0; i < export_section->n_exports; ++i) {
			if (export_section->exports[i].name) {
				free(export_section->exports[i].name);
			}
		}
		free(export_section->exports);
	}

	return 0;
}

struct StartSection {
	uint32_t funcidx;
};

int read_start_section(struct ParseState *pstate,
		       struct StartSection *start_section)
{
	return read_uleb_uint32_t(pstate, &start_section->funcidx);
}

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

int read_element_section(struct ParseState *pstate,
			 struct ElementSection *element_section)
{
	int ret;

	element_section->elements = NULL;

	ret = read_uleb_uint32_t(pstate, &element_section->n_elements);
	if (!ret)
		goto error;

	if (element_section->n_elements) {
		uint32_t i;

		element_section->elements =
		    calloc(element_section->n_elements,
			   sizeof(struct ElementSectionElement));
		if (!element_section->elements)
			goto error;

		for (i = 0; i < element_section->n_elements; ++i) {
			struct ElementSectionElement *element =
			    &element_section->elements[i];

			ret = read_uleb_uint32_t(pstate, &element->tableidx);
			if (!ret)
				goto error;

			element->instructions =
			    read_instructions(pstate,
					      &element->n_instructions, 0, 1);
			if (!element->instructions)
				goto error;

			ret = read_uleb_uint32_t(pstate, &element->n_funcidxs);
			if (!ret)
				goto error;

			if (element->n_funcidxs) {
				uint32_t j;

				element->funcidxs =
				    calloc(element->n_funcidxs,
					   sizeof(uint32_t));
				if (!element->funcidxs)
					goto error;

				for (j = 0; j < element->n_funcidxs; ++j) {
					ret =
					    read_uleb_uint32_t(pstate,
							       &element->funcidxs
							       [j]);
					if (!ret)
						goto error;
				}
			}
		}
	}

	return 1;

 error:
	if (element_section->elements) {
		uint32_t i;
		for (i = 0; i < element_section->n_elements; ++i) {
			struct ElementSectionElement *element =
			    &element_section->elements[i];

			if (element->instructions) {
				free_instructions(element->instructions,
						  element->n_instructions);
			}

			if (element->funcidxs) {
				free(element->funcidxs);
			}
		}
		free(element_section->elements);
	}
	return 0;
}

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

int read_code_section(struct ParseState *pstate,
		      struct CodeSection *code_section)
{
	int ret;

	code_section->codes = NULL;

	ret = read_uleb_uint32_t(pstate, &code_section->n_codes);
	if (!ret)
		goto error;

	if (code_section->n_codes) {
		uint32_t i;

		code_section->codes =
		    calloc(code_section->n_codes,
			   sizeof(struct CodeSectionCode));
		if (!code_section->codes)
			goto error;

		for (i = 0; i < code_section->n_codes; ++i) {
			struct CodeSectionCode *code = &code_section->codes[i];

			ret = read_uleb_uint32_t(pstate, &code->size);
			if (!ret)
				goto error;

			ret = read_uleb_uint32_t(pstate, &code->n_locals);
			if (!ret)
				goto error;

			if (code->n_locals) {
				uint32_t j;

				code->locals =
				    calloc(code->n_locals,
					   sizeof(struct CodeSectionCodeLocal));
				if (!code->locals)
					goto error;

				for (j = 0; j < code->n_locals; ++j) {
					struct CodeSectionCodeLocal
					*code_local = &code->locals[j];

					ret =
					    read_uleb_uint32_t(pstate,
							       &code_local->count);
					if (!ret)
						goto error;

					ret =
					    read_uint8_t(pstate,
							 &code_local->valtype);
					if (!ret)
						goto error;
				}
			}

			code->instructions =
			    read_instructions(pstate,
					      &code->n_instructions, 0, 1);
			if (!code->instructions)
				goto error;
		}
	}

	return 1;

 error:
	if (code_section->codes) {
		uint32_t i;
		for (i = 0; i < code_section->n_codes; ++i) {
			struct CodeSectionCode *code = &code_section->codes[i];

			if (code->locals) {
				free(code->locals);
			}

			if (code->instructions) {
				free_instructions(code->instructions,
						  code->n_instructions);
			}
		}
		free(code_section->codes);
	}
	return 0;
}

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

int read_data_section(struct ParseState *pstate,
		      struct DataSection *data_section)
{
	int ret;

	data_section->datas = NULL;

	ret = read_uleb_uint32_t(pstate, &data_section->n_datas);
	if (!ret)
		goto error;

	if (data_section->n_datas) {
		uint32_t i;

		data_section->datas =
		    calloc(data_section->n_datas,
			   sizeof(struct DataSectionData));
		if (!data_section->datas)
			goto error;

		for (i = 0; i < data_section->n_datas; ++i) {
			struct DataSectionData *data = &data_section->datas[i];

			ret = read_uleb_uint32_t(pstate, &data->memidx);
			if (!ret)
				goto error;

			data->instructions =
			    read_instructions(pstate,
					      &data->n_instructions, 0, 1);
			if (!data->instructions)
				goto error;

			data->buf = read_buffer(pstate, &data->buf_size);
			if (!data->buf)
				goto error;
		}
	}

	return 1;

 error:
	if (data_section->datas) {
		uint32_t i;
		for (i = 0; i < data_section->n_datas; ++i) {
			struct DataSectionData *data = &data_section->datas[i];

			if (data->instructions) {
				free_instructions(data->instructions,
						  data->n_instructions);
			}

			if (data->buf) {
				free(data->buf);
			}
		}
		free(data_section->datas);
	}
	return 0;
}


void dump_global_section(struct GlobalSection *global_section)
{
	uint32_t i;

	for (i = 0; i < global_section->n_globals; ++i) {
		struct GlobalSectionGlobal *global =
		    &global_section->globals[i];
		printf("Global Section %" PRIu32 " type: %" PRIu8
		       " can mute: %" PRIu8 "\n", i,
		       global->type.valtype, global->type.mut);
	}
}


void dump_instructions_inner(const struct Instr *instructions,
			     size_t n_instructions, int indent);

void dump_instruction(const struct Instr *instruction, int indent)
{
	int sps = indent * 2;
	const char *name;
	switch (instruction->opcode) {
	case OPCODE_UNREACHABLE:
		printf("%*sunreachable\n", sps, "");
		break;
	case OPCODE_BLOCK:
		printf("%*sblock 0x%02" PRIx8 " %p\n", sps, "",
		       instruction->data.block.blocktype,
		       instruction->data.block.instructions);
		dump_instructions_inner(instruction->data.block.instructions,
					instruction->data.block.n_instructions,
					indent + 1);
		break;
	case OPCODE_LOOP:
		printf("%*sloop 0x%02" PRIx8 "\n", sps, "",
		       instruction->data.loop.blocktype);
		dump_instructions_inner(instruction->data.loop.instructions,
					instruction->data.loop.n_instructions,
					indent + 1);
		break;
	case OPCODE_BR_IF:
		printf("%*sbr_if 0x%" PRIx32 "\n", sps, "",
		       instruction->data.br_if.labelidx);
		break;
	case OPCODE_RETURN:
		printf("%*sreturn\n", sps, "");
		break;
	case OPCODE_CALL:
		printf("%*scall 0x%" PRIx32 "\n", sps, "",
		       instruction->data.call.funcidx);
		break;
	case OPCODE_GET_LOCAL:
		printf("%*sget_local 0x%" PRIx32 "\n", sps, "",
		       instruction->data.get_local.localidx);
		break;
	case OPCODE_SET_LOCAL:
		printf("%*sset_local 0x%" PRIx32 "\n", sps, "",
		       instruction->data.set_local.localidx);
		break;
	case OPCODE_TEE_LOCAL:
		printf("%*stee_local 0x%" PRIx32 "\n", sps, "",
		       instruction->data.tee_local.localidx);
		break;
	case OPCODE_I32_LOAD:
		name = "i32.load";
		printf("%*s%s align: 0x%" PRIx32 " offset: 0x%" PRIx32
		       "\n", sps, "", name,
		       instruction->data.i32_load.align,
		       instruction->data.i32_load.offset);
		break;
	case OPCODE_I32_CONST:
		printf("%*si32.const 0x%" PRIx32 "\n", sps, "",
		       instruction->data.i32_const.value);
		break;
	case OPCODE_I32_LT_S:
		printf("%*si32.lt_s\n", sps, "");
		break;
	case OPCODE_I32_ADD:
		printf("%*si32.add\n", sps, "");
		break;
	case OPCODE_I32_MUL:
		printf("%*si32.mul\n", sps, "");
		break;
	default:
		printf("%*sBAD 0x%02" PRIx8 "\n", sps, "", instruction->opcode);
		exit(0);
	}
}

void dump_instructions_inner(const struct Instr *instructions,
			     size_t n_instructions, int indent)
{
	uint32_t i;

	for (i = 0; i < n_instructions; ++i) {
		dump_instruction(&instructions[i], indent);
	}
}

void dump_instructions(const struct Instr *instructions, size_t n_instructions)
{
	dump_instructions_inner(instructions, n_instructions, 0);
}

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

int read_module(struct ParseState *pstate, struct Module *module)
{
#define READ(msg, fn, ...)						\
	do {								\
		int ret;						\
		ret = fn(pstate, __VA_ARGS__);				\
		if (!ret) {						\
			if (is_eof(pstate)) {				\
				fprintf(stderr, "EOF while reading " msg "\n"); \
			}						\
			else {						\
				fprintf(stderr, "Error reading " msg ": %s\n", strerror(errno)); \
			}						\
			return 0;					\
		}							\
	}								\
	while (0)

	/* check magic */
	{
		uint32_t magic;

		READ("magic", read_uint32_t, &magic);

		if (magic != WASM_MAGIC) {
			fprintf(stderr,
				"Bad WASM magic 0x%" PRIx32 " vs 0x%"
				PRIx32 "\n", magic, WASM_MAGIC);
			return 0;
		}
	}

	/* check version */
	{
		uint32_t version;

		READ("version", read_uint32_t, &version);

		if (version != VERSION) {
			fprintf(stderr,
				"Unsupported WASM version 0x%" PRIx32
				" vs 0x%" PRIx32 "\n", version, VERSION);
			return 0;
		}

	}

	/* read sections */
	while (1) {
		uint8_t id;
		uint32_t size;

		{
			int ret;
			ret = read_uint8_t(pstate, &id);
			if (!ret) {
				if (is_eof(pstate)) {
					break;
				}
				fprintf(stderr,
					"Error reading id %s\n",
					strerror(errno));
				return 0;
			}
		}
		READ("size", read_uleb_uint32_t, &size);

		switch (id) {
		case SECTION_ID_CUSTOM:
			READ("custom section", advance_parser, size);
			break;
		case SECTION_ID_TYPE:
			READ("type section", read_type_section, &module->type_section);
			break;
		case SECTION_ID_IMPORT:
			READ("import section", read_import_section,
			     &module->import_section);
			break;
		case SECTION_ID_FUNCTION:
			READ("function section", read_function_section,
			     &module->function_section);
			break;
		case SECTION_ID_TABLE:
			READ("table section", read_table_section,
			     &module->table_section);
			break;
		case SECTION_ID_MEMORY:
			READ("memory section", read_memory_section,
			     &module->memory_section);
			break;
		case SECTION_ID_GLOBAL:
			READ("global section", read_global_section,
			     &module->global_section);
			break;
		case SECTION_ID_EXPORT:
			READ("export section", read_export_section,
			     &module->export_section);
			break;
		case SECTION_ID_START:
			READ("start section", read_start_section,
			     &module->start_section);
			break;
		case SECTION_ID_ELEMENT:
			READ("element section", read_element_section,
			     &module->element_section);
			break;
		case SECTION_ID_CODE:
			READ("code section", read_code_section,
			     &module->code_section);
			break;
		case SECTION_ID_DATA:
			READ("data section", read_data_section,
			     &module->data_section);
			break;
		default:
			printf("Unsupported wasm section: 0x%" PRIx32 "\n", id);
			return 0;
		}
	}
	return 1;
}

int main(int argc, char *argv[])
{
	int ret;
	struct ParseState pstate;
	struct Module module;

	if (argc < 2) {
		printf("Need an input file\n");
		return -1;
	}

	ret = init_pstate(&pstate, argv[1]);
	if (!ret) {
		printf("Error loading file %s\n", strerror(errno));
		return -1;
	}

	ret = read_module(&pstate, &module);
	if (!ret) {
		printf("Error parsing module\n");
		return -1;
	}

	return 0;
}
