/* Copyright(C) 2004-2007 Brazil

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include "senna_in.h"

#include <string.h>
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif /* HAVE_NETDB_H */
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif /* HAVE_SYS_SOCKET_H */
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif /* HAVE_NETINET_IN_H */
#ifdef HAVE_NETINET_TCP_H
#include <netinet/tcp.h>
#endif /* HAVE_NETINET_TCP_H */
#ifdef HAVE_SIGNAL_H
#include <signal.h>
#endif /* HAVE_SIGNAL_H */

#include "com.h"
#include "set.h"
#include "ctx.h"

#ifndef PF_INET
#define PF_INET AF_INET
#endif /* PF_INET */

#ifdef USE_MSG_NOSIGNAL
#if __FreeBSD__ >= 2 && __FreeBSD_version >= 600020
#define MSG_NOSIGNAL 0x20000
#endif /* __FreeBSD__ >= 2 && __FreeBSD_version >= 600020 */
#else /* USE_MSG_NOSIGNAL */
#define MSG_NOSIGNAL 0
#endif /* USE_MSG_NOSIGNAL */

#ifdef WIN32
void
log_sockerr(const char *msg)
{
  int e = WSAGetLastError();
  const char *m;
  switch (e) {
  case WSANOTINITIALISED: m = "please call sen_com_init first"; break;
  case WSAEFAULT: m = "bad address"; break;
  case WSAEINVAL: m = "invalid argument"; break;
  case WSAEMFILE: m = "too many sockets"; break;
  case WSAEWOULDBLOCK: m = "operation would block"; break;
  case WSAENOTSOCK: m = "given fd is not socket fd"; break;
  case WSAEOPNOTSUPP: m = "operation is not supported"; break;
  case WSAEADDRINUSE: m = "address is already in use"; break;
  case WSAEADDRNOTAVAIL: m = "address is not available"; break;
  case WSAENETDOWN: m = "network is down"; break;
  case WSAENOBUFS: m = "no buffer"; break;
  case WSAEISCONN: m = "socket is already connected"; break;
  case WSAENOTCONN: m = "socket is not connected"; break;
  case WSAESHUTDOWN: m = "socket is already shutdowned"; break;
  case WSAETIMEDOUT: m = "connection time out"; break;
  case WSAECONNREFUSED: m = "connection refused"; break;
  default:
    SEN_LOG(sen_log_error, "%s: socket error (%d)", msg, e);
    return;
  }
  SEN_LOG(sen_log_error, "%s: %s", msg, m);
}
#define LOG_SOCKERR(m) log_sockerr((m))
#else /* WIN32 */
#define LOG_SOCKERR(m) SEN_LOG(sen_log_error, "%s: %s", (m), strerror(errno))
#endif /* WIN32 */

/******* sen_com ********/

sen_rc
sen_com_init(void)
{
#ifdef WIN32
  WSADATA wd;
  if (WSAStartup(MAKEWORD(2, 0), &wd) != 0) {
    GERR(sen_external_error, "WSAStartup failed");
  }
#else /* WIN32 */
#ifndef USE_MSG_NOSIGNAL
  if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
    GERR(sen_external_error, "signal");
  }
#endif /* USE_MSG_NOSIGNAL */
#endif /* WIN32 */
  return sen_gctx.rc;
}

void
sen_com_fin(void)
{
#ifdef WIN32
  WSACleanup();
#endif /* WIN32 */
}

sen_rc
sen_com_event_init(sen_com_event *ev, int max_nevents, int data_size)
{
  sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */
  sen_rc rc = sen_memory_exhausted;
  ev->max_nevents = max_nevents;
  if ((ev->set = sen_set_open(sizeof(sen_sock), data_size, 0))) {
#ifndef USE_SELECT
#ifdef USE_EPOLL
    if ((ev->events = SEN_MALLOC(sizeof(struct epoll_event) * max_nevents))) {
      if ((ev->epfd = epoll_create(max_nevents)) != -1) {
        rc = sen_success;
        goto exit;
      } else {
        LOG_SOCKERR("epoll_create");
        rc = sen_external_error;
      }
      SEN_FREE(ev->events);
    }
#else /* USE_EPOLL */
    if ((ev->events = SEN_MALLOC(sizeof(struct pollfd) * max_nevents))) {
      rc = sen_success;
      goto exit;
    }
#endif /* USE_EPOLL */
    sen_set_close(ev->set);
    ev->set = NULL;
    ev->events = NULL;
#else /* USE_SELECT */
    rc = sen_success;
#endif /* USE_SELECT */
  }
exit :
  return rc;
}

sen_rc
sen_com_event_fin(sen_com_event *ev)
{
  sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */
  if (ev->set) { sen_set_close(ev->set); }
#ifndef USE_SELECT
  if (ev->events) { SEN_FREE(ev->events); }
#endif /* USE_SELECT */
  return sen_success;
}

sen_rc
sen_com_event_add(sen_com_event *ev, sen_sock fd, int events, sen_com **com)
{
  sen_com *c;
  /* todo : expand events */
  if (!ev || ev->set->n_entries == ev->max_nevents) {
    if (ev) { SEN_LOG(sen_log_error, "too many connections (%d)", ev->max_nevents); }
    return sen_invalid_argument;
  }
#ifdef USE_EPOLL
  {
    struct epoll_event e;
    memset(&e, 0, sizeof(struct epoll_event));
    e.data.fd = (fd);
    e.events = (__uint32_t) events;
    if (epoll_ctl(ev->epfd, EPOLL_CTL_ADD, (fd), &e) == -1) {
      LOG_SOCKERR("epoll_ctl");
      return sen_external_error;
    }
  }
#endif /* USE_EPOLL*/
  if (sen_set_get(ev->set, &fd, (void **)&c)) {
    c->fd = fd;
    c->events = events;
    if (com) { *com = c; }
    return sen_success;
  }
  return sen_internal_error;
}

sen_rc
sen_com_event_mod(sen_com_event *ev, sen_sock fd, int events, sen_com **com)
{
  sen_com *c;
  if (!ev) { return sen_invalid_argument; }
  if (sen_set_at(ev->set, &fd, (void **)&c)) {
    if (c->fd != fd) {
      SEN_LOG(sen_log_error, "sen_com_event_mod fd unmatch %d != %d", c->fd, fd);
      return sen_invalid_format;
    }
    if (com) { *com = c; }
    if (c->events != events) {
#ifdef USE_EPOLL
      struct epoll_event e;
      memset(&e, 0, sizeof(struct epoll_event));
      e.data.fd = (fd);
      e.events = (__uint32_t) events;
      if (epoll_ctl(ev->epfd, EPOLL_CTL_MOD, (fd), &e) == -1) {
        LOG_SOCKERR("epoll_ctl");
        return sen_external_error;
      }
#endif /* USE_EPOLL*/
      c->events = events;
    }
    return sen_success;
  }
  return sen_internal_error;
}

sen_rc
sen_com_event_del(sen_com_event *ev, sen_sock fd)
{
  if (!ev) { return sen_invalid_argument; }
#ifdef USE_EPOLL
  {
    struct epoll_event e;
    memset(&e, 0, sizeof(struct epoll_event));
    e.data.fd = fd;
    if (epoll_ctl(ev->epfd, EPOLL_CTL_DEL, fd, &e) == -1) {
      LOG_SOCKERR("epoll_ctl");
      return sen_external_error;
    }
  }
#endif /* USE_EPOLL*/
  return sen_set_del(ev->set, sen_set_at(ev->set, &fd, NULL));
}

sen_rc
sen_com_event_poll(sen_com_event *ev, int timeout)
{
  int nevents;
  sen_com *com;
#ifdef USE_SELECT
  sen_sock *pfd;
  int nfds = 0;
  fd_set rfds;
  fd_set wfds;
  struct timeval tv;
  if (timeout >= 0) {
    tv.tv_sec = timeout / 1000;
    tv.tv_usec = (timeout % 1000) * 1000;
  }
  FD_ZERO(&rfds);
  FD_ZERO(&wfds);
  SEN_SET_EACH(ev->set, eh, &pfd, &com, {
    if ((com->events & SEN_COM_POLLIN)) { FD_SET(*pfd, &rfds); }
    if ((com->events & SEN_COM_POLLOUT)) { FD_SET(*pfd, &wfds); }
    if (*pfd > nfds) { nfds = *pfd; }
  });
  nevents = select(nfds + 1, &rfds, &wfds, NULL, (timeout >= 0) ? &tv : NULL);
  if (nevents < 0) {
#ifdef WIN32
    if (WSAGetLastError() == WSAEINTR) { return sen_success; }
#else /* WIN32 */
    if (errno == EINTR) { return sen_success; }
#endif /* WIN32 */
    LOG_SOCKERR("select");
    return sen_external_error;
  }
  if (timeout < 0 && !nevents) { SEN_LOG(sen_log_notice, "select returns 0 events"); }
  SEN_SET_EACH(ev->set, eh, &pfd, &com, {
    if (FD_ISSET(*pfd, &rfds)) { com->ev_in(ev, com); }
    if (FD_ISSET(*pfd, &wfds)) { com->ev_out(ev, com); }
  });
#else /* USE_SELECT */
#ifdef USE_EPOLL
  struct epoll_event *ep;
  nevents = epoll_wait(ev->epfd, ev->events, ev->max_nevents, timeout);
#else /* USE_EPOLL */
  int nfd = 0, *pfd;
  struct pollfd *ep = ev->events;
  SEN_SET_EACH(ev->set, eh, &pfd, &com, {
    ep->fd = *pfd;
    //    ep->events =(short) com->events;
    ep->events = POLLIN;
    ep->revents = 0;
    ep++;
    nfd++;
  });
  nevents = poll(ev->events, nfd, timeout);
#endif /* USE_EPOLL */
  if (nevents < 0) {
    if (errno == EINTR) { return sen_success; }
    LOG_SOCKERR("poll");
    return sen_external_error;
  }
  if (timeout < 0 && !nevents) { SEN_LOG(sen_log_notice, "poll returns 0 events"); }
  for (ep = ev->events; nevents; ep++) {
    int efd;
#ifdef USE_EPOLL
    efd = ep->data.fd;
    nevents--;
    if (!sen_set_at(ev->set, &efd, (void *)&com)) {
      SEN_LOG(sen_log_error, "fd(%d) not found in ev->set", efd);
      return sen_internal_error;
    }
    if ((ep->events & SEN_COM_POLLIN)) { com->ev_in(ev, com); }
    if ((ep->events & SEN_COM_POLLOUT)) { com->ev_out(ev, com); }
#else /* USE_EPOLL */
    efd = ep->fd;
    if (!(ep->events & ep->revents)) { continue; }
    nevents--;
    if (!sen_set_at(ev->set, &efd, (void *)&com)) {
      SEN_LOG(sen_log_error, "fd(%d) not found in ev->set", efd);
      return sen_internal_error;
    }
    if ((ep->revents & SEN_COM_POLLIN)) { com->ev_in(ev, com); }
    if ((ep->revents & SEN_COM_POLLOUT)) { com->ev_out(ev, com); }
#endif /* USE_EPOLL */
  }
#endif /* USE_SELECT */
  return sen_success;
}

/******* sen_com_sqtp ********/

sen_rc
sen_com_sqtp_send(sen_com_sqtp *cs, sen_com_sqtp_header *header, char *body)
{
  ssize_t ret, whole_size = sizeof(sen_com_sqtp_header) + header->size;
  header->proto = SEN_COM_PROTO_SQTP;
  if (cs->com.status == sen_com_closing) { header->flags |= SEN_CTX_QUIT; }
  SEN_LOG(sen_log_info, "send (%d,%x,%d,%02x,%02x,%04x,%08x)", header->size, header->flags, header->proto, header->qtype, header->level, header->status, header->info);

  if (header->size) {
#ifdef WIN32
    ssize_t reth;
    if ((reth = send(cs->com.fd, header, sizeof(sen_com_sqtp_header), 0)) == -1) {
      LOG_SOCKERR("send size");
      cs->rc = sen_external_error;
      goto exit;
    }
    if ((ret = send(cs->com.fd, body, header->size, 0)) == -1) {
      LOG_SOCKERR("send body");
      cs->rc = sen_external_error;
      goto exit;
    }
    ret += reth;
#else /* WIN32 */
    struct iovec msg_iov[2];
    struct msghdr msg;
    msg.msg_name = NULL;
    msg.msg_namelen = 0;
    msg.msg_iov = msg_iov;
    msg.msg_iovlen = 2;
    msg.msg_control = NULL;
    msg.msg_controllen = 0;
    msg.msg_flags = 0;
    msg_iov[0].iov_base = header;
    msg_iov[0].iov_len = sizeof(sen_com_sqtp_header);
    msg_iov[1].iov_base = body;
    msg_iov[1].iov_len = header->size;
    while ((ret = sendmsg(cs->com.fd, &msg, MSG_NOSIGNAL)) == -1) {
      LOG_SOCKERR("sendmsg");
      if (errno == EAGAIN || errno == EINTR) { continue; }
      cs->rc = sen_external_error;
      goto exit;
    }
#endif /* WIN32 */
  } else {
    while ((ret = send(cs->com.fd, header, whole_size, MSG_NOSIGNAL)) == -1) {
#ifdef WIN32
      int e = WSAGetLastError();
      LOG_SOCKERR("send");
      if (e == WSAEWOULDBLOCK || e == WSAEINTR) { continue; }
#else /* WIN32 */
      LOG_SOCKERR("send");
      if (errno == EAGAIN || errno == EINTR) { continue; }
#endif /* WIN32 */
      cs->rc = sen_external_error;
      goto exit;
    }
  }
  if (ret != whole_size) {
    SEN_LOG(sen_log_error, "sendmsg: %d < %d", ret, whole_size);
    cs->rc = sen_external_error;
    goto exit;
  }
  cs->rc = sen_success;
exit :
  return cs->rc;
}

sen_rc
sen_com_sqtp_recv(sen_com_sqtp *cs, sen_rbuf *buf,
                  unsigned int *status, unsigned int *info)
{
  ssize_t ret;
  sen_com_sqtp_header *header;
  size_t rest = sizeof(sen_com_sqtp_header);
  if (SEN_RBUF_WSIZE(buf) < rest) {
    if ((cs->rc = sen_rbuf_reinit(buf, rest))) {
      *status = sen_com_emem; *info = 1; goto exit;
    }
  } else {
    SEN_RBUF_REWIND(buf);
  }
  do {
    // todo : also support non blocking mode (use MSG_DONTWAIT)
    if ((ret = recv(cs->com.fd, buf->curr, rest, MSG_WAITALL)) <= 0) {
      if (ret < 0) {
#ifdef WIN32
        int e = WSAGetLastError();
        LOG_SOCKERR("recv size");
        if (e == WSAEWOULDBLOCK || e == WSAEINTR) { continue; }
        *info = e;
#else /* WIN32 */
        LOG_SOCKERR("recv size");
        if (errno == EAGAIN || errno == EINTR) { continue; }
        *info = errno;
#endif /* WIN32 */
      }
      cs->rc = sen_external_error;
      *status = sen_com_erecv_head;
      goto exit;
    }
    rest -= ret, buf->curr += ret;
  } while (rest);
  header = SEN_COM_SQTP_MSG_HEADER(buf);
  SEN_LOG(sen_log_info, "recv (%d,%x,%d,%02x,%02x,%04x,%08x)", header->size, header->flags, header->proto, header->qtype, header->level, header->status, header->info);
  *status = header->status;
  *info = header->info;
  {
    uint16_t proto = header->proto;
    size_t value_size = header->size;
    size_t whole_size = sizeof(sen_com_sqtp_header) + value_size;
    if (proto != SEN_COM_PROTO_SQTP) {
      SEN_LOG(sen_log_error, "illegal header: %d", proto);
      cs->rc = sen_invalid_format;
      *status = sen_com_eproto;
      *info = proto;
      goto exit;
    }
    if (SEN_RBUF_WSIZE(buf) < whole_size) {
      if ((cs->rc = sen_rbuf_resize(buf, whole_size))) {
        *status = sen_com_emem; *info = 2;
        goto exit;
      }
    }
    for (rest = value_size; rest;) {
      if ((ret = recv(cs->com.fd, buf->curr, rest, MSG_WAITALL)) <= 0) {
        if (ret < 0) {
#ifdef WIN32
          int e = WSAGetLastError();
          LOG_SOCKERR("recv body");
          if (e == WSAEWOULDBLOCK || e == WSAEINTR) { continue; }
          *info = e;
#else /* WIN32 */
          LOG_SOCKERR("recv body");
          if (errno == EAGAIN || errno == EINTR) { continue; }
          *info = errno;
#endif /* WIN32 */
        }
        cs->rc = sen_external_error;
        *status = sen_com_erecv_body;
        goto exit;
      }
      rest -= ret, buf->curr += ret;
    }
    *buf->curr = '\0';
  }
  cs->rc = sen_success;
exit :
  return cs->rc;
}

sen_com_sqtp *
sen_com_sqtp_copen(sen_com_event *ev, const char *dest, int port)
{
  sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */
  sen_sock fd;
  sen_com_sqtp *cs = NULL;
  struct hostent *he;
  struct sockaddr_in addr;
  if (!(he = gethostbyname(dest))) {
    LOG_SOCKERR("gethostbyname");
    goto exit;
  }
  addr.sin_family = AF_INET;
  memcpy(&addr.sin_addr, he->h_addr, he->h_length);
  addr.sin_port = htons(port);
  if ((fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
    LOG_SOCKERR("socket");
    goto exit;
  }
  {
    int v = 1;
    if (setsockopt(fd, 6, TCP_NODELAY, (void *) &v, sizeof(int)) == -1) {
      LOG_SOCKERR("setsockopt");
    }
  }
  while (connect(fd, (struct sockaddr *)&addr, sizeof addr) == -1) {
#ifdef WIN32
    if (WSAGetLastError() == WSAECONNREFUSED)
#else /* WIN32 */
    if (errno == ECONNREFUSED)
#endif /* WIN32 */
    {
      SEN_LOG(sen_log_notice, "connect retrying..");
      sleep(2);
      continue;
    }
    LOG_SOCKERR("connect");
    goto exit;
  }
  if (ev) {
    if (sen_com_event_add(ev, fd, SEN_COM_POLLIN, (sen_com **)&cs)) { goto exit; }
  } else {
    if (!(cs = SEN_CALLOC(sizeof(sen_com_sqtp)))) { goto exit; }
    cs->com.fd = fd;
  }
  sen_rbuf_init(&cs->msg, 0);
exit :
  if (!cs) { sen_sock_close(fd); }
  return cs;
}

static void
sen_com_sqtp_receiver(sen_com_event *ev, sen_com *c)
{
  unsigned int status, info;
  sen_com_sqtp *cs = (sen_com_sqtp *)c;
  if (cs->com.status == sen_com_closing) {
    sen_com_sqtp_close(ev, cs);
    return;
  }
  if (cs->com.status != sen_com_idle) {
    SEN_LOG(sen_log_info, "waiting to be idle.. (%d) %d", c->fd, ev->set->n_entries);
    usleep(1000);
    return;
  }
  sen_com_sqtp_recv(cs, &cs->msg, &status, &info);
  cs->com.status = sen_com_doing;
  cs->msg_in(ev, c);
}

static void
sen_com_sqtp_acceptor(sen_com_event *ev, sen_com *c)
{
  sen_com_sqtp *cs = (sen_com_sqtp *)c, *ncs;
  sen_sock fd = accept(cs->com.fd, NULL, NULL);
  if (fd == -1) {
    LOG_SOCKERR("accept");
    return;
  }
  if (sen_com_event_add(ev, fd, SEN_COM_POLLIN, (sen_com **)&ncs)) {
    sen_sock_close(fd);
    return;
  }
  ncs->com.ev_in = sen_com_sqtp_receiver;
  sen_rbuf_init(&ncs->msg, 0);
  ncs->msg_in = cs->msg_in;
}

#define LISTEN_BACKLOG 0x1000

sen_com_sqtp *
sen_com_sqtp_sopen(sen_com_event *ev, int port, sen_com_callback *func)
{
  sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */
  sen_sock lfd;
  sen_com_sqtp *cs = NULL;
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(struct sockaddr_in));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port);
  if ((lfd = socket(PF_INET, SOCK_STREAM, 0)) == -1) {
    LOG_SOCKERR("socket");
    return NULL;
  }
  {
    int v = 1;
    if (setsockopt(lfd, 6, TCP_NODELAY, (void *) &v, sizeof(int)) == -1) {
      LOG_SOCKERR("setsockopt");
      goto exit;
    }
  }
  {
    int retry = 0;
    for (;;) {
      if (bind(lfd, (struct sockaddr *) &addr, sizeof addr) < 0) {
#ifdef WIN32
        if (WSAGetLastError() == WSAEADDRINUSE)
#else /* WIN32 */
        if (errno == EADDRINUSE)
#endif /* WIN32 */
        {
          SEN_LOG(sen_log_notice, "bind retrying..(%d)", port);
          if (++retry < 10) { sleep(2); continue; }
        }
        LOG_SOCKERR("bind");
        goto exit;
      }
      break;
    }
  }
  if (listen(lfd, LISTEN_BACKLOG) < 0) {
    LOG_SOCKERR("listen");
    goto exit;
  }
  if (ev) {
    if (sen_com_event_add(ev, lfd, SEN_COM_POLLIN, (sen_com **)&cs)) { goto exit; }
  } else {
    if (!(cs = SEN_MALLOC(sizeof(sen_com_sqtp)))) { goto exit; }
    cs->com.fd = lfd;
  }
exit :
  if (cs) {
    cs->com.ev_in = sen_com_sqtp_acceptor;
    cs->msg_in = func;
  } else {
    sen_sock_close(lfd);
  }
  return cs;
}

sen_rc
sen_com_sqtp_close(sen_com_event *ev, sen_com_sqtp *cs)
{
  sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */
  sen_sock fd = cs->com.fd;
  sen_rbuf_fin(&cs->msg);
  if (ev) {
    sen_com_event_del(ev, fd);
  } else {
    SEN_FREE(cs);
  }
  if (shutdown(fd, SHUT_RDWR) == -1) { /* LOG_SOCKERR("shutdown"); */ }
  if (sen_sock_close(fd) == -1) {
    LOG_SOCKERR("close");
    return sen_external_error;
  }
  return sen_success;
}
