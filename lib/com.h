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

#ifndef SEN_COM_H
#define SEN_COM_H

#ifndef SENNA_H
#include "senna_in.h"
#endif /* SENNA_H */

#ifndef SEN_STR_H
#include "str.h"
#endif /* SEN_STR_H */

#ifdef	__cplusplus
extern "C" {
#endif

/******* sen_com ********/

#ifdef USE_SELECT
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif /* HAVE_SYS_SELECT_H */
#define SEN_COM_POLLIN  1
#define SEN_COM_POLLOUT 2
#else /* USE_SELECT */
#ifdef USE_EPOLL
#include <sys/epoll.h>
#define SEN_COM_POLLIN  EPOLLIN
#define SEN_COM_POLLOUT EPOLLOUT
#else /* USE_EPOLL */
#include <sys/poll.h>
#define SEN_COM_POLLIN  POLLIN
#define SEN_COM_POLLOUT POLLOUT
#endif /* USE_EPOLL */
#endif /* USE_SELECT */

typedef struct _sen_com sen_com;
typedef struct _sen_com_event sen_com_event;
typedef void sen_com_callback(sen_com_event *, sen_com *);

enum {
  sen_com_idle = 0,
  sen_com_head,
  sen_com_body,
  sen_com_doing,
  sen_com_done,
  sen_com_connecting,
  sen_com_error,
  sen_com_closing
};

enum {
  sen_com_ok = 0,
  sen_com_emem,
  sen_com_erecv_head,
  sen_com_erecv_body,
  sen_com_eproto,
};

struct _sen_com {
  sen_sock fd;
  uint8_t status;
  int events;
  sen_com_callback *ev_in;
  sen_com_callback *ev_out;
};

struct _sen_com_event {
  sen_set *set;
  int max_nevents;
  void *userdata;
#ifndef USE_SELECT
#ifdef USE_EPOLL
  int epfd;
  struct epoll_event *events;
#else /* USE_EPOLL */
  int dummy; /* dummy */
  struct pollfd *events;
#endif /* USE_EPOLL */
#endif /* USE_SELECT */
};

sen_rc sen_com_init(void);
void sen_com_fin(void);
sen_rc sen_com_event_init(sen_com_event *ev, int max_nevents, int data_size);
sen_rc sen_com_event_fin(sen_com_event *ev);
sen_rc sen_com_event_add(sen_com_event *ev, sen_sock fd, int events, sen_com **com);
sen_rc sen_com_event_mod(sen_com_event *ev, sen_sock fd, int events, sen_com **com);
sen_rc sen_com_event_del(sen_com_event *ev, sen_sock fd);
sen_rc sen_com_event_poll(sen_com_event *ev, int timeout);
sen_rc sen_com_event_each(sen_com_event *ev, sen_com_callback *func);

/******* sen_com_sqtp ********/

#define SEN_COM_PROTO_SQTP 20819

typedef struct _sen_com_sqtp sen_com_sqtp;
typedef struct _sen_com_sqtp_header sen_com_sqtp_header;

struct _sen_com_sqtp_header {
  uint32_t size;
  uint16_t flags;
  uint16_t proto;
  uint8_t qtype;
  uint8_t level;
  uint16_t status;
  uint32_t info;
};

struct _sen_com_sqtp {
  sen_com com;
  sen_rc rc;
  size_t rest;
  sen_rbuf msg;
  sen_com_callback *msg_in;
  void *userdata;
};

sen_com_sqtp *sen_com_sqtp_copen(sen_com_event *ev, const char *dest, int port);
sen_com_sqtp *sen_com_sqtp_sopen(sen_com_event *ev, int port, sen_com_callback *func);
sen_rc sen_com_sqtp_close(sen_com_event *ev, sen_com_sqtp *cs);

sen_rc sen_com_sqtp_send(sen_com_sqtp *cs, sen_com_sqtp_header *header, char *body);
sen_rc sen_com_sqtp_recv(sen_com_sqtp *cs, sen_rbuf *buf,
                         unsigned int *status, unsigned int *info);

#define SEN_COM_SQTP_MSG_HEADER(buf) ((sen_com_sqtp_header *)(buf)->head)
#define SEN_COM_SQTP_MSG_BODY(buf) ((buf)->head + sizeof(sen_com_sqtp_header))

#ifdef __cplusplus
}
#endif

#endif /* SEN_COM_H */
