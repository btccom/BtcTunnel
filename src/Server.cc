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
#include "Server.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/fcntl.h>
#include <sys/socket.h>

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/listener.h>

static
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




//////////////////////////////// ServerSession /////////////////////////////////
ServerSession::ServerSession(const uint16_t connId, struct event_base *base,
                             Server *server)
// TODO: args
{
  bev_ = bufferevent_socket_new(base, -1, BEV_OPT_CLOSE_ON_FREE);
  assert(bev_ != nullptr);

  bufferevent_setcb(bev_,
                    Server::cb_tcpRead, nullptr,
                    Server::cb_tcpEvent, this);
  bufferevent_enable(bev_, EV_READ|EV_WRITE);
}

ServerSession::~ServerSession() {
  bufferevent_free(bev_);
}

bool ServerSession::connect(struct sockaddr_in &sin) {
  // bufferevent_socket_connect(): This function returns 0 if the connect
  // was successfully launched, and -1 if an error occurred.
  int res = bufferevent_socket_connect(bev_, (struct sockaddr *)&sin, sizeof(sin));
  if (res == 0) {
    state_ = INIT;
    return true;
  }

  return false;
}

void ServerSession::setTimeout(const int32_t readTimeout,
                               const int32_t writeTimeout) {
  // clear it
  bufferevent_set_timeouts(bev_, NULL, NULL);

  // set a new one
  struct timeval readtv  = {readTimeout, 0};
  struct timeval writetv = {writeTimeout, 0};
  bufferevent_set_timeouts(bev_,
                           readTimeout  > 0 ? &readtv  : nullptr,
                           writeTimeout > 0 ? &writetv : nullptr);
}

void ServerSession::recvData(struct evbuffer *buf) {
  string msg;
  msg.resize(evbuffer_get_length(buf));

  // copies and removes the first datlen bytes from the front of buf
  // into the memory at data
  evbuffer_remove(buf, (uint8_t *)msg.data(), msg.size());

  server_->handleIncomingTCPMesasge(this, msg);
}

void ServerSession::sendData(const char *data, size_t len) {
  // add data to a bufferevent’s output buffer
  bufferevent_write(bev_, data, len);
}




/////////////////////////////////// Server /////////////////////////////////////
Server::Server(const string &udpIP, const uint16_t udpPort)
// TODO: args
{
  base_ = event_base_new();
  assert(base_ != nullptr);

  kcp_ = ikcp_create(KCP_CONV_VALUE, this);
  kcp_->output = cb_kcpOutput;

  kcpInBuf_ = evbuffer_new();
  assert(kcpInBuf_ != nullptr);
}

Server::~Server() {
  // TODO
}

bool Server::listenUDP() {
  struct sockaddr_in sin;
  memset(&sin, 0, sizeof(sin));
  sin.sin_family      = AF_INET;
  sin.sin_port        = htons(udpPort_);
  sin.sin_addr.s_addr = htonl(INADDR_ANY);
  if (inet_pton(AF_INET, udpIP_.c_str(), &sin.sin_addr) == 0) {
    LOG(ERROR) << "invalid ip: " << udpIP_;
    return false;
  }

  // create socket
  udpSockFd_ = socket(AF_INET, SOCK_DGRAM, 0);
  if (udpSockFd_ == -1) {
    LOG(ERROR) << "create udp socket failure: " << strerror(errno);
    return false;
  }

  // bind address
  if (bind(udpSockFd_, (struct sockaddr *) &sin, sizeof(sin)) == -1) {
    LOG(ERROR) << "bind udp socket failure: " << strerror(errno);
    return false;
  }

  // make non-blocking
  fcntl(udpSockFd_, F_SETFL, O_NONBLOCK);

  // add event
  udpReadEvent_ = event_new(base_, udpSockFd_, EV_READ|EV_PERSIST,
                            cb_udpRead, this);
  event_add(udpReadEvent_, nullptr);

  LOG(INFO) << "listen on udp: " << udpIP_ << ":" << udpPort_;
  return true;
}

void Server::removeUpConnection(ServerSession *session,
                                bool isNeedSendCloseMsg) {
  if (isNeedSendCloseMsg)
    sendKcpCloseMsg(session->connIdx_);

  conns_.erase(session->connIdx_);
  delete session;
}

void Server::handleIncomingUDPMesasge(struct sockaddr_in *sin,
                                      socklen_t addrSize,
                                      uint8_t *inData, size_t inDataSize) {
  // copy the latest client address
  targetAddr_     = *sin;
  targetAddrsize_ = addrSize;

  // add to kcp coming evbuf
  evbuffer_add(kcpInBuf_, inData, inDataSize);

  while (readKcpMsg()) {
  }
}

bool Server::readKcpMsg() {
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

void Server::sendKcpMsg(const string &msg) {
  int res = ikcp_send(kcp_, msg.data(), (int)msg.size());
  if (res < 0) {
    // should not happen
    LOG(FATAL) << "kcp send error: " << res;
  }
}

void Server::handleKcpMsg(const uint16_t connIdx, const char *data, size_t len) {
  auto itr = conns_.find(connIdx);

  if (itr == conns_.end()) {
    // resolue upstream host
    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port   = htons(tcpUpstreamPort_);
    if (!resolve(tcpUpstreamHost_, &sin.sin_addr)) {
      goto error;
    }

    ServerSession *s = new ServerSession(connIdx, base_, this);
    if (s->connect(sin)) {
      delete s;
      goto error;
    }

    // connect success
    conns_.insert(std::make_pair(connIdx, s));
    itr = conns_.find(connIdx);
  }
  assert(itr != conns_.end());

  itr->second->sendData(data, len);
  return;

error:
  // send kcp msg to tell Client close the conn
  sendKcpCloseMsg(connIdx);
}

void Server::handleKcpMsg_closeConn(const string &msg) {
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

  removeUpConnection(itr->second, false);
}

void Server::handleIncomingTCPMesasge(ServerSession *session,
                                      string &msg) {
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

void Server::sendKcpCloseMsg(const uint16_t connIdx) {
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

void Server::cb_udpRead(evutil_socket_t fd, short events, void *ptr) {
  Server *server = static_cast<Server *>(ptr);

  // client's address
  struct sockaddr_in sin;
  socklen_t size = sizeof(sin);
  ssize_t res;
  char buf[MAX_MESSAGE_LEN];

  // These calls return the number of bytes received, or -1 if an error occurred.
  // The return value will be 0 when the peer has performed an orderly shutdown.
  res = recvfrom(fd, &buf, sizeof(buf), 0, (struct sockaddr *)&sin, &size);
  if (res == -1) {
    LOG(ERROR) << "recvfrom error, return: " << res;
    return;
  }

  server->handleIncomingUDPMesasge(&sin, size, (uint8_t *)buf, res);
}

void Server::cb_tcpRead(struct bufferevent *bev, void *ptr) {
  static_cast<ServerSession *>(ptr)->recvData(bufferevent_get_input(bev));
}

void Server::cb_tcpEvent(struct bufferevent *bev, short events, void *ptr) {
  ServerSession *session = static_cast<ServerSession *>(ptr);
  Server *server = session->server_;

  if (events & BEV_EVENT_CONNECTED) {
    session->state_ = ServerSession::CONNECTED;
    return;
  }

  if (events & BEV_EVENT_EOF) {
    LOG(INFO) << "tcp upsession closed";
  }
  else if (events & BEV_EVENT_ERROR) {
    LOG(INFO) << "got an error on tcp upsession: "
    << evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR());
  }
  else if (events & BEV_EVENT_TIMEOUT) {
    LOG(INFO) << "upsession read/write timeout, events: " << events;
  }
  else {
    LOG(ERROR) << "unhandled upsession events: " << events;
  }

  // remove up tcp session
  server->removeUpConnection(session, true /* send close msg to client */);
}



