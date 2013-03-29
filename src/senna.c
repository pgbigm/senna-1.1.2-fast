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

#include "lib/com.h"
#include "lib/ql.h"
#include <string.h>
#include <stdio.h>
#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif /* HAVE_SYS_WAIT_H */

#define DEFAULT_PORT 10041
#define DEFAULT_DEST "localhost"

static int port = DEFAULT_PORT;
static int batchmode;
static sen_encoding enc = sen_enc_default;

static void
usage(void)
{
  fprintf(stderr,
          "Usage: senna [options...] [dest]\n"
          "options:\n"
          "  -a:                 run in standalone mode (default)\n"
          "  -c:                 run in client mode\n"
          "  -s:                 run in server mode\n"
          "  -e:                 encoding for new database [none|euc|utf8|sjis|latin1|koi8r]\n"
          "  -p <port number>:   server port number (default: %d)\n"
          "dest: <db pathname> or <dest hostname>\n"
          "  <db pathname>: when standalone/server mode\n"
          "  <dest hostname>: when client mode (default: \"%s\")\n",
          DEFAULT_PORT, DEFAULT_DEST);
}

inline static void
prompt(void)
{
  if (!batchmode) { fputs("> ", stderr); }
}

#define BUFSIZE 0x1000000

static int
do_alone(char *path)
{
  int rc = -1;
  sen_db *db = NULL;
  if (!path || (db = sen_db_open(path)) || (db = sen_db_create(path, 0, enc))) {
    sen_ctx *ctx;
    if ((ctx = sen_ctx_open(db, SEN_CTX_USEQL|(batchmode ? SEN_CTX_BATCHMODE : 0)))) {
      char *buf = SEN_MALLOC(BUFSIZE);
      if (buf) {
        sen_ctx_recv_handler_set(ctx, sen_ctx_stream_out_func, stdout);
        sen_ctx_load(ctx, NULL);
        while (prompt(), fgets(buf, BUFSIZE, stdin)) {
          uint32_t size = strlen(buf) - 1;
          buf[size] = '\0';
          sen_ctx_send(ctx, buf, size, 0);
          if (ctx->stat == SEN_CTX_QUIT) { break; }
        }
        SEN_FREE(buf);
        rc = 0;
      } else {
        fprintf(stderr, "sen_malloc failed (%d)\n", BUFSIZE);
      }
      sen_ctx_close(ctx);
    } else {
      fprintf(stderr, "db_ctx open failed (%s)\n",  path);
    }
    sen_db_close(db);
  } else {
    fprintf(stderr, "db open failed (%s)\n", path);
  }
  return rc;
}

#define BATCHMODE(ctx) do {\
  int flags;\
  unsigned int str_len;\
  char *str, *query = "(batchmode #t)";\
  sen_ctx_send(ctx, query, strlen(query), 0);\
  do {\
    if (sen_ctx_recv(ctx, &str, &str_len, &flags)) {\
      fprintf(stderr, "sen_ctx_recv failed\n");\
    }\
  } while ((flags & SEN_CTX_MORE));\
} while (0)

static int
do_client(char *hostname)
{
  int rc = -1;
  sen_ctx *ctx;
  if ((ctx = sen_ctx_connect(hostname, port, 0))) {
    sen_ctx_info info;
    sen_ctx_info_get(ctx, &info);
    if (!sen_rbuf_reinit(info.outbuf, BUFSIZE)) {
      if (batchmode) { BATCHMODE(ctx); }
      while (prompt(), fgets(info.outbuf->head, SEN_RBUF_WSIZE(info.outbuf), stdin)) {
        int flags;
        char *str;
        unsigned int str_len;
        uint32_t size = strlen(info.outbuf->head) - 1;
        if (sen_ctx_send(ctx, info.outbuf->head, size, 0)) { break; }
        do {
          if (sen_ctx_recv(ctx, &str, &str_len, &flags)) {
            fprintf(stderr, "sen_ctx_recv failed\n");
            goto exit;
          }
          if (str_len) {
            fwrite(str, 1, str_len, stdout);
            putchar('\n');
            fflush(stdout);
          }
        } while ((flags & SEN_CTX_MORE));
        if (ctx->stat == SEN_CTX_QUIT) { break; }
      }
      rc = 0;
    } else {
      fprintf(stderr, "sen_rbuf_reinit failed (%d)\n", BUFSIZE);
    }
    sen_ctx_close(ctx);
  } else {
    fprintf(stderr, "sen_ctx_connect failed (%s:%d)\n", hostname, port);
  }
exit :
  return rc;
}

/* server */

static void
output(sen_ctx *ctx, int flags, void *arg)
{
  static uint32_t info = 0;
  sen_com_sqtp *cs = arg;
  sen_com_sqtp_header header;
  sen_rbuf *buf = &ctx->outbuf;
  header.size = SEN_RBUF_VSIZE(buf);
  header.flags = (flags & SEN_CTX_MORE) ? SEN_CTX_MORE : SEN_CTX_TAIL;
  header.qtype = 1;
  header.level = 0;
  header.status = 0;
  header.info = info++; /* for debug */
  if (ctx->stat == SEN_CTX_QUIT) { cs->com.status = sen_com_closing; }
  sen_com_sqtp_send(cs, &header, buf->head);
  SEN_RBUF_REWIND(buf);
}

static void
errout(sen_com_sqtp *cs, char *msg)
{
  sen_com_sqtp_header header;
  header.size = strlen(msg);
  header.flags = SEN_CTX_TAIL;
  sen_com_sqtp_send(cs, &header, msg);
  SEN_LOG(sen_log_error, "errout: %s", msg);
}

inline static void
do_msg(sen_com_event *ev, sen_com_sqtp *cs)
{
  sen_ctx *ctx = (sen_ctx *)cs->userdata;
  if (!ctx) {
    ctx = sen_ctx_open((sen_db *)ev->userdata, SEN_CTX_USEQL);
    if (!ctx) {
      cs->com.status = sen_com_closing;
      errout(cs, "*** ERROR: ctx open failed");
      return;
    }
    sen_ctx_recv_handler_set(ctx, output, cs);
    sen_ctx_load(ctx, NULL);
    cs->userdata = ctx;
  }
  {
    char *body = SEN_COM_SQTP_MSG_BODY(&cs->msg);
    sen_com_sqtp_header *header = SEN_COM_SQTP_MSG_HEADER(&cs->msg);
    uint32_t size = header->size;
    uint16_t flags = header->flags;
    sen_ctx_send(ctx, body, size, flags);
    if (ctx->stat == SEN_CTX_QUIT || sen_gctx.stat == SEN_CTX_QUIT) {
      cs->com.status = sen_com_closing;
      sen_ctx_close(ctx);
      cs->userdata = NULL;
    } else {
      cs->com.status = sen_com_idle;
    }
    return;
  }
}

/* query queue */

static sen_mutex q_mutex;
static sen_cond q_cond;

typedef struct {
  uint8_t head;
  uint8_t tail;
  sen_com_sqtp *v[0x100];
} queue;

static void
queue_init(queue *q)
{
  q->head = 0;
  q->tail = 0;
}

static sen_rc
queue_enque(queue *q, sen_com_sqtp *c)
{
  uint8_t i = q->tail + 1;
  if (i == q->head) { return sen_other_error; }
  q->v[q->tail] = c;
  q->tail = i;
  return sen_success;
}

static queue qq;

/* worker thread */

#define MAX_NFTHREADS 0x04

static uint32_t nthreads = 0, nfthreads = 0;

static void * CALLBACK
thread_start(void *arg)
{
  sen_com_sqtp *cs;
  SEN_LOG(sen_log_notice, "thread start (%d/%d)", nfthreads, nthreads + 1);
  MUTEX_LOCK(q_mutex);
  nthreads++;
  do {
    nfthreads++;
    while (qq.tail == qq.head) { COND_WAIT(q_cond, q_mutex); }
    nfthreads--;
    if (sen_gctx.stat == SEN_CTX_QUIT) { break; }
    cs = qq.v[qq.head++];
    MUTEX_UNLOCK(q_mutex);
    if (cs) { do_msg((sen_com_event *) arg, cs); }
    MUTEX_LOCK(q_mutex);
  } while (nfthreads < MAX_NFTHREADS);
  nthreads--;
  MUTEX_UNLOCK(q_mutex);
  SEN_LOG(sen_log_notice, "thread end (%d/%d)", nfthreads, nthreads);
  return NULL;
}

static void
msg_handler(sen_com_event *ev, sen_com *c)
{
  sen_com_sqtp *cs = (sen_com_sqtp *)c;
  if (cs->rc) {
    sen_ctx *ctx = (sen_ctx *)cs->userdata;
    SEN_LOG(sen_log_notice, "connection closed..");
    if (ctx) { sen_ctx_close(ctx); }
    sen_com_sqtp_close(ev, cs);
    return;
  }
  {
    int i = 0;
    while (queue_enque(&qq, (sen_com_sqtp *)c)) {
      if (i) {
        SEN_LOG(sen_log_notice, "queue is full try=%d qq(%d-%d) thd(%d/%d) %d", i, qq.head, qq.tail, nfthreads, nthreads, ev->set->n_entries);
      }
      if (++i == 100) {
        errout((sen_com_sqtp *)c, "*** ERROR: query queue is full");
        return;
      }
      usleep(1000);
    }
  }
  MUTEX_LOCK(q_mutex);
  if (!nfthreads && nthreads < MAX_NFTHREADS) {
    sen_thread thread;
    MUTEX_UNLOCK(q_mutex);
    if (THREAD_CREATE(thread, thread_start, ev)) { GSERR("pthread_create"); }
    return;
  }
  COND_SIGNAL(q_cond);
  MUTEX_UNLOCK(q_mutex);
}

#define MAX_CON 0x10000

static int
server(char *path)
{
  int rc = -1;
  sen_com_event ev;
  MUTEX_INIT(q_mutex);
  COND_INIT(q_cond);
  queue_init(&qq);
  if (!sen_com_event_init(&ev, MAX_CON, sizeof(sen_com_sqtp))) {
    sen_db *db = NULL;
    if (!path || (db = sen_db_open(path)) || (db = sen_db_create(path, 0, enc))) {
      sen_com_sqtp *cs;
      ev.userdata = db;
      if ((cs = sen_com_sqtp_sopen(&ev, port, msg_handler))) {
        while (!sen_com_event_poll(&ev, 3000) && sen_gctx.stat != SEN_CTX_QUIT) ;
        for (;;) {
          MUTEX_LOCK(q_mutex);
          if (nthreads == nfthreads) { break; }
          MUTEX_UNLOCK(q_mutex);
          usleep(1000);
        }
        {
          sen_sock *pfd;
          sen_com_sqtp *com;
          SEN_SET_EACH(ev.set, eh, &pfd, &com, {
            sen_ctx *ctx = (sen_ctx *)com->userdata;
            if (ctx) { sen_ctx_close(ctx); }
            sen_com_sqtp_close(&ev, com);
          });
        }
        rc = 0;
      } else {
        fprintf(stderr, "sen_com_sqtp_sopen failed (%d)\n", port);
      }
      sen_db_close(db);
    } else {
      fprintf(stderr, "db open failed (%s)\n", path);
    }
    sen_com_event_fin(&ev);
  } else {
    fprintf(stderr, "sen_com_event_init failed\n");
  }
  return rc;
}

static int
do_server(char *path)
{
#ifndef WIN32
  pid_t pid;
  switch (fork()) {
  case 0:
    break;
  case -1:
    perror("fork");
    return -1;
  default:
    wait(NULL);
    return 0;
  }
  switch ((pid = fork())) {
  case 0:
    break;
  case -1:
    perror("fork");
    return -1;
  default:
    fprintf(stderr, "%d\n", pid);
    _exit(0);
  }
#endif /* WIN32 */
  return server(path);
}

enum {
  mode_alone,
  mode_client,
  mode_server,
  mode_dserver,
  mode_usage
};

#define SET_LOGLEVEL(x) do {\
  static sen_logger_info info;\
  info.max_level = (x);\
  info.flags = SEN_LOG_TIME|SEN_LOG_MESSAGE;\
  info.func = NULL;\
  info.func_arg = NULL;\
  sen_logger_info_set(&info);\
} while(0)

int
main(int argc, char **argv)
{
  char *portstr = NULL, *encstr = NULL, *loglevel = NULL;
  int r, i, mode = mode_alone;
  static sen_str_getopt_opt opts[] = {
    {'p', NULL, NULL, 0, getopt_op_none},
    {'e', NULL, NULL, 0, getopt_op_none},
    {'h', NULL, NULL, mode_usage, getopt_op_update},
    {'a', NULL, NULL, mode_alone, getopt_op_update},
    {'c', NULL, NULL, mode_client, getopt_op_update},
    {'s', NULL, NULL, mode_server, getopt_op_update},
    {'d', NULL, NULL, mode_dserver, getopt_op_update},
    {'l', NULL, NULL, 0, getopt_op_none},
    {'\0', NULL, NULL, 0, 0}
  };
  opts[0].arg = &portstr;
  opts[1].arg = &encstr;
  opts[7].arg = &loglevel;
  i = sen_str_getopt(argc, argv, opts, &mode);
  if (i < 0) { mode = mode_usage; }
  if (portstr) { port = atoi(portstr); }
  if (encstr) {
    switch (*encstr) {
    case 'n' :
    case 'N' :
      enc = sen_enc_none;
      break;
    case 'e' :
    case 'E' :
      enc = sen_enc_euc_jp;
      break;
    case 'u' :
    case 'U' :
      enc = sen_enc_utf8;
      break;
    case 's' :
    case 'S' :
      enc = sen_enc_sjis;
      break;
    case 'l' :
    case 'L' :
      enc = sen_enc_latin1;
      break;
    case 'k' :
    case 'K' :
      enc = sen_enc_koi8r;
      break;
    }
  }
  batchmode = !isatty(0);
  if (sen_init()) { return -1; }
  sen_gctx.encoding = enc; /* todo : make it api */
  if (loglevel) { SET_LOGLEVEL(atoi(loglevel)); }
  switch (mode) {
  case mode_alone :
    r = do_alone(argc <= i ? NULL : argv[i]);
    break;
  case mode_client :
    r = do_client(argc <= i ? DEFAULT_DEST : argv[i]);
    break;
  case mode_server :
    r = do_server(argc <= i ? NULL : argv[i]);
    break;
  case mode_dserver :
    r = server(argc <= i ? NULL : argv[i]);
    break;
  default :
    usage(); r = -1;
    break;
  }
  sen_fin();
  return r;
}
