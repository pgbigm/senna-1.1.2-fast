/* Copyright(C) 2004 Brazil

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
#ifndef SENNA_IN_H
#define SENNA_IN_H

#ifdef __GNUC__
#define _GNU_SOURCE
#endif /* __GNUC__ */

#include "config.h"

#ifdef USE_AIO
/* #define __USE_XOPEN2K 1 */
#define _XOPEN_SOURCE 600
#else
#if !defined(__APPLE__) && !defined(__FreeBSD__)
#define _XOPEN_SOURCE 500
#endif
#endif /* USE_AIO */

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif /* HAVE_STDLIB_H */

#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif /* HAVE_STDINT_H */

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif /* HAVE_SYS_TYPES_H */

#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif /* HAVE_SYS_PARAM_H */

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */

#ifdef HAVE_SYS_MMAN_H
#include <sys/mman.h>
#endif /* HAVE_SYS_MMAN_H */

#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#else /* HAVE_SYS_TIME_H */
#include <sys/timeb.h>
#endif /* HAVE_SYS_TIME_H */

#ifdef HAVE_SYS_RESOURCE_H
#include <sys/resource.h>
#endif /* HAVE_SYS_RESOURCE_H */

#ifdef WIN32

#pragma warning(disable: 4996)
#include <io.h>
#include <basetsd.h>
#include <process.h>
#include <winsock2.h>
#include <windows.h>
#include <stddef.h>
#include <windef.h>
#include <float.h>
#define PATH_MAX (MAX_PATH - 1)
#define inline _inline
#define snprintf _snprintf
#define vsnprintf _vsnprintf
#define unlink _unlink
#define open _open
#define lseek _lseek
#define read _read
#define getpid _getpid
#if _MSC_VER < 1400
#define fstat _fstat
#endif /* _MSC_VER < 1400 */
#define write _write
#define close _close
#define usleep Sleep
#define sleep(x) Sleep((x) * 1000)
#define fpclassify _fpclass
#define uint8_t UINT8
#define int8_t INT8
#define int_least8_t INT8
#define uint_least8_t UINT8
#define int16_t INT16
#define uint16_t UINT16
#define int32_t INT32
#define uint32_t UINT32
#define int64_t INT64
#define uint64_t UINT64
#define ssize_t SSIZE_T
#undef MSG_WAITALL
#define MSG_WAITALL 0
#define PATH_SEPARATOR "\\"
#define CASE_FP_NAN case _FPCLASS_SNAN: case _FPCLASS_QNAN:
#define CASE_FP_INFINITE case _FPCLASS_NINF: case _FPCLASS_PINF:
#define SHUT_RDWR SD_BOTH

#define islessgreater(x,y)  (!_isnan(x) && !_isnan(y) && ((x) < (y) || (x) > (y)))
#define isless(x,y)         (!_isnan(x) && !_isnan(y) && ((x) < (y)))
#define isgreater(x,y)      (!_isnan(x) && !_isnan(y) && ((x) > (y)))
typedef SOCKET sen_sock;
#define sen_sock_close closesocket

#define CALLBACK __stdcall

#else /* WIN32 */

#ifndef PATH_MAX
#if defined(MAXPATHLEN)
#define PATH_MAX MAXPATHLEN
#else /* MAXPATHLEN */
#define PATH_MAX 1024
#endif /* MAXPATHLEN */
#endif /* PATH_MAX */
#ifndef INT_LEAST8_MAX
typedef char int_least8_t;
#endif /* INT_LEAST8_MAX */
#ifndef UINT_LEAST8_MAX
typedef unsigned char uint_least8_t;
#endif /* UINT_LEAST8_MAX */
#define PATH_SEPARATOR "/"
#define CASE_FP_NAN case FP_NAN:
#define CASE_FP_INFINITE case FP_INFINITE:
typedef int sen_sock;
#define sen_sock_close close
#define CALLBACK
#endif /* WIN32 */

#ifndef INT32_MAX
#define INT32_MAX (2147483647)
#endif /* INT32_MAX */

#ifndef INT32_MIN
#define INT32_MIN (-2147483648)
#endif /* INT32_MIN */

#ifdef HAVE_PTHREAD_H
#include <pthread.h>
typedef pthread_t sen_thread;
typedef pthread_mutex_t sen_mutex;
#define THREAD_CREATE(thread,func,arg) (pthread_create(&(thread), NULL, (func), (arg)))
#define MUTEX_INIT(m) pthread_mutex_init(&m, NULL)
#define MUTEX_LOCK(m) pthread_mutex_lock(&m)
#define MUTEX_UNLOCK(m) pthread_mutex_unlock(&m)
#define MUTEX_DESTROY(m)

typedef pthread_cond_t sen_cond;
#define COND_INIT(c) pthread_cond_init(&c, NULL)
#define COND_SIGNAL(c) pthread_cond_signal(&c)
#define COND_WAIT(c,m) pthread_cond_wait(&c, &m)

typedef pthread_key_t sen_thread_key;
#define THREAD_KEY_CREATE pthread_key_create
#define THREAD_KEY_DELETE pthread_key_delete
#define THREAD_SETSPECIFIC pthread_setspecific
#define THREAD_GETSPECIFIC pthread_getspecific

#else /* HAVE_PTHREAD_H */

/* todo */
typedef int sen_thread_key;
#define THREAD_KEY_CREATE(key,destr)
#define THREAD_KEY_DELETE(key)
#define THREAD_SETSPECIFIC(key)
#define THREAD_GETSPECIFIC(key,value)

#ifdef WIN32
typedef uintptr_t sen_thread;
typedef CRITICAL_SECTION sen_mutex;
#define THREAD_CREATE(thread,func,arg) (((thread)=_beginthreadex(NULL, 0, (func), (arg), 0, NULL)) == NULL)
#define MUTEX_INIT(m) InitializeCriticalSection(&m)
#define MUTEX_LOCK(m) EnterCriticalSection(&m)
#define MUTEX_UNLOCK(m) LeaveCriticalSection(&m)
#define MUTEX_DESTROY(m) DeleteCriticalSection(&m)

#else /* WIN32 */
/* todo */
#endif /* WIN32 */

typedef int sen_cond;
#define COND_INIT(c) ((c) = 0)
#define COND_SIGNAL(c)
#define COND_WAIT(c,m) do { MUTEX_UNLOCK(m); usleep(1000); MUTEX_LOCK(m); } while (0)
/* todo : must be enhanced! */

#endif /* HAVE_PTHREAD_H */

#ifdef __GNUC__

#if (defined(__i386__) || defined(__x86_64__)) /* ATOMIC ADD */
#define SEN_ATOMIC_ADD_EX(p,i,r) \
  __asm__ __volatile__ ("lock; xaddl %0,%1" : "=r"(r), "=m"(*p) : "0"(i), "m" (*p))
#define SEN_BIT_SCAN_REV(v,r) \
  __asm__ __volatile__ ("\tmovl %1,%%eax\n\tbsr %%eax,%0\n" : "=r"(r) : "r"(v) : "%eax")
#elif (defined(__PPC__) || defined(__ppc__)) /* ATOMIC ADD */
#define SEN_ATOMIC_ADD_EX(p,i,r) \
  __asm__ __volatile__ ("\n1:\n\tlwarx %0, 0, %1\n\tadd %0, %0, %2\n\tstwcx. %0, 0, %1\n\tbne- 1b\n\tsub %0, %0, %2" : "=&r" (r) : "r" (p), "r" (i) : "cc", "memory");
/* todo */
#define SEN_BIT_SCAN_REV(v,r)   for (r = 31; r && !((1 << r) & v); r--)
#else /* ATOMIC ADD */
/* todo */
#define SEN_BIT_SCAN_REV(v,r)   for (r = 31; r && !((1 << r) & v); r--)
#endif /* ATOMIC ADD */

#ifdef __i386__ /* ATOMIC 64BIT SET */
#define SEN_SET_64BIT(p,v) \
  __asm__ __volatile__ ("\txchgl %%esi, %%ebx\n1:\n\txchgl %%esi, %%ebx\n\tmovl (%0), %%eax\n\tmovl 4(%0), %%edx\n\tlock; cmpxchg8b(%0)\n\tjnz 1b\n\txchgl %%ebx, %%esi" : : "D"(p), "S"(*(((uint32_t *)&(v))+0)), "c"(*(((uint32_t *)&(v))+1)) : "ax", "dx", "memory");
#elif defined(__x86_64__) /* ATOMIC 64BIT SET */
#define SEN_SET_64BIT(p,v) \
  *(p) = (v);
#endif /* ATOMIC 64BIT SET */

#elif (defined(WIN32) || defined (WIN64)) /* __GNUC__ */
#define SEN_ATOMIC_ADD_EX(p,i,r) \
  (r) = (uint32_t)InterlockedExchangeAdd((int32_t *)(p), (int32_t)(i));
#ifdef WIN32 /* ATOMIC 64BIT SET */
#define SEN_SET_64BIT(p,v) \
/* TODO: use _InterlockedCompareExchange64 or inline asm */
#elif defined WIN64 /* ATOMIC 64BIT SET */
#define SEN_SET_64BIT(p,v) \
  *(p) = (v);
#endif /* ATOMIC 64BIT SET */

/* todo */
#define SEN_BIT_SCAN_REV(v,r)   for (r = 31; r && !((1 << r) & v); r--)

#else /* __GNUC__ */
/* todo */
#define SEN_BIT_SCAN_REV(v,r)   for (r = 31; r && !((1 << r) & v); r--)
#endif /* __GNUC__ */

typedef uint8_t byte;

#ifndef SENNA_H
#include "senna.h"
#endif /* SENNA_H */

#endif /* SENNA_IN_H */
