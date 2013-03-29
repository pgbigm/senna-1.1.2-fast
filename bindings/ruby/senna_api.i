%module senna
%{
#define Init_senna Init_senna_api
#include "senna.h"

static sen_sym * sen_index_keys(sen_index *i) { return i->keys; }
static sen_sym * sen_index_lexicon(sen_index *i) { return i->lexicon; }
static sen_inv * sen_index_inv(sen_index *i) { return i->inv; }
static sen_set_eh * sen_set_eh_nth(sen_set_eh *eh, int i) { return eh + i; }

struct LoggerInfo {
  int max_level;
  int flags;
  VALUE func;
  struct _sen_logger_info info;
};

struct SelectOptarg {
  sen_sel_mode mode;
  int similarity_threshold;
  int max_interval;
  VALUE weight_vector;
  VALUE func;
  VALUE func_arg;
  struct _sen_select_optarg optarg;
};

struct GroupOptarg {
  sen_sort_mode mode;
  VALUE func;
  VALUE func_arg;
  int key_size;
  struct _sen_group_optarg optarg;
};

struct SortOptarg {
  sen_sort_mode mode;
  VALUE compar;
  VALUE compar_arg;
  struct _sen_sort_optarg optarg;
};

struct SetSortOptarg {
  sen_sort_mode mode;
  VALUE compar;
  VALUE compar_arg;
  struct _sen_set_sort_optarg optarg;
};

static void
logger_handler(int level, const char *time, const char *title,
                    const char *msg, const char *location, void *func_arg)
{
  struct LoggerInfo *info = (struct LoggerInfo *)func_arg;
  rb_funcall(info->func, rb_intern("call"), 5,
    INT2FIX(level),
    rb_str_new2(time),
    rb_str_new2(title),
    rb_str_new2(msg),
    rb_str_new2(location));
}

static int
select_handler(sen_records *r, const void *key, int sid, void *func_arg)
{
  VALUE result;
  struct SelectOptarg *optarg = (struct SelectOptarg *)func_arg;
  result = rb_funcall(optarg->func, rb_intern("call"), 4,
    SWIG_NewPointerObj(SWIG_as_voidptr(r), SWIGTYPE_p_sen_records, 0),
    rb_str_new2(key),
    INT2FIX(sid),
    optarg->func_arg);
  return FIX2INT(result);
}

static int
group_handler(sen_records *r, const sen_recordh *rh, void *gkey, void *func_arg)
{
  VALUE result;
  struct GroupOptarg *optarg = (struct GroupOptarg *)func_arg;
  result = rb_funcall(optarg->func, rb_intern("call"), 3,
    SWIG_NewPointerObj(SWIG_as_voidptr(r), SWIGTYPE_p_sen_records, 0),
    SWIG_NewPointerObj(SWIG_as_voidptr(rh), SWIGTYPE_p_sen_recordh, 0),
    optarg->func_arg);
  if (result == Qnil || TYPE(result) != T_STRING) {
    return 1;
  } else {
    memcpy(gkey, rb_string_value_ptr(&result), optarg->key_size);
    return 0;
  }
}

static int
sort_handler(sen_records *ra, const sen_recordh *a, sen_records *rb, const sen_recordh *b, void *compar_arg)
{
  VALUE result;
  struct SortOptarg *optarg = (struct SortOptarg *)compar_arg;
  result = rb_funcall(optarg->compar, rb_intern("call"), 5,
    SWIG_NewPointerObj(SWIG_as_voidptr(ra), SWIGTYPE_p_sen_records, 0),
    SWIG_NewPointerObj(SWIG_as_voidptr(a), SWIGTYPE_p_sen_recordh, 0),
    SWIG_NewPointerObj(SWIG_as_voidptr(rb), SWIGTYPE_p_sen_records, 0),
    SWIG_NewPointerObj(SWIG_as_voidptr(b), SWIGTYPE_p_sen_recordh, 0),
    optarg->compar_arg);
  return FIX2INT(result);
}

static int
set_sort_handler(sen_set *sa, sen_set_eh *a, sen_set *sb, sen_set_eh *b, void *compar_arg)
{
  VALUE result;
  struct SetSortOptarg *optarg = (struct SetSortOptarg *)compar_arg;
  result = rb_funcall(optarg->compar, rb_intern("call"), 5,
    SWIG_NewPointerObj(SWIG_as_voidptr(sa), SWIGTYPE_p_sen_set, 0),
    SWIG_NewPointerObj(SWIG_as_voidptr(a), SWIGTYPE_p_sen_set_eh, 0),
    SWIG_NewPointerObj(SWIG_as_voidptr(sb), SWIGTYPE_p_sen_set, 0),
    SWIG_NewPointerObj(SWIG_as_voidptr(b), SWIGTYPE_p_sen_set_eh, 0),
    optarg->compar_arg);
  return FIX2INT(result);
}

static void
mark_logger_info(void *ptr) {
  struct LoggerInfo *info = (struct LoggerInfo *)ptr;
  rb_gc_mark(info->func);
}

static void
mark_select_optarg(void *ptr) {
  struct SelectOptarg *optarg = (struct SelectOptarg *)ptr;
  rb_gc_mark(optarg->weight_vector);
  rb_gc_mark(optarg->func);
  rb_gc_mark(optarg->func_arg);
}

static void
mark_group_optarg(void *ptr) {
  struct GroupOptarg *optarg = (struct GroupOptarg *)ptr;
  rb_gc_mark(optarg->func);
  rb_gc_mark(optarg->func_arg);
}

static void
mark_sort_optarg(void *ptr) {
  struct SortOptarg *optarg = (struct SortOptarg *)ptr;
  rb_gc_mark(optarg->compar);
  rb_gc_mark(optarg->compar_arg);
}

static void
mark_set_sort_optarg(void *ptr) {
  struct SetSortOptarg *optarg = (struct SetSortOptarg *)ptr;
  rb_gc_mark(optarg->compar);
  rb_gc_mark(optarg->compar_arg);
}

void atexit_sen_fin(void) {
// sen_fin();
}

%}

%markfunc LoggerInfo "mark_logger_info";
%markfunc SelectOptarg "mark_select_optarg";
%markfunc GroupOptarg "mark_group_optarg";
%markfunc SortOptarg "mark_sort_optarg";
%markfunc SetSortOptarg "mark_set_sort_optarg";

%init %{
sen_init();
atexit(atexit_sen_fin);
%}

%include "exception.i"
%include "typemaps.i"

#ifdef SWIGRUBY

%typemap(in) const void * {
  if (TYPE($input) == T_STRING) {
    $1 = (void *)StringValuePtr($input);
  } else {
    SWIG_exception_fail(SWIG_TypeError, "Expected string");
  }
}

%typemap(in) (void *keybuf, int bufsize) {
  int size = NUM2INT($input);
  $1 = (size) ? calloc(size, 1) : NULL;
  $2 = size;
}
%typemap(argout) (void *keybuf, int bufsize) {
  VALUE ary;
  if (TYPE($result) == T_ARRAY) {
    ary = $result;
  } else {
    ary = rb_ary_new2(2);
    rb_ary_push(ary, $result);
  }
  if ($1) {
    rb_ary_push(ary, rb_str_new2($1));
    free($1);
  } else {
    rb_ary_push(ary, Qnil);
  }
  $result = ary;
}

%typemap(in) (char *pathbuf, int bufsize) {
  int size = NUM2INT($input);
  $1 = (size) ? calloc(size, 1) : NULL;
  $2 = size;
}
%typemap(argout) (char *pathbuf, int bufsize) {
  VALUE ary;
  if (TYPE($result) == T_ARRAY) {
    ary = $result;
  } else {
    ary = rb_ary_new2(2);
    rb_ary_push(ary, $result);
  }
  if ($1) {
    rb_ary_push(ary, rb_str_new2($1));
    free($1);
  } else {
    rb_ary_push(ary, Qnil);
  }
  $result = ary;
}

%typemap(in,numinputs=0) (char **str, unsigned int *str_len) {
  $1 = calloc(1, sizeof(char *));
  $2 = calloc(1, sizeof(int));
}
%typemap(argout) (char **str, unsigned int *str_len) {
  VALUE ary;
  if (TYPE($result) == T_ARRAY) {
    ary = $result;
  } else {
    ary = rb_ary_new2(2);
    rb_ary_push(ary, $result);
  }
  rb_ary_push(ary, rb_str_new(*($1), *($2)));
  free($1);
  free($2);
  $result = ary;
}

%typemap(in,numinputs=0) (int *) {
  $1 = (int *)calloc(1, sizeof(int));
}
%typemap(argout) (int *) {
  VALUE ary;
  if (TYPE($result) == T_ARRAY) {
    ary = $result;
  } else {
    ary = rb_ary_new2(2);
    rb_ary_push(ary, $result);
  }
  rb_ary_push(ary, INT2NUM(*$1));
  free($1);
  $result = ary;
}

%typemap(in,numinputs=0) (unsigned int *) {
  $1 = (unsigned int *)calloc(1, sizeof(unsigned int));
}
%typemap(argout) (unsigned int *) {
  VALUE ary;
  if (TYPE($result) == T_ARRAY) {
    ary = $result;
  } else {
    ary = rb_ary_new2(2);
    rb_ary_push(ary, $result);
  }
  rb_ary_push(ary, UINT2NUM(*$1));
  free($1);
  $result = ary;
}

%typemap(in,numinputs=0) (size_t *) {
  $1 = (size_t *)calloc(1, sizeof(size_t));
}
%typemap(argout) (size_t *) {
  VALUE ary;
  if (TYPE($result) == T_ARRAY) {
    ary = $result;
  } else {
    ary = rb_ary_new2(2);
    rb_ary_push(ary, $result);
  }
  rb_ary_push(ary, UINT2NUM(*$1));
  free($1);
  $result = ary;
}

%typemap(in) (void **) {
  int size = NUM2INT($input);
  $1 = (size > -1) ? calloc(1, sizeof(void *)) : NULL;
}
%typemap(argout) (void **) {
  VALUE ary;
  VALUE res;
  if ($1) {
    int size = NUM2INT($input);
    res = size ? rb_str_new(*($1), size) : rb_str_new2(*($1));
    free($1);
  } else {
    res = Qnil;
  }
  if (TYPE($result) == T_ARRAY) {
    ary = $result;
  } else {
    ary = rb_ary_new2(2);
    rb_ary_push(ary, $result);
  }
  rb_ary_push(ary, res);
  $result = ary;
}

%typemap(in) (const char ** const) {
  int size = NUM2INT($input);
  $1 = size ? calloc(1, sizeof(char *)) : NULL;
}
%typemap(argout) (const char ** const) {
  VALUE ary;
  VALUE res;
  if ($1) {
    res = rb_str_new(*($1), NUM2INT($result));
    free($1);
  } else {
    res = Qnil;
  }
  ary = rb_ary_new2(2);
  rb_ary_push(ary, $result);
  rb_ary_push(ary, res);
  $result = ary;
}

%typemap(in) (char *result) {
  int size = NUM2INT($input);
  $1 = (size) ? calloc(size, 1) : NULL;
}
%typemap(argout) (char *result) {
  VALUE ary;
  if (TYPE($result) == T_ARRAY) {
    ary = $result;
  } else {
    ary = rb_ary_new2(2);
    rb_ary_push(ary, $result);
  }
  if ($1) {
    rb_ary_push(ary, rb_str_new2($1));
    free($1);
  } else {
    rb_ary_push(ary, Qnil);
  }
  $result = ary;
}

%typemap(in) (const sen_logger_info *) {
  struct LoggerInfo *w;
  int res = SWIG_ConvertPtr($input, (void *)&w, SWIGTYPE_p_LoggerInfo, 0);
  if (!SWIG_IsOK(res)) {
    SWIG_exception_fail(SWIG_ArgError(res), "LoggerInfo expected");
  }
  if (w) {
    $1 = &w->info;
    w->info.max_level = w->max_level;
    w->info.flags = w->flags;
    w->info.func = CLASS_OF(w->func) == rb_cProc ? logger_handler : NULL;
    w->info.func_arg = w;
  } else {
    $1 = NULL;
  }
}

%typemap(in) (sen_select_optarg *) {
  struct SelectOptarg *w;
  int res = SWIG_ConvertPtr($input, (void *)&w, SWIGTYPE_p_SelectOptarg, 0);
  if (!SWIG_IsOK(res)) {
    SWIG_exception_fail(SWIG_ArgError(res), "SelectOptarg expected");
  }
  if (w) {
    $1 = &w->optarg;
    w->optarg.mode = w->mode;
    w->optarg.similarity_threshold = w->similarity_threshold;
    w->optarg.max_interval = w->max_interval;
    if (TYPE(w->weight_vector) == T_ARRAY) {
      int i, size = FIX2INT(rb_funcall(w->weight_vector, rb_intern("size"), 0));
      w->optarg.vector_size = size;
      w->optarg.weight_vector = malloc(sizeof(int) * size);
      for (i = 0; i < size; i++) {
        w->optarg.weight_vector[i] =
          FIX2INT(rb_funcall(w->weight_vector, rb_intern("[]"), 1, INT2NUM(i)));
      }
    } else {
      w->optarg.vector_size = 0;
      w->optarg.weight_vector = NULL;
    }
    w->optarg.func = CLASS_OF(w->func) == rb_cProc ? select_handler : NULL;
    w->optarg.func_arg = w;
  } else {
    $1 = NULL;
  }
}
%typemap(argout) (sen_select_optarg *) {
  if ($1 && ($1)->weight_vector) { free(($1)->weight_vector); }
}

%typemap(in) (sen_group_optarg *) {
  struct GroupOptarg *w;
  int res = SWIG_ConvertPtr($input, (void *)&w, SWIGTYPE_p_GroupOptarg, 0);
  if (!SWIG_IsOK(res)) {
    SWIG_exception_fail(SWIG_ArgError(res), "GroupOptarg expected");
  }
  if (w) {
    $1 = &w->optarg;
    w->optarg.mode = w->mode;
    w->optarg.func = CLASS_OF(w->func) == rb_cProc ? group_handler : NULL;
    w->optarg.func_arg = w;
    w->optarg.key_size = w->key_size;
  } else {
    $1 = NULL;
  }
}

%typemap(in) (sen_sort_optarg *) {
  struct SortOptarg *w;
  int res = SWIG_ConvertPtr($input, (void *)&w, SWIGTYPE_p_SortOptarg, 0);
  if (!SWIG_IsOK(res)) {
    SWIG_exception_fail(SWIG_ArgError(res), "SortOptarg expected");
  }
  if (w) {
    $1 = &w->optarg;
    w->optarg.mode = w->mode;
    if (CLASS_OF(w->compar) == rb_cProc) {
      w->optarg.compar = sort_handler;
      w->optarg.compar_arg = w;
    } else {
      w->optarg.compar = NULL;
      w->optarg.compar_arg = FIXNUM_P(w->compar_arg) ? (void *)(FIX2INT(w->compar_arg)) : NULL;
    }
  } else {
    $1 = NULL;
  }
}

%typemap(in) (sen_set_sort_optarg *) {
  struct SetSortOptarg *w;
  int res = SWIG_ConvertPtr($input, (void *)&w, SWIGTYPE_p_SetSortOptarg, 0);
  if (!SWIG_IsOK(res)) {
    SWIG_exception_fail(SWIG_ArgError(res), "SetSortOptarg expected");
  }
  if (w) {
    $1 = &w->optarg;
    w->optarg.mode = w->mode;
    if (CLASS_OF(w->compar) == rb_cProc) {
      w->optarg.compar = set_sort_handler;
      w->optarg.compar_arg = w;
    } else {
      w->optarg.compar = NULL;
      w->optarg.compar_arg = FIXNUM_P(w->compar_arg) ? (void *)(FIX2INT(w->compar_arg)) : NULL;
    }
  } else {
    $1 = NULL;
  }
}

%typemap(in) (sen_snip_mapping *) {
  $1 = FIXNUM_P($input) ? (sen_snip_mapping *)(FIX2INT($input)) : NULL;
}

#endif

typedef unsigned sen_id;

enum sen_rc;
enum sen_encoding;
enum sen_rec_unit;
enum sen_sel_operator;
enum sen_sel_mode;
enum sen_sort_mode;
enum sen_log_level;

%apply int *OUTPUT {  int *key_size, int *flags,
                      int *initial_n_segments, sen_encoding *encoding,
                      unsigned *nrecords_keys, unsigned *file_size_keys,
                      unsigned *nrecords_lexicon, unsigned *file_size_lexicon,
                      unsigned long long *inv_seg_size,
                      unsigned long long *inv_chunk_size };

struct LoggerInfo {
  int max_level;
  int flags;
  VALUE func;
  struct _sen_logger_info info;
};

struct SelectOptarg {
  sen_sel_mode mode;
  int similarity_threshold;
  int max_interval;
  VALUE weight_vector;
  VALUE func;
  VALUE func_arg;
  struct _sen_select_optarg optarg;
};

struct GroupOptarg {
  sen_sort_mode mode;
  VALUE func;
  VALUE func_arg;
  int key_size;
  struct _sen_group_optarg optarg;
};

struct SortOptarg {
  sen_sort_mode mode;
  VALUE compar;
  VALUE compar_arg;
  struct _sen_sort_optarg optarg;
};

struct SetSortOptarg {
  sen_sort_mode mode;
  VALUE compar;
  VALUE compar_arg;
  struct _sen_set_sort_optarg optarg;
};

/******** accessors ********/

static sen_sym * sen_index_keys(sen_index *i);
static sen_sym * sen_index_lexicon(sen_index *i);
static sen_inv * sen_index_inv(sen_index *i);
static sen_set_eh * sen_set_eh_nth(sen_set_eh *eh, int i);

/******** basic API ********/

sen_rc sen_init(void);
sen_rc sen_fin(void);

sen_index *sen_index_create(const char *path, int key_size, int flags,
                            int initial_n_segments, sen_encoding encoding);
sen_index *sen_index_open(const char *path);
sen_rc sen_index_close(sen_index *i);
sen_rc sen_index_remove(const char *path);
sen_rc sen_index_rename(const char *old_name, const char *new_name);
sen_rc sen_index_upd(sen_index *i, const void *key,
                     const char *oldvalue, unsigned int oldvalue_len,
                     const char *newvalue, unsigned int newvalue_len);
sen_records *sen_index_sel(sen_index *i, const char *string, unsigned int string_len);
int sen_records_next(sen_records *r, void *keybuf, int bufsize, int *score);
sen_rc sen_records_rewind(sen_records *r);
int sen_records_curr_score(sen_records *r);
int sen_records_curr_key(sen_records *r, void *keybuf, int bufsize);
int sen_records_nhits(sen_records *r);
int sen_records_find(sen_records *r, const void *key);
sen_rc sen_records_close(sen_records *r);

/******** advanced API ********/

sen_values *sen_values_open(void);
sen_rc sen_values_close(sen_values *v);
sen_rc sen_values_add(sen_values *v, const char *str, unsigned int str_len,
                      unsigned int weight);

sen_records *sen_records_open(sen_rec_unit record_unit,
                              sen_rec_unit subrec_unit,
                              unsigned int max_n_subrecs);
sen_records *sen_records_union(sen_records *a, sen_records *b);
sen_records *sen_records_subtract(sen_records *a, sen_records *b);
sen_records *sen_records_intersect(sen_records *a, sen_records *b);
int sen_records_difference(sen_records *a, sen_records *b);
sen_rc sen_records_sort(sen_records *r, int limit, sen_sort_optarg *optarg);
sen_rc sen_records_group(sen_records *r, int limit, sen_group_optarg *optarg);
const sen_recordh * sen_records_curr_rec(sen_records *r);
const sen_recordh * sen_records_at(sen_records *r, const void *key,
                                   unsigned section, unsigned pos,
                                   int *score, int *n_subrecs);
sen_rc sen_record_info(sen_records *r, const sen_recordh *rh,
                       void *keybuf, int bufsize, int *keysize,
                       int *section, int *pos, int *score, int *n_subrecs);
sen_rc sen_record_subrec_info(sen_records *r, const sen_recordh *rh,
                              int index, void *keybuf, int bufsize,
                              int *keysize, int *section, int *pos, int *score);
sen_index *sen_index_create_with_keys(const char *path, sen_sym *keys, int flags,
                                      int initial_n_segments, sen_encoding encoding);
sen_index *sen_index_open_with_keys(const char *path, sen_sym *keys);
sen_rc sen_index_update(sen_index *i, const void *key, unsigned int section,
                        sen_values *oldvalues, sen_values *newvalues);
sen_rc sen_index_select(sen_index *i, const char *string, unsigned int string_len,
                        sen_records *r, sen_sel_operator op, sen_select_optarg *optarg);
sen_rc sen_index_info(sen_index *i, int *key_size, int *flags,
                      int *initial_n_segments, sen_encoding *encoding,
                      unsigned *nrecords_keys, unsigned *file_size_keys,
                      unsigned *nrecords_lexicon, unsigned *file_size_lexicon,
                      unsigned long long *inv_seg_size,
                      unsigned long long *inv_chunk_size);
int sen_index_path(sen_index *i, char *pathbuf, int bufsize);

sen_query *sen_query_open(const char *str, unsigned int str_len,
                          sen_sel_operator default_op, int max_exprs, sen_encoding encoding);
unsigned int sen_query_rest(sen_query *q, const char ** const rest);
sen_rc sen_query_close(sen_query *q);
sen_rc sen_query_exec(sen_index *i, sen_query *q, sen_records *r, sen_sel_operator op);
void sen_query_term(sen_query *q, query_term_callback func, void *func_arg);
sen_rc sen_query_scan(sen_query *q, const char **strs, unsigned int *str_lens,
                      unsigned int nstrs, int flags, int *found, int *score);
sen_snip *sen_query_snip(sen_query *query, int flags,
                         unsigned int width, unsigned int max_results,
                         unsigned int n_tags,
                         const char **opentags, unsigned int *opentag_lens,
                         const char **closetags, unsigned int *closetag_lens,
                         sen_snip_mapping *mapping);

sen_rc sen_index_del(sen_index *i, const void *key);

/******** low level API ********/

sen_set *sen_set_open(unsigned key_size, unsigned value_size, unsigned init_size);
sen_rc sen_set_close(sen_set *set);
sen_rc sen_set_info(sen_set *set, unsigned *key_size,
                    unsigned *value_size, unsigned *n_entries);
sen_set_eh *sen_set_get(sen_set *set, const void *key, void **value);
sen_set_eh *sen_set_at(sen_set *set, const void *key, void **value);
sen_rc sen_set_del(sen_set *set, sen_set_eh *e);
sen_set_cursor *sen_set_cursor_open(sen_set *set);
sen_set_eh *sen_set_cursor_next(sen_set_cursor *cursor, void **key, void **value);
sen_rc sen_set_cursor_close(sen_set_cursor *cursor);
sen_rc sen_set_element_info(sen_set *set, const sen_set_eh *e,
                            void **key, void **value);
sen_set *sen_set_union(sen_set *a, sen_set *b);
sen_set *sen_set_subtract(sen_set *a, sen_set *b);
sen_set *sen_set_intersect(sen_set *a, sen_set *b);
int sen_set_difference(sen_set *a, sen_set *b);
sen_set_eh *sen_set_sort(sen_set *set, int limit, sen_set_sort_optarg *optarg);

sen_sym * sen_sym_create(const char *path, unsigned key_size,
                         unsigned flags, sen_encoding encoding);
sen_sym * sen_sym_open(const char *path);
sen_rc sen_sym_info(sen_sym *sym, int *key_size, unsigned *flags,
                    sen_encoding *encoding, unsigned *nrecords, unsigned *file_size);
sen_rc sen_sym_close(sen_sym *sym);
sen_rc sen_sym_remove(const char *path);

/* Lookup the sym table and find the id of the corresponding entry.
 * If no matches are found, create a new entry, and return that ID
 */
sen_id sen_sym_get(sen_sym *sym, const void *key);

/* Lookup the sym table and find the id of the corresponding entry.
 * If no matches are found return SEN_SYM_NIL
 */
sen_id sen_sym_at(sen_sym *sym, const void *key);
sen_rc sen_sym_del(sen_sym *sym, const void *key);
unsigned int sen_sym_size(sen_sym *sym);
int sen_sym_key(sen_sym *sym, sen_id id, void *keybuf, int bufsize);
sen_set * sen_sym_prefix_search(sen_sym *sym, const void *key);
sen_set * sen_sym_suffix_search(sen_sym *sym, const void *key);
sen_id sen_sym_common_prefix_search(sen_sym *sym, const void *key);
int sen_sym_pocket_get(sen_sym *sym, sen_id id);
sen_rc sen_sym_pocket_set(sen_sym *sym, sen_id id, unsigned int value);
sen_id sen_sym_next(sen_sym *sym, sen_id id);

/******** utility API ********/
sen_snip *sen_snip_open(sen_encoding encoding, int flags, unsigned int width,
                        unsigned int max_results,
                        const char *defaultopentag, unsigned int defaultopentag_len,
                        const char *defaultclosetag, unsigned int defaultclosetag_len,
                        sen_snip_mapping *mapping);
sen_rc sen_snip_close(sen_snip *snip);
sen_rc sen_snip_add_cond(sen_snip *snip, const char *keyword, unsigned int keyword_len,
                         const char *opentag, unsigned int opentag_len,
                         const char *closetag, unsigned int closetag_len);
sen_rc sen_snip_exec(sen_snip *snip, const char *string, unsigned int string_len,
                     unsigned int *nresults, unsigned int *max_tagged_len);
sen_rc sen_snip_get_result(sen_snip *snip, const unsigned int index,
                           char *result, unsigned int *result_len);

sen_records_heap *sen_records_heap_open(int size, int limit, sen_sort_optarg *optarg);
sen_rc sen_records_heap_add(sen_records_heap *h, sen_records *r);
int sen_records_heap_next(sen_records_heap *h);
sen_records *sen_records_heap_head(sen_records_heap *h);
sen_rc sen_records_heap_close(sen_records_heap *h);

int sen_inv_entry_info(sen_inv *inv, sen_id key, unsigned *a, unsigned *pocket,
                       unsigned *chunk, unsigned *chunk_size, unsigned *buffer_free,
                       unsigned *nterms, unsigned *nterms_void, unsigned *tid,
                       unsigned *size_in_chunk, unsigned *pos_in_chunk,
                       unsigned *size_in_buffer, unsigned *pos_in_buffer);

int sen_str_normalize(const char *str, unsigned int str_len,
                      sen_encoding encoding, int flags,
                      char *pathbuf, int bufsize);
unsigned int sen_str_charlen(const char *str, sen_encoding encoding);

/* misc */

sen_rc sen_logger_info_set(const sen_logger_info *info);

void sen_logger_put(sen_log_level level,
                    const char *file, int line, const char *func, char *fmt, const char *msg);

/* sen_db */

sen_db *sen_db_create(const char *path, int flags, sen_encoding encoding);
sen_db *sen_db_open(const char *path);
sen_rc sen_db_close(sen_db *s);

/* sen_ctx */

sen_ctx *sen_ctx_open(sen_db *db, int flags);
sen_ctx *sen_ctx_connect(const char *host, int port, int flags);
sen_rc sen_ctx_load(sen_ctx *c, const char *path);
sen_rc sen_ctx_send(sen_ctx *c, char *str, unsigned int str_len, int flags);
sen_rc sen_ctx_recv(sen_ctx *c, char **str, unsigned int *str_len, int *flags);
sen_rc sen_ctx_close(sen_ctx *c);
