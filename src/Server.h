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
#ifndef TUT_SERVER_H_
#define TUT_SERVER_H_

#include "Common.h"

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/listener.h>

#include "ikcp.h"


class ServerTCPSession;
class Server;


/////////////////////////////// ServerTCPSession ///////////////////////////////
class ServerTCPSession {
  struct bufferevent *bev_;

public:
  Server *server_;
  uint16_t connIdx_;  // connection index

public:
  ServerTCPSession(const uint16_t connIdx, struct event_base *base, Server *server);
  ~ServerTCPSession();

  bool connect(struct sockaddr_in &sin);
  void setTimeout(const int32_t readTimeout, const int32_t writeTimeout);

  void recvData(struct evbuffer *buf);
  void sendData(const char *data, size_t len);
};



/////////////////////////////////// Server /////////////////////////////////////
class Server {
  bool running_;

  // libevent2
  struct event_base *base_;
  struct event *exitEvTimer_;     // deley to stop server when exit
  struct event *kcpUpdateTimer_;  // call ikcp_update() interval

  // listen udp
  string   udpIP_;
  uint16_t udpPort_;
  int      udpSockFd_;
  struct event *udpReadEvent_;

  // KDP connection
  struct evbuffer *kcpInBuf_;

  // idx -> conn
  map<uint16_t, ServerTCPSession *> conns_;

  string   tcpUpstreamHost_;
  uint16_t tcpUpstreamPort_;

  // timeout
  int32_t  tcpReadTimeout_;
  int32_t  tcpWriteTimeout_;

  // target addr
  struct sockaddr_in targetAddr_;
  socklen_t targetAddrsize_;

  bool readKcpMsg();
  void sendKcpMsg(const string &msg);
  void sendKcpCloseMsg(const uint16_t connIdx);

  void handleKcpMsg(const uint16_t connIdx, const char *data, size_t len);
  void handleKcpMsg_closeConn(const string &msg);

public:
  ikcpcb *kcp_;

public:
  Server(const string &udpIP, const uint16_t udpPort,
         const string &tcpUpstreamHost, const uint16_t tcpUpstreamPort,
         const int32_t tcpReadTimeout, const int32_t tcpWriteTimeout);
  ~Server();

  bool setup();
  void stop();
  void exitLoop();

  void kcpUpdateManually();

  void removeUpConnection(ServerTCPSession *session, bool isNeedSendCloseMsg);

  void handleIncomingUDPMesasge(struct sockaddr_in *sin, socklen_t addrSize,
                                uint8_t *inData, size_t inDataSize);
  void handleIncomingTCPMesasge(ServerTCPSession *session, string &msg);

  int sendKcpDataLowLevel(const char *buf, int len, ikcpcb *kcp);

  static int  cb_kcpOutput(const char *buf, int len, ikcpcb *kcp, void *ptr);
  static void cb_udpRead  (evutil_socket_t fd, short events, void *ptr);
  static void cb_tcpRead  (struct bufferevent *bev, void *ptr);
  static void cb_tcpEvent (struct bufferevent *bev,
                           short events, void *ptr);

  static void cb_exitLoop(evutil_socket_t fd,
                          short events, void *ptr);
  static void cb_kcpUpdate(evutil_socket_t fd,
                           short events, void *ptr);
};

#endif
