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

#include "lib/inv.h"
#include "lib/sym.h"
#include "lib/store.h"

#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <setjmp.h>
#ifdef HAVE_UCONTEXT_H
#include <ucontext.h>
#endif /* HAVE_UCONTEXT_H */

#ifdef WIN32
#include <errno.h>
#define sigsetjmp(x,y) setjmp((x))
#define siglongjmp(x,y) longjmp((x), (y))
typedef jmp_buf sigjmp_buf;
#endif /* WIN32 */

/* TODO: export sen_index_clear_lock */
sen_rc sen_index_clear_lock(sen_index *i);

#define INITIAL_CHECK_FILES 128
#define MAX_NSEGV 32
#define MAX_NERR_FILE 8

#define CHK_DUMP_LEXICON (1 << 0)
#define CHK_THROUGH_SEGV (1 << 1)
#define CHK_UNLOCK       (1 << 2)

typedef enum {
  type_unknown = 0,
  type_sym,
  type_inv,
  type_ja,
  type_ra
} senchk_filetype;

typedef enum {
  rc_success
} senchk_rc;

typedef struct {
  senchk_filetype type;
  char *path;
  char *lexpath; /* for use inv check */
  char *keyspath; /* for use inv check */
  volatile unsigned int cerr; /* error count */
} senchk_file;

typedef struct {
  senchk_file *files;
  unsigned int n_files;
  unsigned int max_n_files;
} senchk_filelist;

/* globals */
volatile unsigned int segvcount = 0;
senchk_file *chkfile = NULL;
int chkflags = 0;
sigjmp_buf sigret;

static void
#ifdef WIN32
trapsegv(int sno)
#else /* WIN32 */
trapsegv(unsigned int sno, siginfo_t si, ucontext_t *sc)
#endif /* WIN32 */
{
  if (++segvcount >= MAX_NSEGV) {
    exit(1);
  }
#ifndef WIN32
  switch(si.si_code) {
#ifdef SEGV_MAPERR
    case SEGV_MAPERR:
      printf("* SEGV:%p IS NOT MAPPED!\n", si.si_addr);
      break;
#endif /* SEGV_MAPERR */
#ifdef SEGV_ACCERR
    case SEGV_ACCERR:
      printf("* SEGV:CANNOT ACCESS %p\n", si.si_addr);
      break;
#endif /* SEGV_ACCERR */
    default:
      printf("* SEGMENTATION FAULT! si_code:%d address:%p\n",
             si.si_code, si.si_addr);
  }
#endif /* WIN32 */
  if (chkfile) {
    printf("* ERROR occured on checking file '%s'. Maybe broken.\n", chkfile->path);
    if (chkfile->keyspath && chkfile->lexpath) {
      printf("* file '%s' depends keys file '%s' and lexicon file '%s'\n",
             chkfile->path, chkfile->keyspath, chkfile->lexpath);
    }
    chkfile->cerr++;
    siglongjmp(sigret, 1);
  }
}

static void
show_delimiter(FILE *fp)
{
  fprintf(fp, "------------------------------------------------------\n");
}

static void
show_sym_info(sen_sym *sym, FILE *fp)
{
  /* TODO: show flags */
  /* TODO: need sen_enc2str */
  int keysz;
  sen_encoding enc;
  unsigned int flags, nrec, fsz;
  if (sen_sym_info(sym, &keysz, &flags, &enc, &nrec, &fsz)) {
    fprintf(stderr, "sen_sym_info failed...\n");
  } else {
    fprintf(fp,
            "  Sym file infomation:\n"
            "   - key_size                : %10d\n"
            "   - flags                   : %10u\n"
            "   - encoding                : %10s\n"
            "   - nrecords                : %10u\n"
            "   - file_size               : %10u\n",
            keysz, flags, sen_enctostr(enc), nrec, fsz);
  }
}

int
check_sym(senchk_file *f)
{
  sen_sym *sym = NULL;

  chkfile = f;
  show_delimiter(stdout);
  printf(" check sym file: '%s'\n", f->path);

  if (chkflags & CHK_THROUGH_SEGV || !sigsetjmp(sigret, 1)) {
    sen_id id = SEN_SYM_NIL, pid, max_id, c = 0;

    if (!(sym = sen_sym_open(f->path))) {
      printf("cannot open sym file '%s'\n", f->path);
      return -1;
    }
    show_sym_info(sym, stdout);

    max_id = sen_sym_curr_id(sym);
    while ((pid = sen_sym_next(sym, id)) != SEN_SYM_NIL && f->cerr < MAX_NERR_FILE) {
      char key[SEN_SYM_MAX_KEY_SIZE];
      sen_id at_id;
      id = pid;
      if (!(c % 1000)) {
        fprintf(stderr, "entries: %d\r", c);
      }
      if (!sen_sym_key(sym, id, key, SEN_SYM_MAX_KEY_SIZE)) {
        printf("sen_sym_key failed! id: %d\n", id);
        f->cerr++;
        continue;
      } else if ((at_id = sen_sym_at(sym, key)) == SEN_SYM_NIL) {
        printf("sen_sym_at failed! key: %s\n", key);
        f->cerr++;
      } else if (id != at_id) {
        printf("sen_sym_key/sen_sym_at results are not consistent:"
               "key: %s key_id:%d at_id:%d", key, id, at_id);
        f->cerr++;
      }
      c++;
      /* TODO: sis check with suffix_search */
    }
    sen_sym_close(sym);
    printf(" checked sym file %s: total %d entries\n", f->path, c);

    return f->cerr;
  } else {
    /* handle segv */
    if (sym) { sen_sym_close(sym); }
    return -2;
  }
}

static void
show_index_info(sen_index *i, FILE *fp)
{
  int keysz, flags, inseg;
  sen_encoding enc;
  unsigned int nkey, szkey, nlex, szlex;
  unsigned long long szseg, szchunk;
  if (sen_index_info(i, &keysz, &flags, &inseg, &enc,
                     &nkey, &szkey, &nlex, &szlex, &szseg, &szchunk)) {
    fprintf(stderr, "sen_index_info failed...\n");
    return;
  }
  /* TODO: get lock infomation through API */
  /* TODO: need sen_enc2str */
  fprintf(fp,
          "  Index file infomation:\n"
          "   - lock                    : %12u\n"
          "   - key_size                : %12d\n"
          "   - flags                   : %12d\n"
          "   - initial_n_segments      : %12d\n"
          "   - encoding                : %12s\n"
          "   - nrecords_keys           : %12u\n"
          "   - file_size_keys          : %12u\n"
          "   - nrecords_lexicon        : %12u\n"
          "   - file_size_lexicon       : %12u\n"
          "   - inv_segment_size        : %12llu\n"
          "   - inv_chunk_size          : %12llu\n",
          *i->keys->lock, keysz, flags, inseg,
          sen_enctostr(enc), nkey, szkey,
          nlex, szlex, szseg, szchunk);
}

int
check_inv(senchk_file *f)
{
  int e;
  sen_index *i = NULL;
  sen_sym *keys = NULL, *lex = NULL;

  chkfile = f;
  show_delimiter(stdout);
  printf(" check inv file: '%s'\n", f->path);

  if (chkflags & CHK_THROUGH_SEGV || !sigsetjmp(sigret, 1)) {
    sen_id tid = SEN_SYM_NIL, c = 0;
    sen_inv *inv;
    sen_inv_cursor *cur;
    unsigned int tdf = 0, tsf = 0, tnposts = 0;

    if (!(keys = sen_sym_open(f->keyspath))) {
      printf("cannot open keys sym file '%s' "
             "for checking inv file '%s'\n", f->keyspath, f->path);
      e = -1; goto exit;
    }
    if (!(lex = sen_sym_open(f->lexpath))) {
      printf("cannot open lex sym file '%s' "
             "for checking inv file '%s'\n", f->lexpath, f->path);
      e = -2; goto exit;
    }
    if (!(i = sen_index_open_with_keys_lexicon(f->path, keys, lex))) {
      printf("cannot open inv file '%s'\n", f->path);
      e = -3; goto exit;
    }
    show_index_info(i, stdout);

    if (chkflags & CHK_UNLOCK) {
      printf("  unlock inv file: '%s'\n", f->path);
      sen_index_clear_lock(i);
    } else {
      inv = i->inv;
      if (chkflags & CHK_DUMP_LEXICON) {
        puts("  Inv records infomation:\n"
             "         tid,        df,        sf,    nposts | term");
      }
      while ((tid = sen_sym_next(lex, tid)) != SEN_SYM_NIL && f->cerr < MAX_NERR_FILE) {
        if (!(c % 1000)) {
          fprintf(stderr, "entries: %d\r", c);
        }
        if (!(cur = sen_inv_cursor_open(inv, tid, 1))) {
          /* TODO: detect array_at fails or not
          printf("cannot open inv cursor '%s'\n", f->path);
          f->cerr++;
          */
        } else {
          sen_inv_posting pre = {0, 0, 0, 0, 0, 0};
          unsigned int df = 0, sf = 0, nposts = 0;
          while (!sen_inv_cursor_next(cur) && f->cerr < MAX_NERR_FILE) {
            sen_inv_posting *post = cur->post;
            sen_id max_rid = sen_sym_curr_id(i->keys);
            if (pre.rid > post->rid || post->rid > max_rid) {
              printf("invalid rid: pre-rid:%d rid:%d max_rid:%d\n",
                     pre.rid, post->rid, max_rid);
              f->cerr++;
            } else if (pre.rid == post->rid) {
              /* TODO: get max_sid and compare it */
              if (pre.sid >= post->sid) {
                printf("invalid sid: rid:%d pre-sid:%d sid:%d\n",
                       post->rid, pre.sid, post->sid);
                f->cerr++;
              } else {
                sf++;
              }
            } else {
              char key[SEN_SYM_MAX_KEY_SIZE];
              if (!sen_sym_key(keys, post->rid, key, SEN_SYM_MAX_KEY_SIZE)) {
                printf("looking up key from keys failed.\n"
                       " rid: %d\n", post->rid);
                f->cerr++;
              }
              df++;
              sf++;
            }
            pre.pos = 0;
            while (!sen_inv_cursor_next_pos(cur) && f->cerr < MAX_NERR_FILE) {
              post = cur->post;
              if (pre.pos && pre.pos >= post->pos) {
                printf("invalid position: rid:%d sid:%d pre-pos:%d pos:%d\n",
                       post->rid, post->sid, pre.pos, post->pos);
                f->cerr++;
              } else if (!post->tf) {
                printf("invalid tf: rid:%d sid:%d pos:%d tf:0\n",
                       post->rid, post->sid, post->pos);
                f->cerr++;
              }
              nposts++;
              pre = *post;
            }
            pre = *post;
          }
          if (chkflags & CHK_DUMP_LEXICON) {
            char term[SEN_SYM_MAX_KEY_SIZE];
            if (!sen_sym_key(lex, tid, term, SEN_SYM_MAX_KEY_SIZE)) {
              printf("looking up term from lexicon failed.\n"
                     " termid: %d\n", tid);
              f->cerr++;
            } else {
              /* FIXME: check lexicon size and print non-null terminated fix-size term */
              printf("  %10d,%10d,%10d,%10d | %s\n", tid, df, sf, nposts, term);
            }
          }
          tdf += df;
          tsf += sf;
          tnposts += nposts;
          sen_inv_cursor_close(cur);
        }
        c++;
      }
      printf(" checked inv file %s: total %d entiries\n"
             " checked: %d records/%d sections/%d posts\n", f->path, c, tdf, tsf, tnposts);
      e = f->cerr;
    }
  } else {
    /* handle segv */
    e = -4;
  }
exit:
  if (i) { sen_index_close(i); }
  if (keys) { sen_sym_close(keys); }
  if (lex) { sen_sym_close(lex); }
  return e;
}

/* copy from store.c */
inline static void
gen_pathname(const char *path, char *buffer, int fno)
{
  size_t len = strlen(path);
  memcpy(buffer, path, len);
  if (fno >= 0) {
    buffer[len] = '.';
    sen_str_itoh(fno, buffer + len + 1, 7);
  } else {
    buffer[len] = '\0';
  }
}

char *
cat_pathname(const char *path, const char *ext)
{
  char *buf;
  if (!path || !ext) { return NULL; }
  {
    size_t path_len = strlen(path);
    size_t ext_len = strlen(ext);
    if (path_len + ext_len >= PATH_MAX) { return NULL; }
    if ((buf = SEN_GMALLOC(path_len + ext_len + 1))) {
      memcpy(buf, path, path_len);
      memcpy(buf + path_len, ext, ext_len + 1);
    }
    return buf;
  }
}

int
check_file_existence(const char *path, const char *ext)
{
  FILE *fp;
  const char *buf;
  int ret = 0;
  if (!path) { return 0; }
  if (ext) {
    if (!(buf = cat_pathname(path, ext))) { return 0; }
  } else {
    buf = path;
  }
  if (!(fp = fopen(buf, "r"))) {
    switch(errno) {
      case ENOENT:
        goto exit;
      case EACCES:
        printf("cannot access %s.\n", buf);
        goto exit;
      default:
        printf("error: [%s][%d] %s\n", buf, errno, strerror(errno));
        goto exit;
    }
  }
  fclose(fp);
  ret = 1;
exit:
  if (ext) { SEN_GFREE((char *)buf); }
  return ret;
}

senchk_filelist *
senchk_filelist_init(unsigned int max_n_files)
{
  senchk_filelist *fl;
  if ((fl = SEN_GMALLOC(sizeof(senchk_filelist)))) {
    if ((fl->files = SEN_GMALLOC(max_n_files * sizeof(senchk_file)))) {
      fl->n_files = 0;
      fl->max_n_files = max_n_files;
      return fl;
    }
    SEN_GFREE(fl);
  }
  return NULL;
}

inline static void
senchk_filelist_expand(senchk_filelist *fl)
{
  senchk_file *files;
  if (fl->n_files < fl->max_n_files) { return; }
  fl->max_n_files <<= 1;
  if (!(files = SEN_GREALLOC(fl->files,
                             fl->max_n_files * sizeof(senchk_file)))) {
    puts("memory realloction failed: on senchk_filelist_expand");
    fl->max_n_files >>= 1;
    fl->n_files = 0; /* overwrite */
    return;
  }
  fl->files = files;
}

int
senchk_filelist_add(senchk_filelist *fl, senchk_filetype type,
                    const char *path, const char *keyspath, const char *lexpath)
{
  if (fl && path) {
    senchk_file *f = fl->files + fl->n_files;
    f->type = type;
    if (type == type_inv && keyspath && lexpath) {
      f->keyspath = strdup(keyspath);
      f->lexpath = strdup(lexpath);
    } else {
      f->keyspath = f->lexpath = NULL;
    }
    f->path = strdup(path);
    fl->n_files++;
    senchk_filelist_expand(fl);
    return 1;
  }
  return 0;
}

int
senchk_filelist_addi(senchk_filelist *fl, senchk_filetype type, void *instance)
{
  if (fl && instance) {
    senchk_file *f = fl->files + fl->n_files;
    f->type = type;
    f->keyspath = f->lexpath = NULL;
    switch(type) {
      case type_sym:
        if (((sen_sym *)instance)->v08p) {
          puts("files made by senna version 0.8 is not supported.");
          return 0;
        }
        f->path = strdup(((sen_sym *)instance)->io->path);
        break;
      case type_inv:
        {
          /* TODO: move sen_inv to inv.h */
          /* f->path = strdup(((sen_index *)instance)->seg->path) */
          f->keyspath = strdup(((sen_index *)instance)->keys->io->path);
          f->lexpath = strdup(((sen_index *)instance)->lexicon->io->path);

          /* FIXME: temporal implementation */
          f->path = strdup(f->lexpath);
          f->path[strlen(f->path) - 1] = 'i';
        }
        break;
      case type_ra:
        /* TODO: implement */
        break;
      case type_ja:
        /* TODO: implement */
        break;
      default:
        /* TODO: error */
        break;
    }
    fl->n_files++;
    senchk_filelist_expand(fl);
    return 1;
  }
  return 0;
}

void
senchk_filelist_fin(senchk_filelist *fl)
{
  senchk_file *f, *end;
  if (!fl) { return; }
  end = fl->files + fl->n_files;
  for (f = fl->files; f < end; f++) {
    if (f->path) { SEN_GFREE(f->path); }
    if (f->keyspath) { SEN_GFREE(f->keyspath); }
    if (f->lexpath) { SEN_GFREE(f->lexpath); }
  }
  SEN_GFREE(fl);
}

int
senchk_filelist_get(senchk_filelist *fl, const char *path)
{
  /* returns file count added */
  int ret = 0;
  if (check_file_existence(path, NULL)) {
    sen_db *db;
    if ((db = sen_db_open(path)) && !segvcount) {
      sen_id cid = 0;
      if (senchk_filelist_add(fl, type_sym, path, NULL, NULL)) {
        ret += 1;
      }
      while ((cid = sen_sym_next(db->keys, cid)) != SEN_SYM_NIL) {
        uint32_t spec_len;
        char buffer[PATH_MAX];
        sen_db_store_spec *spec;
        if (!(spec = sen_ja_ref(db->values, cid, &spec_len))) {
          /* TODO: implement */
          return 0;
        }
        gen_pathname(db->keys->io->path, buffer, cid);
        switch(spec->type) {
        case sen_db_raw_class :
          /* not physical file */
          break;
        case sen_db_class :
          /* sym */
          /* TODO: implement */
          if (senchk_filelist_add(fl, type_sym, buffer, NULL, NULL)) {
            ret += 1;
          }
          break;
        case sen_db_obj_slot :
          /* TODO: implement obj check */
          /* ra */
          if (senchk_filelist_add(fl, type_ra, buffer, NULL, NULL)) {
            ret += 1;
          }
          break;
        case sen_db_ra_slot :
          /* ra */
          if (senchk_filelist_add(fl, type_ra, buffer, NULL, NULL)) {
            ret += 1;
          }
          break;
        case sen_db_ja_slot :
          /* ja */
          if (senchk_filelist_add(fl, type_ja, buffer, NULL, NULL)) {
            ret += 1;
          }
          break;
        case sen_db_idx_slot :
          /* inv keys lexicon */
          {
            sen_db_store *l, *k;
            if (!(k = sen_db_store_by_id(db, spec->u.s.class))) {
              /* TODO: implement */
            }
            if (!(l = sen_db_slot_class_by_id(db, cid))) {
              /* TODO: implement */
            }
            if (senchk_filelist_add(fl, type_inv, buffer,
                                    k->u.c.keys->io->path,
                                    l->u.c.keys->io->path)) {
              ret += 1;
            }
          }
          break;
        default:
          break;
        }
        sen_ja_unref(db->values, cid, spec, spec_len);
      }
      sen_db_close(db);
    }
  } else if (check_file_existence(path, ".SEN") &&
             check_file_existence(path, ".SEN.i") &&
             check_file_existence(path, ".SEN.l")) {
    sen_index *index;
    if ((index = sen_index_open(path)) && !segvcount) {
      /* TODO: check vgram if exists */
      if (senchk_filelist_addi(fl, type_sym, index->keys) &&
          senchk_filelist_addi(fl, type_sym, index->lexicon) &&
          senchk_filelist_addi(fl, type_inv, index)) {
        ret = 3;
      }
      sen_index_close(index);
    }
  }
  return ret;
}

int
senchk_filelist_check(senchk_filelist *fl)
{
  int ngcount = 0;
  senchk_file *f, *end;
  if (!fl) { return -1; }
  end = fl->files + fl->n_files;

  if (!(chkflags & CHK_UNLOCK)) {
    /* sym/ra/ja check */
    for (f = fl->files; f < end; f++) {
      switch (f->type) {
        case type_sym:
          if (check_sym(f)) {
            ngcount++;
            printf("* BROKEN SYM FILE: '%s'\n", f->path);
          }
          break;
        case type_ra:
          /* TODO: implement */
          show_delimiter(stdout);
          printf(" check ra file: '%s'\n", f->path);
          puts("* now checking ra is not implemented...");
          break;
        case type_ja:
          /* TODO: implement */
          show_delimiter(stdout);
          printf(" check ja file: '%s'\n", f->path);
          puts("* now checking ja is not implemented...");
          break;
        case type_inv:
          /* do nothing */
          break;
        default:
          /* TODO: print error */
          break;
      }
    }
  }

  /* inv check */
  for (f = fl->files; f < end; f++) {
    if (f->type == type_inv) {
      if (check_inv(f)) {
        ngcount++;
        printf("* BROKEN INV FILE: '%s' KEYS: '%s' LEXICON: '%s'\n",
               f->path, f->keyspath, f->lexpath);
      }
    }
  }

  return ngcount;
}

const char *
handleopt(int argc, char **argv)
{
  int i;
  static sen_str_getopt_opt opts[] = {
    {'l', "lexdump", NULL, CHK_DUMP_LEXICON, getopt_op_on},
    {'t', "throughsegv", NULL, CHK_THROUGH_SEGV, getopt_op_on},
    {'u', "unlock", NULL, CHK_UNLOCK, getopt_op_on},
    {'\0', NULL, NULL, 0, 0}
  };

  i = sen_str_getopt(argc, argv, opts, &chkflags);
  if (i == argc - 1) {
    return argv[i];
  } else {
    return NULL;
  }
}

static void
show_seninfo(FILE *fp)
{
  /* TODO: sen_info revise. confpath will be deprecated. */
  /* TODO: need sen_enc2str */
  char *version, *confopt, *confpath;
  sen_encoding denc;
  unsigned int nseg, pmt;
  sen_info(&version, &confopt, &confpath,
           &denc, &nseg, &pmt);
  fprintf(fp,
          "  Senna infomation:\n"
          "   - Version                 : %s\n"
          "   - Configure Options       : %s\n"
          "   - Default Encoding        : %s\n"
          "   - Initial N-Segments      : %d\n"
          "   - Partial Match Threshold : %d\n",
          version, confopt, sen_enctostr(denc), nseg, pmt);
}

static void
usage(void)
{
  fprintf(stderr,
          "Usage: sennachk [options...] senna-file\n"
          "options:\n"
          "  --lexdump, -l     : print lex words on checking inv file\n"
          "  --throughsegv, -t : run without trap SIGSEGV\n"
          "  --unlock, -u      : unlock index lock\n"
          "senna-file: <index path> or <db path>\n");
}

int
main(int argc, char *argv[])
{
  int ret, files, ngfiles;
  const char *path;
  senchk_filelist *fls;
#ifndef WIN32
  struct sigaction m;
#endif /* WIN32 */

  if (!(path = handleopt(argc, argv))) {
    usage();
    return 1;
  }

  if (!(chkflags & CHK_THROUGH_SEGV)) {
#ifdef WIN32
    if (signal(SIGSEGV, trapsegv) == SIG_ERR) {
#else /* WIN32 */
    m.sa_flags = SA_SIGINFO;
    m.sa_sigaction = (void *)trapsegv;
    if (sigaction(SIGSEGV, &m, NULL) < 0) {
#endif /* WIN32 */
      printf("cannot handle segv");
      return 2;
    }
  }

  sen_init();
  show_delimiter(stdout);
  show_seninfo(stdout);

  if (!(fls = senchk_filelist_init(INITIAL_CHECK_FILES))) {
    puts("cannot init filelist object");
    ret = 3;
    goto exit;
  }

  if (!(files = senchk_filelist_get(fls, path)) || segvcount) {
    puts("cannot find senna index.");
    ret = 4;
    goto exit;
  }

  ngfiles = senchk_filelist_check(fls);

  show_delimiter(stdout);
  printf(" %d file(s) checked, and %d file(s) are invalid\n", files, ngfiles);
exit:
  if (fls) {
    senchk_filelist_fin(fls);
  }
  sen_fin();

  return 0;
}
