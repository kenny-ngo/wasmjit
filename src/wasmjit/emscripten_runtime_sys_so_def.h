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

#ifdef SO_DEBUG
SO(DEBUG, 1, INT)
#endif
#ifdef SO_REUSEADDR
SO(REUSEADDR, 2, INT)
#endif
#ifdef SO_TYPE
SO(TYPE, 3, INT)
#endif
#ifdef SO_ERROR
SO(ERROR, 4, INT)
#endif
#ifdef SO_DONTROUTE
SO(DONTROUTE, 5, INT)
#endif
#ifdef SO_BROADCAST
SO(BROADCAST, 6, INT)
#endif
#ifdef SO_SNDBUF
SO(SNDBUF, 7, INT)
#endif
#ifdef SO_RCVBUF
SO(RCVBUF, 8, INT)
#endif
#ifdef SO_SNDBUFFORCE
SO(SNDBUFFORCE, 32, INT)
#endif
#ifdef SO_RCVBUFFORCE
SO(RCVBUFFORCE, 33, INT)
#endif
#ifdef SO_KEEPALIVE
SO(KEEPALIVE, 9, INT)
#endif
#ifdef SO_OOBINLINE
SO(OOBINLINE, 10, INT)
#endif
#ifdef SO_NO_CHECK
SO(NO_CHECK, 11, INT)
#endif
#ifdef SO_PRIORITY
SO(PRIORITY, 12, INT)
#endif
#ifdef SO_LINGER
SO(LINGER, 13, LINGER)
#endif
#ifdef SO_BSDCOMPAT
SO(BSDCOMPAT, 14, INT)
#endif
#ifdef SO_REUSEPORT
SO(REUSEPORT, 15, INT)
#endif
#ifdef SO_PASSCRED
SO(PASSCRED, 16, INT)
#endif
#ifdef SO_PEERCRED
SO(PEERCRED, 17, UCRED)
#endif
#ifdef SO_RCVLOWAT
SO(RCVLOWAT, 18, INT)
#endif
#ifdef SO_SNDLOWAT
SO(SNDLOWAT, 19, INT)
#endif
#ifdef SO_RCVTIMEO
SO(RCVTIMEO, 20, TIMEVAL)
#endif
#ifdef SO_SNDTIMEO
SO(SNDTIMEO, 21, TIMEVAL)
#endif
#ifdef SO_SECURITY_AUTHENTICATION
SO(SECURITY_AUTHENTICATION, 22, INT)
#endif
#ifdef SO_SECURITY_ENCRYPTION_TRANSPORT
SO(SECURITY_ENCRYPTION_TRANSPORT, 23, INT)
#endif
#ifdef SO_SECURITY_ENCRYPTION_NETWORK
SO(SECURITY_ENCRYPTION_NETWORK, 24, INT)
#endif
#ifdef SO_BINDTODEVICE
SO(BINDTODEVICE, 25, STRING)
#endif
#ifdef SO_ATTACH_FILTER
SO(ATTACH_FILTER, 26, INT)
#endif
#ifdef SO_DETACH_FILTER
SO(DETACH_FILTER, 27, INT)
#endif
#if defined(SO_GET_FILTER) && SO_GET_FILTER != SO_ATTACH_FILTER
SO(GET_FILTER, 26, INT)
#endif
#ifdef SO_PEERNAME
SO(PEERNAME, 28, INT)
#endif
#ifdef SO_TIMESTAMP
SO(TIMESTAMP, 29, INT)
#endif
#ifdef SO_ACCEPTCONN
SO(ACCEPTCONN, 30, INT)
#endif
#ifdef SO_PEERSEC
SO(PEERSEC, 31, INT)
#endif
#ifdef SO_PASSSEC
SO(PASSSEC, 34, INT)
#endif
#ifdef SO_TIMESTAMPNS
SO(TIMESTAMPNS, 35, INT)
#endif
#ifdef SO_MARK
SO(MARK, 36, INT)
#endif
#ifdef SO_TIMESTAMPING
SO(TIMESTAMPING, 37, INT)
#endif
#ifdef SO_PROTOCOL
SO(PROTOCOL, 38, INT)
#endif
#ifdef SO_DOMAIN
SO(DOMAIN, 39, INT)
#endif
#ifdef SO_RXQ_OVFL
SO(RXQ_OVFL, 40, INT)
#endif
#ifdef SO_WIFI_STATUS
SO(WIFI_STATUS, 41, INT)
#endif
#ifdef SO_PEEK_OFF
SO(PEEK_OFF, 42, INT)
#endif
#ifdef SO_NOFCS
SO(NOFCS, 43, INT)
#endif
#ifdef SO_LOCK_FILTER
SO(LOCK_FILTER, 44, INT)
#endif
#ifdef SO_SELECT_ERR_QUEUE
SO(SELECT_ERR_QUEUE, 45, INT)
#endif
#ifdef SO_BUSY_POLL
SO(BUSY_POLL, 46, INT)
#endif
#ifdef SO_MAX_PACING_RATE
SO(MAX_PACING_RATE, 47, INT)
#endif
#ifdef SO_BPF_EXTENSIONS
SO(BPF_EXTENSIONS, 48, INT)
#endif
#ifdef SO_INCOMING_CPU
SO(INCOMING_CPU, 49, INT)
#endif
#ifdef SO_ATTACH_BPF
SO(ATTACH_BPF, 50, INT)
#endif
#if defined(SO_DETACH_BPF) && SO_DETACH_BPF != SO_DETACH_FILTER
SO(DETACH_BPF, 27, INT)
#endif
#ifdef SO_ATTACH_REUSEPORT_CBPF
SO(ATTACH_REUSEPORT_CBPF, 51, INT)
#endif
#ifdef SO_ATTACH_REUSEPORT_EBPF
SO(ATTACH_REUSEPORT_EBPF, 52, INT)
#endif
#ifdef SO_CNX_ADVICE
SO(CNX_ADVICE, 53, INT)
#endif
#ifdef SO_MEMINFO
SO(MEMINFO, 55, INT)
#endif
#ifdef SO_INCOMING_NAPI_ID
SO(INCOMING_NAPI_ID, 56, INT)
#endif
#ifdef SO_COOKIE
SO(COOKIE, 57, INT)
#endif
#ifdef SO_PEERGROUPS
SO(PEERGROUPS, 59, INT)
#endif
#ifdef SO_ZEROCOPY
SO(ZEROCOPY, 60, INT)
#endif
