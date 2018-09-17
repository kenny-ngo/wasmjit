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

#ifdef EPERM
ERRNO(EPERM, 1)
#endif
#ifdef ENOENT
ERRNO(ENOENT, 2)
#endif
#ifdef ESRCH
ERRNO(ESRCH, 3)
#endif
#ifdef EINTR
ERRNO(EINTR, 4)
#endif
#ifdef EIO
ERRNO(EIO, 5)
#endif
#ifdef ENXIO
ERRNO(ENXIO, 6)
#endif
#ifdef E2BIG
ERRNO(E2BIG, 7)
#endif
#ifdef ENOEXEC
ERRNO(ENOEXEC, 8)
#endif
#ifdef EBADF
ERRNO(EBADF, 9)
#endif
#ifdef ECHILD
ERRNO(ECHILD, 10)
#endif
#ifdef EAGAIN
ERRNO(EAGAIN, 11)
#endif
#ifdef ENOMEM
ERRNO(ENOMEM, 12)
#endif
#ifdef EACCES
ERRNO(EACCES, 13)
#endif
#ifdef EFAULT
ERRNO(EFAULT, 14)
#endif
#ifdef ENOTBLK
ERRNO(ENOTBLK, 15)
#endif
#ifdef EBUSY
ERRNO(EBUSY, 16)
#endif
#ifdef EEXIST
ERRNO(EEXIST, 17)
#endif
#ifdef EXDEV
ERRNO(EXDEV, 18)
#endif
#ifdef ENODEV
ERRNO(ENODEV, 19)
#endif
#ifdef ENOTDIR
ERRNO(ENOTDIR, 20)
#endif
#ifdef EISDIR
ERRNO(EISDIR, 21)
#endif
#ifdef EINVAL
ERRNO(EINVAL, 22)
#endif
#ifdef ENFILE
ERRNO(ENFILE, 23)
#endif
#ifdef EMFILE
ERRNO(EMFILE, 24)
#endif
#ifdef ENOTTY
ERRNO(ENOTTY, 25)
#endif
#ifdef ETXTBSY
ERRNO(ETXTBSY, 26)
#endif
#ifdef EFBIG
ERRNO(EFBIG, 27)
#endif
#ifdef ENOSPC
ERRNO(ENOSPC, 28)
#endif
#ifdef ESPIPE
ERRNO(ESPIPE, 29)
#endif
#ifdef EROFS
ERRNO(EROFS, 30)
#endif
#ifdef EMLINK
ERRNO(EMLINK, 31)
#endif
#ifdef EPIPE
ERRNO(EPIPE, 32)
#endif
#ifdef EDOM
ERRNO(EDOM, 33)
#endif
#ifdef ERANGE
ERRNO(ERANGE, 34)
#endif
#ifdef EDEADLK
ERRNO(EDEADLK, 35)
#endif
#ifdef ENAMETOOLONG
ERRNO(ENAMETOOLONG, 36)
#endif
#ifdef ENOLCK
ERRNO(ENOLCK, 37)
#endif
#ifdef ENOSYS
ERRNO(ENOSYS, 38)
#endif
#ifdef ENOTEMPTY
ERRNO(ENOTEMPTY, 39)
#endif
#ifdef ELOOP
ERRNO(ELOOP, 40)
#endif
#if defined(EWOULDBLOCK) && EWOULDBLOCK != EAGAIN
ERRNO(EWOULDBLOCK, 11)
#endif
#ifdef ENOMSG
ERRNO(ENOMSG, 42)
#endif
#ifdef EIDRM
ERRNO(EIDRM, 43)
#endif
#ifdef ECHRNG
ERRNO(ECHRNG, 44)
#endif
#ifdef EL2NSYNC
ERRNO(EL2NSYNC, 45)
#endif
#ifdef EL3HLT
ERRNO(EL3HLT, 46)
#endif
#ifdef EL3RST
ERRNO(EL3RST, 47)
#endif
#ifdef ELNRNG
ERRNO(ELNRNG, 48)
#endif
#ifdef EUNATCH
ERRNO(EUNATCH, 49)
#endif
#ifdef ENOCSI
ERRNO(ENOCSI, 50)
#endif
#ifdef EL2HLT
ERRNO(EL2HLT, 51)
#endif
#ifdef EBADE
ERRNO(EBADE, 52)
#endif
#ifdef EBADR
ERRNO(EBADR, 53)
#endif
#ifdef EXFULL
ERRNO(EXFULL, 54)
#endif
#ifdef ENOANO
ERRNO(ENOANO, 55)
#endif
#ifdef EBADRQC
ERRNO(EBADRQC, 56)
#endif
#ifdef EBADSLT
ERRNO(EBADSLT, 57)
#endif
#if defined(EDEADLOCK) && EDEADLOCK != EDEADLK
ERRNO(EDEADLOCK, 35)
#endif
#ifdef EBFONT
ERRNO(EBFONT, 59)
#endif
#ifdef ENOSTR
ERRNO(ENOSTR, 60)
#endif
#ifdef ENODATA
ERRNO(ENODATA, 61)
#endif
#ifdef ETIME
ERRNO(ETIME, 62)
#endif
#ifdef ENOSR
ERRNO(ENOSR, 63)
#endif
#ifdef ENONET
ERRNO(ENONET, 64)
#endif
#ifdef ENOPKG
ERRNO(ENOPKG, 65)
#endif
#ifdef EREMOTE
ERRNO(EREMOTE, 66)
#endif
#ifdef ENOLINK
ERRNO(ENOLINK, 67)
#endif
#ifdef EADV
ERRNO(EADV, 68)
#endif
#ifdef ESRMNT
ERRNO(ESRMNT, 69)
#endif
#ifdef ECOMM
ERRNO(ECOMM, 70)
#endif
#ifdef EPROTO
ERRNO(EPROTO, 71)
#endif
#ifdef EMULTIHOP
ERRNO(EMULTIHOP, 72)
#endif
#ifdef EDOTDOT
ERRNO(EDOTDOT, 73)
#endif
#ifdef EBADMSG
ERRNO(EBADMSG, 74)
#endif
#ifdef EOVERFLOW
ERRNO(EOVERFLOW, 75)
#endif
#ifdef ENOTUNIQ
ERRNO(ENOTUNIQ, 76)
#endif
#ifdef EBADFD
ERRNO(EBADFD, 77)
#endif
#ifdef EREMCHG
ERRNO(EREMCHG, 78)
#endif
#ifdef ELIBACC
ERRNO(ELIBACC, 79)
#endif
#ifdef ELIBBAD
ERRNO(ELIBBAD, 80)
#endif
#ifdef ELIBSCN
ERRNO(ELIBSCN, 81)
#endif
#ifdef ELIBMAX
ERRNO(ELIBMAX, 82)
#endif
#ifdef ELIBEXEC
ERRNO(ELIBEXEC, 83)
#endif
#ifdef EILSEQ
ERRNO(EILSEQ, 84)
#endif
#ifdef ERESTART
ERRNO(ERESTART, 85)
#endif
#ifdef ESTRPIPE
ERRNO(ESTRPIPE, 86)
#endif
#ifdef EUSERS
ERRNO(EUSERS, 87)
#endif
#ifdef ENOTSOCK
ERRNO(ENOTSOCK, 88)
#endif
#ifdef EDESTADDRREQ
ERRNO(EDESTADDRREQ, 89)
#endif
#ifdef EMSGSIZE
ERRNO(EMSGSIZE, 90)
#endif
#ifdef EPROTOTYPE
ERRNO(EPROTOTYPE, 91)
#endif
#ifdef ENOPROTOOPT
ERRNO(ENOPROTOOPT, 92)
#endif
#ifdef EPROTONOSUPPORT
ERRNO(EPROTONOSUPPORT, 93)
#endif
#ifdef ESOCKTNOSUPPORT
ERRNO(ESOCKTNOSUPPORT, 94)
#endif
#ifdef EOPNOTSUPP
ERRNO(EOPNOTSUPP, 95)
#endif
#ifdef EPFNOSUPPORT
ERRNO(EPFNOSUPPORT, 96)
#endif
#ifdef EAFNOSUPPORT
ERRNO(EAFNOSUPPORT, 97)
#endif
#ifdef EADDRINUSE
ERRNO(EADDRINUSE, 98)
#endif
#ifdef EADDRNOTAVAIL
ERRNO(EADDRNOTAVAIL, 99)
#endif
#ifdef ENETDOWN
ERRNO(ENETDOWN, 100)
#endif
#ifdef ENETUNREACH
ERRNO(ENETUNREACH, 101)
#endif
#ifdef ENETRESET
ERRNO(ENETRESET, 102)
#endif
#ifdef ECONNABORTED
ERRNO(ECONNABORTED, 103)
#endif
#ifdef ECONNRESET
ERRNO(ECONNRESET, 104)
#endif
#ifdef ENOBUFS
ERRNO(ENOBUFS, 105)
#endif
#ifdef EISCONN
ERRNO(EISCONN, 106)
#endif
#ifdef ENOTCONN
ERRNO(ENOTCONN, 107)
#endif
#ifdef ESHUTDOWN
ERRNO(ESHUTDOWN, 108)
#endif
#ifdef ETOOMANYREFS
ERRNO(ETOOMANYREFS, 109)
#endif
#ifdef ETIMEDOUT
ERRNO(ETIMEDOUT, 110)
#endif
#ifdef ECONNREFUSED
ERRNO(ECONNREFUSED, 111)
#endif
#ifdef EHOSTDOWN
ERRNO(EHOSTDOWN, 112)
#endif
#ifdef EHOSTUNREACH
ERRNO(EHOSTUNREACH, 113)
#endif
#ifdef EALREADY
ERRNO(EALREADY, 114)
#endif
#ifdef EINPROGRESS
ERRNO(EINPROGRESS, 115)
#endif
#ifdef ESTALE
ERRNO(ESTALE, 116)
#endif
#ifdef EUCLEAN
ERRNO(EUCLEAN, 117)
#endif
#ifdef ENOTNAM
ERRNO(ENOTNAM, 118)
#endif
#ifdef ENAVAIL
ERRNO(ENAVAIL, 119)
#endif
#ifdef EISNAM
ERRNO(EISNAM, 120)
#endif
#ifdef EREMOTEIO
ERRNO(EREMOTEIO, 121)
#endif
#ifdef EDQUOT
ERRNO(EDQUOT, 122)
#endif
#ifdef ENOMEDIUM
ERRNO(ENOMEDIUM, 123)
#endif
#ifdef EMEDIUMTYPE
ERRNO(EMEDIUMTYPE, 124)
#endif
#ifdef ECANCELED
ERRNO(ECANCELED, 125)
#endif
#ifdef ENOKEY
ERRNO(ENOKEY, 126)
#endif
#ifdef EKEYEXPIRED
ERRNO(EKEYEXPIRED, 127)
#endif
#ifdef EKEYREVOKED
ERRNO(EKEYREVOKED, 128)
#endif
#ifdef EKEYREJECTED
ERRNO(EKEYREJECTED, 129)
#endif
#ifdef EOWNERDEAD
ERRNO(EOWNERDEAD, 130)
#endif
#ifdef ENOTRECOVERABLE
ERRNO(ENOTRECOVERABLE, 131)
#endif
