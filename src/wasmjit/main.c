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

struct ParseState {
	char *input;
	size_t amt_left;
};

int read_uint32_t(struct ParseState *pstate, uint32_t * data)
{
	if (pstate->amt_left < sizeof(uint32_t)) {
		return 0;
	}

	memcpy(data, pstate->input, sizeof(*data));
	pstate->amt_left -= sizeof(uint32_t);

	return 1;
}

int read_magic(struct ParseState *pstate, uint32_t * magic)
{
	return read_uint32_t(pstate, magic);
}

int main(int argc, char *argv[])
{
	ssize_t offset;
	uint32_t magic;
	size_t input_size;
	int ret;
	struct ParseState pstate;

	if (argc < 2) {
		printf("Need an input file\n");
		return -1;
	}

	pstate.input = load_file(argv[1], &pstate.amt_left);
	if (!pstate.input) {
		printf("Error loading file %s\n", strerror(errno));
		return -1;
	}

	ret = read_magic(&pstate, &magic);
	if (!ret) {
		printf("Error reading magic %s\n", strerror(errno));
		return -1;
	}

	if (magic != WASM_MAGIC) {
		printf("Bad WASM magic 0x%" PRIx32 " vs 0x%" PRIx32 "\n", magic,
		       WASM_MAGIC);
		return -1;
	}

	/* */

	return 0;
}
