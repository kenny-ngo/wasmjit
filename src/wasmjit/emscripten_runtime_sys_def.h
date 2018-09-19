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

KWSC3(lseek, unsigned int, off_t, unsigned int)
KWSC3(writev, unsigned long, const struct iovec *, unsigned long)
KWSC3(write, unsigned int, void *, size_t)
KWSC1(close, unsigned int)
KWSC1(unlink, const char *)
KWSC3(socket, int, int, int)
KWSC3(bind, int, const struct sockaddr *, socklen_t)
KWSC3(connect, int, const struct sockaddr *, socklen_t)
KWSC2(listen, int, int)
KWSC3(accept, int, struct sockaddr *, socklen_t *)
KWSC3(getsockname, int, struct sockaddr *, socklen_t *)
KWSC3(getpeername, int, struct sockaddr *, socklen_t *)
KWSC6(sendto, int, const void *, size_t, int, const struct sockaddr *, socklen_t)
