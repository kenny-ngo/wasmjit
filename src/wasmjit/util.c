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

#include <wasmjit/util.h>

#include <wasmjit/vector.h>

#include <wasmjit/sys.h>

DEFINE_VECTOR_GROW(buffer, struct SizedBuffer);

int output_buf(struct SizedBuffer *sstack, const void *buf,
	       size_t n_elts)
{
	if (!buffer_grow(sstack, n_elts))
		return 0;
	memcpy(sstack->elts + sstack->n_elts - n_elts, buf,
	       n_elts * sizeof(sstack->elts[0]));
	return 1;
}

#ifndef __KERNEL__

#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

#include <sys/stat.h>
#include <sys/types.h>

char *wasmjit_load_file(const char *filename, size_t *size)
{
	FILE *f = NULL;
	char *input = NULL;
	int fd = -1, ret;
	struct stat st;
	size_t rets;

	fd = open(filename, O_RDONLY);
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
	if (rets != (size_t) st.st_size) {
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

void wasmjit_unload_file(char *buf, size_t size)
{
	free(buf);
	(void) size;
}

#else

#include <linux/fs.h>

char *wasmjit_load_file(const char *file_name, size_t *size)
{
	void *buf;
	loff_t offsize;
	int ret;
	ret = kernel_read_file_from_path(file_name, &buf, &offsize,
					 INT_MAX, READING_UNKNOWN);
	if (ret < 0)
		return NULL;

	*size = offsize;
	return buf;
}

void wasmjit_unload_file(char *buf, size_t size)
{
	vfree(buf);
	(void) size;
}

#endif
