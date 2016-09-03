/*
 MIT License

 Copyright (c) 2016 BTC.COM

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
#ifndef COMMON_H_
#define COMMON_H_

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cstdint>

#include <time.h>
#include <netinet/in.h>
#include <sys/time.h>

#include <string>
#include <map>

#include <glog/logging.h>

#include "ikcp.h"

#define MAX_MESSAGE_LEN 1500
#define KCP_CONV_DEFAULT_VALUE  0xFFFFFFFFu

#define KCP_MSG_CONNIDX_NONE      0x0000u
#define KCP_MSG_TYPE_CLOSE_CONN   0x01u     // close connection


using std::string;
using std::map;

bool resolve(const string &host, struct	in_addr *sin_addr);

/* get system time */
static inline void itimeofday(long *sec, long *usec) {
  struct timeval time;
  gettimeofday(&time, NULL);
  if (sec) *sec = time.tv_sec;
  if (usec) *usec = time.tv_usec;
}
/* get clock in millisecond 64 */
inline IINT64 iclock64(void) {
  long s, u;
  IINT64 value;
  itimeofday(&s, &u);
  value = ((IINT64)s) * 1000 + (u / 1000);
  return value;
}
inline IUINT32 iclock() {
  return (IUINT32)(iclock64() & 0xfffffffful);
}

#endif
