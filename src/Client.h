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
#ifndef TUT_CLIENT_H_
#define TUT_CLIENT_H_

#include "Common.h"

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/listener.h>

#include "ikcp.h"


class ClientTCPSession;
class Client;



//////////////////////////////// ClientTCPSession //////////////////////////////
class ClientTCPSession {
  struct bufferevent *bev_;

public:
  Client *client_;
  uint16_t connIdx_;  // connection index

public:
  ClientTCPSession(const uint16_t connIdx, struct event_base *base,
                   evutil_socket_t fd, Client *client);
  ~ClientTCPSession();

	void setTimeout(const int32_t readTimeout, const int32_t writeTimeout);

  void recvData(struct evbuffer *buf);
  void sendData(const char *data, size_t len);
};



//////////////////////////////////// Client ////////////////////////////////////
class Client {
  // libevent2
  struct event_base *base_;
  struct event *exitEvTimer_;        // deley to stop server when exit
  struct event *kcpUpdateTimer_;     // call ikcp_update() interval
  struct event *kcpKeepAliveTimer_;  // kcp keep-alive

  // upstream udp
  int      udpSockFd_;
  string   udpUpstreamHost_;
  uint16_t udpUpstreamPort_;
  struct sockaddr_in udpUpstreamAddr_;
  struct event *udpReadEvent_;

  // listen tcp
  struct evconnlistener *listener_;
  string   listenIP_;
  uint16_t listenPort_;

  // timeout
  int32_t tcpReadTimeout_;
  int32_t tcpWriteTimeout_;

  // KDP connection
  bool isInitKCPConv_;
  uint32_t kcpConv_;
  struct evbuffer *kcpInBuf_;

  // idx -> conn
  map<uint16_t, ClientTCPSession *> conns_;

  bool readKcpMsg();
  void sendKcpMsg(const string &msg);
  void sendKcpCloseMsg(const uint16_t connIdx);

  void handleKcpMsg(const uint16_t connIdx, const char *data, size_t len);
  void handleKcpMsg_closeConn(const string &msg);

  void sendInitKCPConvPkg();

public:
  bool running_;
  ikcpcb *kcp_;

public:
  Client(const string &udpUpstreamHost, const uint16_t udpUpstreamPort,
         const string &listenIP, const uint16_t listenPort,
         const int32_t tcpReadTimeout, const int32_t tcpWriteTimeout);
  ~Client();

  bool setup();
  void run();
  void stop();
  void exitLoop();

  void checkInitKCP();
  void kcpUpdateManually();
  bool recvInitKCPConvPkg(const uint8_t *p);
  void kcpKeepAlive();

  static void listenerCallback(struct evconnlistener *listener,
                               evutil_socket_t fd,
                               struct sockaddr* saddr,
                               int socklen, void *ptr);

  void handleIncomingUDPMesasge(uint8_t *inData, size_t inDataSize);
  void handleIncomingTCPMesasge(ClientTCPSession *session, string &msg);

  void addConnection(ClientTCPSession *session);
  void removeConnection(ClientTCPSession *session, bool isNeedSendCloseMsg);

  int sendKcpDataLowLevel(const char *buf, int len, ikcpcb *kcp);

  static int  cb_kcpOutput(const char *buf, int len, ikcpcb *kcp, void *user);
  static void cb_udpRead  (evutil_socket_t fd, short events, void *ptr);
  static void cb_tcpRead  (struct bufferevent *bev, void *ptr);
  static void cb_tcpEvent (struct bufferevent *bev,
                           short events, void *ptr);

  static void cb_exitLoop(evutil_socket_t fd,
                          short events, void *ptr);
  static void cb_kcpUpdate(evutil_socket_t fd,
                           short events, void *ptr);
  static void cb_kcpKeepAlive(evutil_socket_t fd,
                              short events, void *ptr);
  static void cb_initKCP(evutil_socket_t fd,
                         short events, void *ptr);
};

#endif
