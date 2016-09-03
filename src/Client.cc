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
#include "Client.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/fcntl.h>
#include <sys/socket.h>

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/listener.h>

#include "ikcp.h"

//////////////////////////////// ClientTCPSession //////////////////////////////
ClientTCPSession::ClientTCPSession(const uint16_t connIdx,
                                   struct event_base *base,
                                   evutil_socket_t fd,
                                   Client *client) {
  bev_ = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE);
  assert(bev_ != nullptr);

  bufferevent_setcb(bev_,
                    Client::cb_tcpRead, NULL,
                    Client::cb_tcpEvent, (void*)this);

  // By default, a newly created bufferevent has writing enabled.
  bufferevent_enable(bev_, EV_READ|EV_WRITE);
}

ClientTCPSession::~ClientTCPSession() {
  // BEV_OPT_CLOSE_ON_FREE: fd will auto close
  bufferevent_free(bev_);
}

void ClientTCPSession::recvData(struct evbuffer *buf) {
  string msg;
  msg.resize(evbuffer_get_length(buf));

  // copies and removes the first datlen bytes from the front of buf
  // into the memory at data
  evbuffer_remove(buf, (uint8_t *)msg.data(), msg.size());

  client_->handleIncomingTCPMesasge(this, msg);
}

void ClientTCPSession::sendData(const char *data, size_t len) {
  // add data to a bufferevent’s output buffer
  bufferevent_write(bev_, data, len);
}


//////////////////////////////////// Client ////////////////////////////////////
Client::Client(const string &udpUpstreamHost, const uint16_t udpUpstreamPort) {
  base_ = event_base_new();
  assert(base_ != nullptr);

  kcp_ = ikcp_create(KCP_CONV_VALUE, this);
  kcp_->output = cb_kcpOutput;
  ikcp_wndsize(kcp_, 128, 128);  // set kcp windown size
  ikcp_nodelay(kcp_,
               1,  // enable nodelay
               10, // interval ms
               2,  // fastresend: 2
               1); // no traffic control

  kcpInBuf_ = evbuffer_new();
  assert(kcpInBuf_ != nullptr);
}

Client::~Client() {
  if (listener_)
    evconnlistener_free(listener_);

  evbuffer_free(kcpInBuf_);
  ikcp_release(kcp_);

  if (udpReadEvent_)
  	event_free(udpReadEvent_);
  if (exitEvTimer_)
    event_free(exitEvTimer_);

  event_base_free(base_);

  LOG(INFO) << "client closed";
}

void Client::stop() {
  if (!running_)
    return;

  running_ = false;

  LOG(INFO) << "stop tcp listener...";
  evconnlistener_disable(listener_);

  LOG(INFO) << "remove all tcp connections...";
  for (auto conn : conns_) {
    removeConnection(conn.second, true);
  }

  // stop server in N seconds, let it send close kcp msg to server
  LOG(INFO) << "closing client in 3 seconds...";
  exitEvTimer_ = evtimer_new(base_, Client::cb_exitLoop, this);
  struct timeval threeSec = {3, 0};
  event_add(exitEvTimer_, &threeSec);
}

void Client::cb_exitLoop(evutil_socket_t fd,
                         short events, void *ptr) {
  Client *client = static_cast<Client *>(ptr);
  client->exitLoop();
}

void Client::exitLoop() {
  event_base_loopexit(base_, NULL);
}

bool Client::setup() {
  //
  // create udp sock
  //
  udpSockFd_ = socket(AF_INET, SOCK_DGRAM, 0);
  if (udpSockFd_ == -1) {
    LOG(ERROR) << "create udp socket failure: " << strerror(errno);
    return false;
  }

  // make non-blocking
  fcntl(udpSockFd_, F_SETFL, O_NONBLOCK);

  // add event
  udpReadEvent_ = event_new(base_, udpSockFd_, EV_READ|EV_PERSIST,
                            cb_udpRead, this);
  event_add(udpReadEvent_, nullptr);

  //
  // listen tcp address
  //
  struct sockaddr_in sin;
  memset(&sin, 0, sizeof(sin));
  sin.sin_family = AF_INET;
  sin.sin_port   = htons(listenPort_);
  sin.sin_addr.s_addr = htonl(INADDR_ANY);
  if (inet_pton(AF_INET, listenIP_.c_str(), &sin.sin_addr) == 0) {
    LOG(ERROR) << "invalid ip: " << listenIP_;
    return false;
  }

  listener_ = evconnlistener_new_bind(base_,
                                      Client::listenerCallback,
                                      (void*)this,
                                      LEV_OPT_REUSEABLE|LEV_OPT_CLOSE_ON_FREE,
                                      // backlog, Set to -1 for a reasonable default
                                      -1,
                                      (struct sockaddr*)&sin, sizeof(sin));
  if(!listener_) {
    LOG(ERROR) << "cannot create listener: " << listenIP_ << ":" << listenPort_;
    return false;
  }

  //
  // KCP interval update
  //
  kcpUpdateTimer_ = event_new(base_, -1, EV_PERSIST,
                              Client::cb_kcpUpdate, this);
  struct timeval timer_10ms = {0, 10000};  // 10ms
  event_add(kcpUpdateTimer_, &timer_10ms);

  return true;
}

void Client::cb_kcpUpdate(evutil_socket_t fd,
                          short events, void *ptr) {
  Client *client = static_cast<Client *>(ptr);
  ikcp_update(client->kcp_, iclock());
}

void Client::kcpUpdateManually() {
  event_del(kcpUpdateTimer_);

  ikcp_update(kcp_, iclock());

  // set agagin
  struct timeval timer_10ms = {0, 10000};  // 10ms
  event_add(kcpUpdateTimer_, &timer_10ms);
}

void Client::listenerCallback(struct evconnlistener *listener,
                              evutil_socket_t fd,
                              struct sockaddr* saddr,
                              int socklen, void *ptr) {
  Client *client = static_cast<Client *>(ptr);
  struct event_base  *base = (struct event_base*)client->base_;

  uint16_t connIdx = 0u;  // TODO
  ClientTCPSession *csession = new ClientTCPSession(connIdx, base,
                                                    fd, client);
  client->addConnection(csession);
}

void Client::addConnection(ClientTCPSession *session) {
  conns_.insert(std::make_pair(session->connIdx_, session));
}

void Client::handleIncomingUDPMesasge(uint8_t *inData, size_t inDataSize) {
  ikcp_input(kcp_, (const char *)inData, inDataSize);

  char buf[2048];
  const int kLen = sizeof(buf);

  while (1) {
    int size = ikcp_recv(kcp_, buf, kLen);
    if (size < 0) break;

    // add to kcp coming evbuf
    evbuffer_add(kcpInBuf_, buf, size);
  }

  while (readKcpMsg()) {
  }

  kcpUpdateManually();
}

bool Client::readKcpMsg() {
  //
  // KCP Mesasge:
  // | len(2) | connIdx(2) | ... |
  //
  // if connIdx == 0: means it will be another type message:
  //
  // | len(2) | connIdx(2):0 | type(1) | ... |
  //

  const size_t evBufLen = evbuffer_get_length(kcpInBuf_);

  if (evBufLen < 4)  // length should at least 4 bytes
    return false;

  // copy the fist 4 bytes
  uint8_t buf[4];
  evbuffer_copyout(kcpInBuf_, buf, 4);

  const uint16_t msglen = *(uint16_t *)(buf);
  if (evBufLen < msglen)  // didn't received the whole message yet
    return false;

  const uint16_t connIdx = *(uint16_t *)(buf + 2);

  // copies and removes the first datlen bytes from the front of buf
  // into the memory at data
  string msg;
  msg.resize(msglen);
  evbuffer_remove(kcpInBuf_, (uint8_t *)msg.data(), msg.size());

  if (connIdx == KCP_MSG_CONNIDX_NONE) {
    //
    // option message
    //
    const uint8_t *p = (uint8_t *)msg.data();
    const uint8_t type = *(p + 4);
    if (type == KCP_MSG_TYPE_CLOSE_CONN) {
      handleKcpMsg_closeConn(msg);
    } else {
      LOG(ERROR) << "unkown kcp msg type: " << type;
    }
  }
  else
  {
    //
    // data message
    //
    handleKcpMsg(connIdx, msg.data() + 4, msg.size() - 4);
  }

  return true;  // read message success, return true
}

void Client::handleKcpMsg(const uint16_t connIdx,
                          const char *data, size_t len) {
  auto itr = conns_.find(connIdx);

  if (itr == conns_.end()) {
    // can't find conn at Client side, tell Server close this conn
    sendKcpCloseMsg(connIdx);
    return;
  }

  ClientTCPSession *csession = itr->second;  // alias
  csession->sendData(data, len);
}

void Client::handleKcpMsg_closeConn(const string &msg) {
  //
  // KCP_MSG_TYPE_CLOSE_CONN
  // | len(2) | 0x0000(2) | 0x01 | connIdx(2) |
  //
  const uint8_t *p = (uint8_t *)msg.data();
  const uint16_t connIdx = *(uint16_t *)(p + 5);

  auto itr = conns_.find(connIdx);
  if (itr == conns_.end()) {
    LOG(ERROR) << "handle close msg fail, can't find conn by Idx: " << connIdx;
    return;
  }

  removeConnection(itr->second, false);
}

void Client::removeConnection(ClientTCPSession *session,
                              bool isNeedSendCloseMsg) {
  if (isNeedSendCloseMsg)
    sendKcpCloseMsg(session->connIdx_);

  conns_.erase(session->connIdx_);
  delete session;
}

void Client::sendKcpCloseMsg(const uint16_t connIdx) {
  //
  // KCP Mesasge:
  // | len(2) | connIdx(2) | ... |
  //
  // if connIdx == 0: means it will be another type message:
  //
  // | len(2) | connIdx(2):0 | type(1) | ... |
  //
  // KCP_MSG_TYPE_CLOSE_CONN
  // | len(2) | 0x0000(2) | 0x01 | connIdx(2) |
  //

  // build message for kcp
  string kcpMsg;
  kcpMsg.resize(2 + 2 + 1 + 2);
  uint8_t *p = (uint8_t *)kcpMsg.data();

  // len
  *(uint16_t *)p = (uint16_t)kcpMsg.size();
  p += 2;

  // sepcial connIdx: 0
  *(uint16_t *)p = (uint16_t)KCP_MSG_CONNIDX_NONE;
  p += 2;

  // type
  *(uint8_t *)p++ = KCP_MSG_TYPE_CLOSE_CONN;

  // real connIdx
  *(uint16_t *)p = connIdx;
  p += 2;

  // send
  sendKcpMsg(kcpMsg);
}

void Client::sendKcpMsg(const string &msg) {
  // returns below zero for error
  int res = ikcp_send(kcp_, msg.data(), (int)msg.size());

  // should not happen
  if (res < 0) {
    LOG(FATAL) << "kcp send error: " << res;
  }

  kcpUpdateManually();
}

int Client::sendKcpDataLowLevel(const char *buf, int len, ikcpcb *kcp) {
  // On success, these calls return the number of characters sent.
  // On error, -1 is returned, and errno is set appropriately.
  ssize_t r = sendto(udpSockFd_, buf, (size_t)len, MSG_DONTWAIT,
                     (struct sockaddr *)&udpUpstreamAddr_,
                     sizeof(udpUpstreamAddr_));
  if (r == -1) {
    LOG(ERROR) << "sendto error: " << strerror(errno);
  }
  return (int)r;
}

void Client::handleIncomingTCPMesasge(ClientTCPSession *session, string &msg) {
  //
  // cause we use uint16_t as the kcp message length, so we can't send message
  // which over than 65535
  //
  const size_t maxMsgLen = UINT16_MAX - 4;

  while (msg.size() > 0) {
    size_t len = std::min(maxMsgLen, msg.size());
    assert(len < UINT16_MAX);

    //
    // KCP Mesasge:
    // | len(2) | connIdx(2) | ... |
    //

    // build message for kcp
    string kcpMsg;
    kcpMsg.resize(4 + len);
    assert(kcpMsg.size() <= UINT16_MAX);

    uint8_t *p = (uint8_t *)kcpMsg.data();

    // len
    *(uint16_t *)p = (uint16_t)kcpMsg.size();
    p += 2;

    // conn idx
    *(uint16_t *)p = (uint16_t)session->connIdx_;
    p += 2;

    // content
    memcpy(p, msg.data(), len);

    // send
    sendKcpMsg(kcpMsg);

    // remove the first `len` bytes from string
    msg.erase(msg.begin(), msg.begin() + len);
  } /* /while */
}

int Client::cb_kcpOutput(const char *buf, int len, ikcpcb *kcp, void *ptr) {
  Client *client = static_cast<Client *>(ptr);
  return client->sendKcpDataLowLevel(buf, len, kcp);
}

void Client::cb_udpRead(evutil_socket_t fd, short events, void *ptr) {
  Client *client = static_cast<Client *>(ptr);
  ssize_t res;
  char buf[MAX_MESSAGE_LEN];

  // These calls return the number of bytes received, or -1 if an error occurred.
  // The return value will be 0 when the peer has performed an orderly shutdown.
  res = recvfrom(fd, &buf, sizeof(buf), 0, nullptr, nullptr);
  if (res == -1) {
    LOG(ERROR) << "recvfrom error, return: " << res;
    return;
  }

  client->handleIncomingUDPMesasge((uint8_t *)buf, res);
}

void Client::cb_tcpRead(struct bufferevent *bev, void *ptr) {
  static_cast<ClientTCPSession *>(ptr)->recvData(bufferevent_get_input(bev));
}

void Client::cb_tcpEvent(struct bufferevent *bev,
                         short events, void *ptr) {
  ClientTCPSession *csession = static_cast<ClientTCPSession *>(ptr);
  Client *client = csession->client_;

  // should not be 'BEV_EVENT_CONNECTED'
  assert((events & BEV_EVENT_CONNECTED) != BEV_EVENT_CONNECTED);

  if (events & BEV_EVENT_EOF) {
    LOG(INFO) << "tcp downsession closed";
  }
  else if (events & BEV_EVENT_ERROR) {
    LOG(INFO) << "got an error on tcp downsession: "
    << evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR());
  }
  else if (events & BEV_EVENT_TIMEOUT) {
    LOG(INFO) << "downsession read/write timeout, events: " << events;
  }
  else {
    LOG(ERROR) << "unhandled downsession events: " << events;
  }

  // remove up tcp session
  client->removeConnection(csession, true /* send close msg to server */);
}



