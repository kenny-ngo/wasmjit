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
			struct {
				int valtype;
				int mut;
			} globaltype;
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
			{
				uint8_t valtype;
				ret = read_uint8_t(pstate, &valtype);
				if (!ret)
					goto error;
				import->desc.globaltype.valtype = valtype;
			}

			{
				uint8_t mut;
				ret = read_uint8_t(pstate, &mut);
				if (!ret)
					goto error;
				import->desc.globaltype.mut = mut;
			}

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

		table_section->tables = calloc(table_section->n_tables, sizeof(struct TableSectionTable));
		if (!table_section->tables)
			goto error;

		for (i = 0; i < table_section->n_tables; ++i) {
			struct TableSectionTable *table = &table_section->tables[i];

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

int main(int argc, char *argv[])
{
	int ret;
	struct ParseState pstate;
	struct TypeSection type_section;
	struct ImportSection import_section;
	struct FunctionSection function_section;
	struct TableSection table_section;
	struct MemorySection memory_section;

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
