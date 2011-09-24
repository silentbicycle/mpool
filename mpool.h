#ifndef MPOOL_H
#define MPOOL_H

/* Turn on debugging traces */
#ifndef MPOOL_DEBUG
#define MPOOL_DEBUG 0
#endif

/* Allow overriding malloc functions. */
#ifndef MPOOL_MALLOC
#define MPOOL_MALLOC(sz) malloc(sz)
#define MPOOL_REALLOC(p, sz) realloc(p, sz)
#define MPOOL_FREE(p, sz) free(p)
#endif

typedef struct {
        int ct;                /* actual pool count */
        int pal;               /* pool array length (2^x ceil of ct) */
        int min_pool;          /* minimum pool size */
        int max_pool;          /* maximum pool size */
        int pg_sz;             /* page size, typically 4096 */
        void **ps;             /* pools */
        int *sizes;            /* chunk size for each pool */
        void *hs[1];           /* heads for pools' free lists */
} mpool;

/* Initialize a memory pool for allocations between 2^min2 and 2^max2,
 * inclusive. (Larger allocations will be directly allocated and freed
 * via mmap / munmap.) */
mpool *mpool_init(int min2, int max2);

/* Allocate SZ bytes. */
void *mpool_alloc(mpool *mp, int sz);

/* mmap a new memory pool of TOTAL_SZ bytes, then build an internal
 * freelist of SZ-byte cells, with the head at (result)[0]. */
void **mpool_new_pool(unsigned int sz, unsigned int total_sz);

/* Return pointer P (SZ bytes in size) to the appropriate pool. */
void mpool_repool(mpool *mp, void *p, int sz);

/* Resize P from OLD_SZ to NEW_SZ, copying content. */
void *mpool_realloc(mpool *mp, void *p, int old_sz, int new_sz);

/* Free the pool. */
void mpool_free(mpool *mp);

#endif
