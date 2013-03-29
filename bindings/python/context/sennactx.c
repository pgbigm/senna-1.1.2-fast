/* Copyright(C) 2007 Brazil

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
#include <Python.h>
#include <senna/senna.h>

/* TODO: use exception */

typedef struct {
  PyObject_HEAD
  sen_db *db;
} sennactx_DbObject;

typedef struct {
  PyObject_HEAD
  sen_ctx *ctx;
} sennactx_ContextObject;

static PyTypeObject sennactx_DbType;
static PyTypeObject sennactx_ContextType;

/* Object methods */

static PyObject *
sennactx_DbObject_new(PyTypeObject *type, PyObject *args, PyObject *keywds)
{
  sennactx_DbObject *self;
  if ((self = (sennactx_DbObject *)type->tp_alloc(type, 0))) {
    self->db = NULL;
  }
  return (PyObject *)self;
}

static void
sennactx_DbObject_dealloc(sennactx_DbObject *self)
{
  if (self->db) {
    Py_BEGIN_ALLOW_THREADS
    sen_db_close(self->db);
    Py_END_ALLOW_THREADS
  }
  self->ob_type->tp_free(self);
}

static PyObject *
sennactx_ContextObject_new(PyTypeObject *type, PyObject *args, PyObject *keywds)
{
  sennactx_ContextObject *self;
  if ((self = (sennactx_ContextObject *)type->tp_alloc(type, 0))) {
    self->ctx = NULL;
  }
  return (PyObject *)self;
}

static void
sennactx_ContextObject_dealloc(sennactx_ContextObject *self)
{
  if (self->ctx) {
    Py_BEGIN_ALLOW_THREADS
    sen_ctx_close(self->ctx);
    Py_END_ALLOW_THREADS
  }
  self->ob_type->tp_free(self);
}

/* Class methods */

static PyObject *
sennactx_DbClass_create(PyTypeObject *type, PyObject *args, PyObject *keywds)
{
  int flags, enc;
  const char *path;
  sennactx_DbObject *self;
  static char *kwlist[] = {"path", "flags", "encodings", NULL};

  if (!PyArg_ParseTupleAndKeywords(args, keywds, "sii", kwlist,
                                   &path, &flags, &enc)) {
    return NULL;
  }
  if ((self = (sennactx_DbObject *)sennactx_DbObject_new(type, NULL, NULL))) {
    Py_BEGIN_ALLOW_THREADS
    self->db = sen_db_create(path, flags, enc);
    Py_END_ALLOW_THREADS
    if (self->db) {
      return (PyObject *)self;
    }
    sennactx_DbObject_dealloc(self);
  }
  return NULL;
}

static PyObject *
sennactx_DbClass_open(PyTypeObject *type, PyObject *args, PyObject *keywds)
{
  const char *path;
  sennactx_DbObject *self;
  static char *kwlist[] = {"path", NULL};

  if (!PyArg_ParseTupleAndKeywords(args, keywds, "s", kwlist,
                                   &path)) {
    return NULL;
  }
  if ((self = (sennactx_DbObject *)sennactx_DbObject_new(type, NULL, NULL))) {
    Py_BEGIN_ALLOW_THREADS
    self->db = sen_db_open(path);
    Py_END_ALLOW_THREADS
    if (self->db) {
      return (PyObject *)self;
    }
    sennactx_DbObject_dealloc(self);
  }
  return NULL;
}

static PyObject *
sennactx_ContextClass_connect(PyTypeObject *type, PyObject *args, PyObject *keywds)
{
  int port, flags;
  const char *host;
  sennactx_ContextObject *self;
  static char *kwlist[] = {"host", "port", "flags", NULL};

  if (!PyArg_ParseTupleAndKeywords(args, keywds, "sii", kwlist,
                                   &host, &port, &flags)) {
    return NULL;
  }
  if ((self = (sennactx_ContextObject *)sennactx_ContextObject_new(type, NULL, NULL))) {
    Py_BEGIN_ALLOW_THREADS
    self->ctx = sen_ctx_connect(host, port, flags);
    Py_END_ALLOW_THREADS
    if (self->ctx) {
      return (PyObject *)self;
    }
    sennactx_ContextObject_dealloc(self);
  }
  return NULL;
}

/* instance methods */

static PyObject *
sennactx_Db_close(sennactx_DbObject *self)
{
  sen_rc rc;

  if (!self->db) { return NULL; }
  Py_BEGIN_ALLOW_THREADS
  rc = sen_db_close(self->db);
  Py_END_ALLOW_THREADS
  if (!rc) {
    self->db = NULL;
  }
  return Py_BuildValue("i", rc);
}

static PyObject *
sennactx_Db_ctx_open(sennactx_DbObject *self, PyObject *args, PyObject *keywds)
{
  int flags;
  sennactx_ContextObject *ctx;
  static char *kwlist[] = {"flags", NULL};

  if (!PyArg_ParseTupleAndKeywords(args, keywds, "i", kwlist,
                                   &flags)) {
    return NULL;
  }
  if ((ctx = (sennactx_ContextObject *)sennactx_ContextObject_new(
               &sennactx_ContextType, NULL, NULL))) {
    Py_BEGIN_ALLOW_THREADS
    ctx->ctx = sen_ctx_open(self->db, flags);
    Py_END_ALLOW_THREADS
    if (ctx->ctx) {
      return (PyObject *)ctx;
    }
    sennactx_ContextObject_dealloc(ctx);
  }
  return NULL;
}

static PyObject *
sennactx_Context_load(sennactx_ContextObject *self, PyObject *args, PyObject *keywds)
{
  sen_rc rc;
  const char *path;
  static char *kwlist[] = {"path", NULL};

  if (!PyArg_ParseTupleAndKeywords(args, keywds, "s", kwlist,
                                   &path)) {
    return NULL;
  }
  if (!self->ctx) { return NULL; }
  Py_BEGIN_ALLOW_THREADS
  rc = sen_ctx_load(self->ctx, path);
  Py_END_ALLOW_THREADS
  return Py_BuildValue("i", rc);
}

static PyObject *
sennactx_Context_send(sennactx_ContextObject *self, PyObject *args, PyObject *keywds)
{
  sen_rc rc;
  char *str;
  int str_len, flags;
  static char *kwlist[] = {"str", "flags", NULL};

  if (!PyArg_ParseTupleAndKeywords(args, keywds, "s#i", kwlist,
                                   &str,
                                   &str_len,
                                   &flags)) {
    return NULL;
  }
  if (!self->ctx) { return NULL; }
  Py_BEGIN_ALLOW_THREADS
  rc = sen_ctx_send(self->ctx, str, str_len, flags);
  Py_END_ALLOW_THREADS
  return Py_BuildValue("i", rc);
}

static PyObject *
sennactx_Context_recv(sennactx_ContextObject *self)
{
  sen_rc rc;
  int flags;
  char *str;
  unsigned int str_len;

  if (!self->ctx) { return NULL; }
  Py_BEGIN_ALLOW_THREADS
  rc = sen_ctx_recv(self->ctx, &str, &str_len, &flags);
  Py_END_ALLOW_THREADS
  return Py_BuildValue("(is#i)", rc, str, str_len, flags);
}

static PyObject *
sennactx_Context_close(sennactx_ContextObject *self)
{
  sen_rc rc;

  if (!self->ctx) { return NULL; }
  Py_BEGIN_ALLOW_THREADS
  rc = sen_ctx_close(self->ctx);
  Py_END_ALLOW_THREADS
  if (!rc) {
    self->ctx = NULL;
  }
  return Py_BuildValue("i", rc);
}

static PyObject *
sennactx_Context_info_get(sennactx_ContextObject *self)
{
  sen_rc rc;
  sen_ctx_info info;

  if (!self->ctx) { return NULL; }
  rc = sen_ctx_info_get(self->ctx, &info);
  /* TODO: handling unsigned int properlly */
  /* TODO: get outbuf */
  return Py_BuildValue("{s:i,s:i,s:i}",
                       "rc", rc,
                       "com_status", info.com_status,
                       "com_info", info.com_info
                       /* "outbuf", info.outbuf */
                      );
}

/* methods of classes */

static PyMethodDef sennactx_Db_methods[] = {
  {"create", (PyCFunction)sennactx_DbClass_create,
   METH_VARARGS | METH_KEYWORDS | METH_CLASS,
   "Create a Senna DB and Open an instance of it."},
  {"open", (PyCFunction)sennactx_DbClass_open,
   METH_VARARGS | METH_KEYWORDS | METH_CLASS,
   "Open a Senna DB instance."},
  {"close", (PyCFunction)sennactx_Db_close,
   METH_NOARGS,
   "Release Senna DB instance."},
  {"ctx_open", (PyCFunction)sennactx_Db_ctx_open,
   METH_VARARGS | METH_KEYWORDS,
   "Open a local senna context."},
  {NULL, NULL, 0, NULL}
};

static PyMethodDef sennactx_Context_methods[] = {
/*  {"open", (PyCFunction)sennactx_ContextClass_open,
   METH_VARARGS | METH_KEYWORDS | METH_CLASS,
   "Create a local Senna context."}, */
  {"connect", (PyCFunction)sennactx_ContextClass_connect,
   METH_VARARGS | METH_KEYWORDS | METH_CLASS,
   "Create a remote Senna context."},
  {"load", (PyCFunction)sennactx_Context_load,
   METH_VARARGS | METH_KEYWORDS,
   "Load a file into context."},
  {"send", (PyCFunction)sennactx_Context_send,
   METH_VARARGS | METH_KEYWORDS,
   "Send message to context."},
  {"recv", (PyCFunction)sennactx_Context_recv,
   METH_NOARGS,
   "Receive message from context."},
  {"close", (PyCFunction)sennactx_Context_close,
   METH_NOARGS,
   "Release Senna context."},
  {"info_get", (PyCFunction)sennactx_Context_info_get,
   METH_NOARGS,
   "Get context information."},
  {NULL, NULL, 0, NULL}
};

static PyMethodDef module_methods[] = {
  {NULL, NULL, 0, NULL}
};

/* type objects */

static PyTypeObject sennactx_DbType = {
  PyObject_HEAD_INIT(NULL)
  0,                                           /* ob_size */
  "sennactx.Db",                               /* tp_name */
  sizeof(sennactx_DbObject),                   /* tp_basicsize */
  0,                                           /* tp_itemsize */
  (destructor)sennactx_DbObject_dealloc,       /* tp_dealloc */
  0,                                           /* tp_print */
  0,                                           /* tp_getattr */
  0,                                           /* tp_setattr */
  0,                                           /* tp_compare */
  0,                                           /* tp_repr */
  0,                                           /* tp_as_number */
  0,                                           /* tp_as_sequence */
  0,                                           /* tp_as_mapping */
  0,                                           /* tp_hash  */
  0,                                           /* tp_call */
  0,                                           /* tp_str */
  0,                                           /* tp_getattro */
  0,                                           /* tp_setattro */
  0,                                           /* tp_as_buffer */
  Py_TPFLAGS_DEFAULT,                          /* tp_flags */
  "Senna Db objects",                          /* tp_doc */
  0,                                           /* tp_traverse */
  0,                                           /* tp_clear */
  0,                                           /* tp_richcompare */
  0,                                           /* tp_weaklistoffset */
  0,                                           /* tp_iter */
  0,                                           /* tp_iternext */
  sennactx_Db_methods,                         /* tp_methods */
  0,                                           /* tp_members */
  0,                                           /* tp_getset */
  0,                                           /* tp_base */
  0,                                           /* tp_dict */
  0,                                           /* tp_descr_get */
  0,                                           /* tp_descr_set */
  0,                                           /* tp_dictoffset */
  0,                                           /* tp_init */
  0,                                           /* tp_alloc */
  sennactx_DbObject_new,                       /* tp_new */
};

static PyTypeObject sennactx_ContextType = {
  PyObject_HEAD_INIT(NULL)
  0,                                           /* ob_size */
  "sennactx.Context",                          /* tp_name */
  sizeof(sennactx_ContextObject),              /* tp_basicsize */
  0,                                           /* tp_itemsize */
  (destructor)sennactx_ContextObject_dealloc,  /* tp_dealloc */
  0,                                           /* tp_print */
  0,                                           /* tp_getattr */
  0,                                           /* tp_setattr */
  0,                                           /* tp_compare */
  0,                                           /* tp_repr */
  0,                                           /* tp_as_number */
  0,                                           /* tp_as_sequence */
  0,                                           /* tp_as_mapping */
  0,                                           /* tp_hash  */
  0,                                           /* tp_call */
  0,                                           /* tp_str */
  0,                                           /* tp_getattro */
  0,                                           /* tp_setattro */
  0,                                           /* tp_as_buffer */
  Py_TPFLAGS_DEFAULT,                          /* tp_flags */
  "Senna Context objects",                     /* tp_doc */
  0,                                           /* tp_traverse */
  0,                                           /* tp_clear */
  0,                                           /* tp_richcompare */
  0,                                           /* tp_weaklistoffset */
  0,                                           /* tp_iter */
  0,                                           /* tp_iternext */
  sennactx_Context_methods,                    /* tp_methods */
  0,                                           /* tp_members */
  0,                                           /* tp_getset */
  0,                                           /* tp_base */
  0,                                           /* tp_dict */
  0,                                           /* tp_descr_get */
  0,                                           /* tp_descr_set */
  0,                                           /* tp_dictoffset */
  0,                                           /* tp_init */
  0,                                           /* tp_alloc */
  sennactx_ContextObject_new,                  /* tp_new */
};

/* consts */

typedef struct _ConstPair {
  const char *name;
  int value;
} ConstPair;

static ConstPair consts[] = {
  /* sen_rc */
  {"RC_SUCCESS", sen_success},
  {"RC_MEMORY_EXHAUSTED", sen_memory_exhausted},
  {"RC_INVALID_FORMAT", sen_invalid_format},
  {"RC_FILE_OPERATION_ERROR", sen_file_operation_error},
  {"RC_OTHER_ERROR", sen_other_error},
  /* sen_encoding */
  {"ENC_DEFAULT", sen_enc_default},
  {"ENC_NONE", sen_enc_none},
  {"ENC_EUC_JP", sen_enc_euc_jp},
  {"ENC_UTF8", sen_enc_utf8},
  {"ENC_SJIS", sen_enc_sjis},
  {"ENC_LATIN1", sen_enc_latin1},
  {"ENC_KOI8R", sen_enc_koi8r},
  /* sen_ctx flags */
  {"CTX_MORE", SEN_CTX_MORE},
  {"CTX_USEQL", SEN_CTX_USEQL},
  {"CTX_BATCHMODE", SEN_CTX_BATCHMODE},
  /* end */
  {NULL, 0}
};

#ifndef PyMODINIT_FUNC
#define PyMODINIT_FUNC void
#endif
PyMODINIT_FUNC
initsennactx(void)
{
  unsigned int i;
  PyObject *m, *dict;

  if (!(m = Py_InitModule3("sennactx", module_methods,
                           "Senna context module."))) {
    goto exit;
  }
  sen_init();

  /* register classes */

  if (PyType_Ready(&sennactx_DbType) < 0) { goto exit; }
  if (PyType_Ready(&sennactx_ContextType) < 0) { goto exit; }
  Py_INCREF(&sennactx_DbType);
  PyModule_AddObject(m, "Db", (PyObject *)&sennactx_DbType);
  Py_INCREF(&sennactx_ContextType);
  PyModule_AddObject(m, "Context", (PyObject *)&sennactx_ContextType);

  /* register consts */

  if (!(dict = PyModule_GetDict(m))) { goto exit; }

  for (i = 0; consts[i].name; i++) {
    PyObject *v;
    if (!(v = PyInt_FromLong(consts[i].value))) {
      goto exit;
    }
    PyDict_SetItemString(dict, consts[i].name, v);
    Py_DECREF(v);
  }
  if (Py_AtExit((void (*)(void))sen_fin)) { goto exit; }
exit:
  if (PyErr_Occurred()) {
    PyErr_SetString(PyExc_ImportError, "sennactx: init failed");
  }
}
