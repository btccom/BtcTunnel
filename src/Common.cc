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
#include "Common.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/fcntl.h>
#include <sys/socket.h>

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/listener.h>


bool resolve(const string &host, struct	in_addr *sin_addr) {
  struct evutil_addrinfo *ai = NULL;
  struct evutil_addrinfo hints_in;
  memset(&hints_in, 0, sizeof(evutil_addrinfo));
  // AF_INET, v4; AF_INT6, v6; AF_UNSPEC, both v4 & v6
  hints_in.ai_family   = AF_UNSPEC;
  hints_in.ai_socktype = SOCK_STREAM;
  hints_in.ai_protocol = IPPROTO_TCP;
  hints_in.ai_flags    = EVUTIL_AI_ADDRCONFIG;

  // TODO: use non-blocking to resolve hostname
  int err = evutil_getaddrinfo(host.c_str(), NULL, &hints_in, &ai);
  if (err != 0) {
    LOG(ERROR) << "evutil_getaddrinfo err: " << err << ", " << evutil_gai_strerror(err);
    return false;
  }
  if (ai == NULL) {
    LOG(ERROR) << "evutil_getaddrinfo res is null";
    return false;
  }

  // only get the first record, ignore ai = ai->ai_next
  if (ai->ai_family == AF_INET) {
    struct sockaddr_in *sin = (struct sockaddr_in*)ai->ai_addr;
    *sin_addr = sin->sin_addr;

    char ipStr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(sin->sin_addr), ipStr, INET_ADDRSTRLEN);
    LOG(INFO) << "resolve host: " << host << ", ip: " << ipStr;
  } else if (ai->ai_family == AF_INET6) {
    // not support yet
    LOG(ERROR) << "not support ipv6 yet";
    return false;
  }
  evutil_freeaddrinfo(ai);
  return true;
}
