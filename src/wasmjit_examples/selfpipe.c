/* -*-mode:c; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
  System-call heavy microbenchmark to show off speed of wasmjit.
 */

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

#include <stdio.h>

#include <unistd.h>

char buf[4096];

int main(int argc, char *argv[])
{
	int pipes[2], ret;
	size_t i;

	ret = pipe(pipes);
	if (ret) {
		perror("pipe");
		return -1;
	}

	for (i = 0; i < 16ULL * 1024ULL * 1024ULL * 1024 / 4096; ++i) {
		ret = write(pipes[1], buf, sizeof(buf));
		if (ret < 0) {
			perror("write");
			return -1;
		}

		ret = read(pipes[0], buf, sizeof(buf));
		if (ret < 0) {
			perror("read");
		}
	}

	return 0;
}
