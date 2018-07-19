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

void init_pstate(struct ParseState *pstate) {
	pstate->eof = 0;
}

int is_eof(struct ParseState *pstate) {
	return pstate->eof;
}

int advance_parser(struct ParseState *pstate, size_t size) {
	if (pstate->amt_left < size) {
		pstate->eof = 1;
		return 0;
	}
	pstate->amt_left -= size;
	pstate->input += size;
	return 1;
}

uint32_t uint32_t_swap_bytes(uint32_t data) {
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

uint8_t uint8_t_swap_bytes(uint8_t data) {
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

DEFINE_INT_READER(uint32_t)
DEFINE_INT_READER(uint8_t)

#define DEFINE_ULEB_READER(type)					\
	int read_uleb_##type(struct ParseState *pstate, type *data) {	\
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

DEFINE_ULEB_READER(uint32_t)

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

int main(int argc, char *argv[])
{
	int ret;
	struct ParseState pstate;

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
			printf("Bad WASM magic 0x%" PRIx32 " vs 0x%" PRIx32 "\n", magic,
			       WASM_MAGIC);
			return -1;
		}
	}

	/* check version */
	{
		uint32_t version;

		READ("version", read_uint32_t, &version);

		if (version != VERSION) {
			printf("Unsupported WASM version 0x%" PRIx32 " vs 0x%" PRIx32 "\n",
			       version, VERSION);
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
				printf("Error reading id %s\n", strerror(errno));
				return -1;
			}
		}
		READ("size", read_uleb_uint32_t, &size);

		switch (id) {
		case SECTION_ID_CUSTOM:
			READ("custom section", advance_parser, size);
			break;
		case SECTION_ID_TYPE:
		case SECTION_ID_IMPORT:
		case SECTION_ID_FUNCTION:
		case SECTION_ID_TABLE:
		case SECTION_ID_MEMORY:
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
