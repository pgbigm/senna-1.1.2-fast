/* Copyright(C) 2005 Takuo Kitame <kitame @ valinux. co. jp>

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

#ifdef USE_AIO

#include "senna_in.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>

#include <sys/param.h>

#include <sys/shm.h>
#include <sys/mman.h>
#include <fcntl.h>

#include "cache.h"

#define SEN_ATOMIC_DEC(x) { uint32_t _n; SEN_ATOMIC_ADD_EX((x), -1, _n); }

/* feature switch */
int sen_aio_enabled;
int sen_debug_print;

/* cache structure  */
int sen_cache_size;  /* size of cache (MB) */
int sen_cache_block; /* alignment */
int sen_hash_size;   /* size of hash array */

#define DEFAULT_CACHE_SIZE   256
#define DEFAULT_CACHE_BLOCK  4096
#define DEFAULT_HASH_SIZE    1024

#define MEM_ALIGN    sen_cache_block
#define CACHE_SIZE   (sen_cache_size * 1024 * 1024)
#define CACHE_NUM    (CACHE_SIZE / MEM_ALIGN) /* number of cache data */
#define HASH_SIZE    sen_hash_size

/* shm namespaces */
#define CACHE_DATA_SHM_NAME    "senna_cache_data"
#define CACHE_BIN_SHM_NAME     "senna_cache_bin"
#define CACHE_COUNTER_SHM_NAME "senna_cache_counter"
#define CACHE_HASH_SHM_NAME    "senna_cache_hash"

/* hash block */
typedef struct _CacheHash CacheHash;
struct _CacheHash {
    int num; /* first CacheData number */
    int lock;
};

/* for debug  */

/* mmaped to shm */
unsigned int *ccount; /* global counter */
void *cache_data; /* CacheData array */
void *cache_bin;  /* Cache binary */
void *cache_hash; /* Hash array */

/* misc macro */
#define HASH_VAL (1024 * 128)
//#define CALC_HASH(x,y,z)  ((x + y + (z/sen_cache_block)) % HASH_SIZE)
#define CALC_HASH(x,y,z)  ((x + y + (z/HASH_VAL)) % HASH_SIZE)
#define GET_CD_NUM(num)   ((CacheData*)(cache_data + sizeof(CacheData)*num))
#define GET_HASH_NUM(num) ((CacheHash*)(cache_hash + sizeof(CacheHash)*num))
#define GET_HASH_CD(cd)   (GET_HASH_NUM(CALC_HASH(cd->dev,cd->inode,cd->offset)))

/*
 * Initialize shm region. 
 */
static void
sen_cache_init (void)
{
    int shmfd;
    int ret;
    char shm_name[PATH_MAX];

    if (getenv("SEN_CACHE_SIZE")) {
        sen_cache_size = atoi(getenv("SEN_CACHE_SIZE"));
    } else {
        sen_cache_size = DEFAULT_CACHE_SIZE;
    }
    if (getenv("SEN_CACHE_BLOCK")) {
        sen_cache_block = atoi(getenv("SEN_CACHE_BLOCK"));
    } else {
        sen_cache_block = DEFAULT_CACHE_BLOCK;
    }
    if (getenv("SEN_HASH_SIZE")) {
        sen_hash_size = atoi(getenv("SEN_HASH_SIZE"));
    } else {
        sen_hash_size = DEFAULT_HASH_SIZE;
    }
    dp ("cache size: %d MB, cache block: %d, hash size: %d\n", sen_cache_size, sen_cache_block, sen_hash_size);

    /* Array of CacheData */
    snprintf (shm_name, PATH_MAX, "%s_%d", CACHE_DATA_SHM_NAME, MEM_ALIGN);
    shmfd = shm_open (shm_name, O_CREAT|O_RDWR|O_EXCL, 0666);
    if (shmfd < 0 && errno == EEXIST) {
        /* exist : no op */
    } else {
        int h;
        int num;
        size_t s = sizeof(CacheData) * CACHE_NUM;
        void *p = malloc (s);

        for (h = 0; h < HASH_SIZE; h++) {
            /* Initialize with INVALID */
            for (num = 0; num < CACHE_NUM / HASH_SIZE; num++) {
                int number = num + (h * (CACHE_NUM / HASH_SIZE));
                CacheData *cd = p + sizeof(CacheData) * number;
                
                cd->num = number; /* unique number */
                cd->ref = 0; 

                /* vector list */
                if (cd->num + 1 < (h+1) * (CACHE_NUM / HASH_SIZE)) {
                    cd->next = cd->num + 1;
                } else {
                    cd->next = -1; /* end of list */
                }
                cd->flag = CACHE_INVALID;
            }
        }
        
        ret = write (shmfd, p, s);
        free (p);
        if (ret < 0) {
            perror ("sen_cache_init: write");
            exit (1);
        }
    }
    
    /* Cache binary */
    snprintf (shm_name, PATH_MAX, "%s_%d", CACHE_BIN_SHM_NAME, MEM_ALIGN);
    shmfd = shm_open (shm_name, O_CREAT|O_RDWR|O_EXCL, 0666);
    if (shmfd < 0 && errno == EEXIST) {
        /* exist : no op */
    } else {
        size_t s = CACHE_SIZE;
        void *p = malloc (s);
        ret = write (shmfd, p, s);
        free (p);
	if (ret < 0) {
            perror ("sen_cache_init: write");
            exit (1);
	}
    }

    /* global counter */
    snprintf (shm_name, PATH_MAX, "%s_%d", CACHE_COUNTER_SHM_NAME, MEM_ALIGN);
    shmfd = shm_open (shm_name, O_CREAT|O_RDWR|O_EXCL, 0666);
    if (shmfd < 0 && errno == EEXIST) {
        /* exist : no op */
    } else {
        unsigned int i = 1;
        ret = write (shmfd, &i, sizeof(unsigned int));
	if (ret < 0) {
	  perror ("sen_cache_init: write");
	  exit (1);
	}
    }

    /* Array of CacheHash */
    snprintf (shm_name, PATH_MAX, "%s_%d", CACHE_HASH_SHM_NAME, MEM_ALIGN);
    shmfd = shm_open (shm_name, O_CREAT|O_RDWR|O_EXCL, 0666);
    if (shmfd < 0 && errno == EEXIST) {
        /* exist : no op */
    } else {
        int i;
        size_t s = sizeof(CacheHash) * HASH_SIZE;
        void *p = malloc(s);
        for (i = 0; i < HASH_SIZE; i++) {
            CacheHash *hash = p + sizeof(CacheHash) * i;

            /* first CacheData */
            hash->num = i * (CACHE_NUM / HASH_SIZE);

            hash->lock = 0;
        }
        write (shmfd, p, s);
        free (p);
    }
}

/*
 * open shm region, and mmap().
 */
void
sen_cache_open (void)
{
    int shmfd;
    char shm_name[PATH_MAX];

    /* no op when disabled */
    if (!sen_aio_enabled) return;
    
    sen_cache_init ();

    /* global counter */
    snprintf (shm_name, PATH_MAX, "%s_%d", CACHE_COUNTER_SHM_NAME, MEM_ALIGN);
    shmfd = shm_open (shm_name, O_CREAT|O_RDWR, 0666);
    if ( (ccount = (unsigned int *)mmap(NULL, sizeof(unsigned int),
                                PROT_READ|PROT_WRITE, MAP_SHARED, shmfd, 0)) == (unsigned int *)-1) {
        perror("mmap (counter)");
        exit (1);
    }

    /* Cache binary */
    snprintf (shm_name, PATH_MAX, "%s_%d", CACHE_BIN_SHM_NAME, MEM_ALIGN);
    shmfd = shm_open (shm_name, O_CREAT|O_RDWR, 0666);
    if ( (cache_bin = mmap (NULL, CACHE_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED,
			    shmfd, 0)) == (void *)-1) {
        perror("mmap (cache)");
        exit (1);
    }

    /* CacheHash */
    snprintf (shm_name, PATH_MAX, "%s_%d", CACHE_HASH_SHM_NAME, MEM_ALIGN);
    shmfd = shm_open (shm_name, O_CREAT|O_RDWR, 0666);
    cache_hash = mmap (NULL, sizeof(CacheHash) * HASH_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED,
                       shmfd, 0);

    /* CacheData */
    snprintf (shm_name, PATH_MAX, "%s_%d", CACHE_DATA_SHM_NAME, MEM_ALIGN);    
    shmfd = shm_open (shm_name, O_CREAT|O_RDWR, 0666);
    if ( (cache_data = mmap(NULL, sizeof(CacheData) * CACHE_NUM,
                            PROT_READ|PROT_WRITE, MAP_SHARED, shmfd, 0)) == (void *)-1) {
        perror("mmap (CacheData)");
        exit (1);
    }
}

/*
 * search Hit, Free or LRU node.
 *
 * @dev: device number
 * @inode: inode number
 * @offset: file offset
 *
 * return: CacheData or NULL (Cache region is full of referenced)
 */
static CacheData *
sen_cache_hash_search (dev_t dev, ino_t inode, size_t offset, size_t size)
{
    int h = CALC_HASH(dev,inode,offset);
    CacheData *cd = NULL;   /* expire CacheData */
    CacheData *fcd = NULL;  /* free CacheData */
    CacheHash *hash = GET_HASH_NUM(h);
    unsigned int c = *ccount; /* current stamp */
    unsigned int l = 0; /* oldest stamp */
    int i;
    int r = 0; /* retry count */

LRU_retry:
    SEN_ATOMIC_ADD_EX(&(hash->lock), 1, i);
    if (i > 0) {
        SEN_ATOMIC_DEC(&(hash->lock));
        usleep (1);
        goto LRU_retry;
    }

    l = 0;
    for (i = 0; i < CACHE_NUM / HASH_SIZE; i++) {
        int number = i + (h * (CACHE_NUM / HASH_SIZE));
        CacheData *tmp = GET_CD_NUM(number);

        if (tmp->offset == offset &&
            tmp->inode == inode &&
            tmp->dev == dev &&
            tmp->flag != CACHE_INVALID) {

            if (tmp->flag == CACHE_LOCKED) {
                /* still in locked */
                SEN_ATOMIC_DEC(&(hash->lock));
                usleep(1);
                goto LRU_retry;
            }
            
            tmp->stamp = (*ccount)++;            
             
            /*
             *  should ignore CACHE_READ reference,
             *  because it will be upped by other process.
             */
            if (tmp->flag == CACHE_VALID) {
                tmp->ref++;
                dp ("CacheData[%d]: ref: %d\n", tmp->num, tmp->ref); 
            }

            SEN_ATOMIC_DEC(&(hash->lock));
            return tmp;
        }

        /* LRU or INVALID search */
        if (fcd) continue; /* already found free area */
        
        if (tmp->flag == CACHE_INVALID && tmp->ref == 0) {
            fcd = tmp;
        } else if (!fcd && (unsigned int)(c - tmp->stamp) > l) {
            l = (unsigned int)(c - tmp->stamp);
            cd = tmp;
        }
    }

    if (fcd || (cd && cd->flag == CACHE_VALID && cd->ref == 0)) {

        if (!fcd) {
            /* Expire LRU and unreferenced CacheData */
            dp ("Expire[%d] CacheData[%d] stamp=%u dev=%lu inode=%lu offset=%u size=%u\n",
                h, cd->num, cd->stamp, (long)cd->dev, (long)cd->inode, cd->offset, cd->size);

            fcd = cd;
        }

        /* free CacheData is now available */
        fcd->ref++;
        fcd->stamp = (*ccount)++;

        fcd->flag = CACHE_LOCKED;

        fcd->dev = dev;
        fcd->inode = inode;
        fcd->offset = offset;
        fcd->size = size;

        dp ("CacheData[%d]: ref: %d\n", fcd->num, fcd->ref);         

        SEN_ATOMIC_DEC(&(hash->lock));
        return fcd;
    } else {
        /* Not found Free CacheDate &&  LRU CacheData is used, cannot expire. */
        SEN_ATOMIC_DEC(&(hash->lock));
        usleep(2);
        cd->stamp = c; /* update counter stamp with current */
        if (++r > CACHE_NUM / HASH_SIZE) {
            /* no space for new cache in this hash block */
            dp ("No room for new CacheData: hash[%d] is full of used\n", h);
            return NULL;
        }
        goto LRU_retry;
    }

    /* should not be reached */
    return NULL;
}

/*
 * read via shm cache.
 *
 * @oper: CacheIOOper, structure of CacheData, aiocb and real-read flag.
 * @dev: device number
 * @inode: inode number
 * @offset: file offset
 *
 * return: pointer to buffer  or NULL (no cache room, cannot use cache)
 *
 */
void *
sen_cache_read (CacheIOOper *oper, dev_t dev, ino_t inode, size_t offset, size_t size)
{
    void *p = NULL;
    CacheData *cd = NULL;

    cd = sen_cache_hash_search (dev, inode, offset, size);

    if (!cd) return NULL; /* no room */

    switch (cd->flag) {
    case CACHE_INVALID:
    case CACHE_LOCKED:
        /* Cache Miss */

        /* Initialize CacheData */
        cd->flag = CACHE_READ;

        /* set aiocb */
        oper->iocb->aio_buf = p = cache_bin + (cd->num * MEM_ALIGN);
        oper->iocb->aio_nbytes = size;
        oper->iocb->aio_offset = offset;
        
        /* real read() is required */
        oper->read = 1;
        
        dp ("Miss CacheData[%d] stamp=%u dev=%lu inode=%lu offset=%d size=%d flag=CACHE_INVALID ref=%d\n",
            cd->num, cd->stamp, (long)dev, (long)inode, offset, size, cd->ref);
        break;
    case CACHE_VALID:
        /* Hit complete Cache */
        dp ("Hit CacheData[%d] stamp=%u dev=%lu inode=%lu offset=%d size=%d flag=CACHE_VALID ref=%d\n",
            cd->num, cd->stamp, (long)dev, (long)inode, offset, size, cd->ref);
        p = cache_bin + (cd->num * MEM_ALIGN);
        break;
    case CACHE_READ:
        /* Hit but reading by other operation */

        dp ("Hit CacheData[%d] stamp=%u dev=%lu inode=%lu offset=%d size=%d flag=CACHE_READ ref=%d\n",
            cd->num, cd->stamp, (long)dev, (long)inode, offset, size, cd->ref);

        /* will be ignored, but should not return NULL */
        p = cache_bin + (cd->num * MEM_ALIGN); 
        break;
    default:
        break;
    }
    
    /* IOOper : CacheData */
    oper->cd = cd;
    
    return p;
}

/*
 * count up reference
 *
 * @number: CacheData number
 */
void
sen_cache_data_unref (int number)
{
    CacheData *cd = GET_CD_NUM(number);
    CacheHash *hash = GET_HASH_CD(cd);
    int c;

    /* 
     * FIXME: assert cd and hash.
     */
    
unref_retry:
    SEN_ATOMIC_ADD_EX(&(hash->lock), 1, c);
    if (c > 0) {
        // printf ("in unref_retry: hash=%d pid=%d\n",
        //          hash->num, getpid());
        SEN_ATOMIC_DEC(&(hash->lock));
        usleep (1);
        goto unref_retry;
    }
    cd->ref--;
    SEN_ATOMIC_DEC(&(hash->lock));
    dp ("CacheData[%d]: unref: %d\n", cd->num, cd->ref);
}

/*
 * count up reference
 *
 * @number: CacheData number
 */
void
sen_cache_data_ref (int number)
{
    CacheData *cd = GET_CD_NUM(number);
    CacheHash *hash = GET_HASH_CD(cd);
    int c;
    
    /* 
     * FIXME: assert cd and hash.
     */

ref_retry:
    SEN_ATOMIC_ADD_EX(&(hash->lock), 1, c);
    if (c > 0) {
        //printf ("in ref_retry: hash=%d pid=%d\n",
        // hash->num, getpid());
        SEN_ATOMIC_DEC(&(hash->lock));
        usleep (1);
        goto ref_retry;
    }
    cd->ref++;
    SEN_ATOMIC_DEC(&(hash->lock));
    dp ("CacheData[%d]: ref: %d\n", cd->num, cd->ref); 
}

/*
 * mark CacheData as invalid  between pos and pos+size.
 *
 * @dev: device number.
 * @inode: inode number.
 * @offset: real offset.
 * @size: real required size.
 *
 */
void
sen_cache_mark_invalid (dev_t dev, ino_t inode, off_t offset, size_t size)
{
    off_t voffset = offset - (offset % MEM_ALIGN);
    size_t vsize = offset + size;
    int number, hash_begin, hash_end;
    CacheData *cd;

    vsize = ((vsize -1) / MEM_ALIGN + 1) *  MEM_ALIGN;
    vsize = vsize - voffset;

    hash_begin = CALC_HASH(dev, inode, voffset);
    hash_end   = CALC_HASH(dev, inode, (voffset+vsize-1));
    if (hash_end < hash_begin)
        hash_end = hash_end + HASH_SIZE;
    /*
     *  h_b  |     |h_e  |h_e+1     // begin | end | end+1
     *  oooo  oooo  oooo  oooo      // hash
     *  n       n++     <           // cd number
     */
    /*
     * CACHE_NUM / HASH_SIZE = number of cd in a HASH.
     */
    for (number = hash_begin * (CACHE_NUM / HASH_SIZE);
         number < (hash_end + 1) * (CACHE_NUM / HASH_SIZE);
         number++) {

        if (number >= CACHE_NUM)
            cd = GET_CD_NUM(number - CACHE_NUM);
        else
            cd = GET_CD_NUM(number);
        
        if (cd->offset >= voffset &&
            cd->offset < voffset + vsize &&
            cd->inode == inode &&
            cd->dev == dev) {
            dp ("MAI CacheData[%d] stamp=%u dev=%lu inode=%lu offset=%d size=%d flag=CACHE_READ ref=%d\n",
                cd->num, cd->stamp, (long)cd->dev, (long)cd->inode, cd->offset, cd->size, cd->ref);
            cd->flag = CACHE_INVALID;
        }
    }
}

/*
 * for DEBUG.
 *
 */
void
sen_cache_dump (void)
{
    int i,l;
    int used;
    int allofused = 0;
    for (i = 0; i < HASH_SIZE; i++) {
        used = 0;
        for (l = i * (CACHE_NUM / HASH_SIZE); l < (i +1) * (CACHE_NUM/HASH_SIZE); l++) {
            CacheData *cd = GET_CD_NUM(l);
            if (cd->flag != CACHE_INVALID)
                used++;
            printf (" HASH[%d] CacheData[%d]: dev=%lu inode=%lu offset=%u flag=%d ref=%d\n",
                    i, l, (long)cd->dev, (long)cd->inode, cd->offset, cd->flag, cd->ref);
        }
        allofused = allofused + used;
        printf ("HASH %d: %d/%d used.\n---\n", i, used, CACHE_NUM / HASH_SIZE);
    }
    printf ("ALL: %d/%d used.\n", allofused, CACHE_NUM);
}

#endif /* USE_AIO */
