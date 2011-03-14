#ifndef MPOOL_H
#define MPOOL_H

/* Save size info for large pools. Adds overhead when repooling, but
 * makes it possible to free them without knowing their exact size
 * (including when the whole mpool set is freed).
 * If 0, large pools *must* be manually deallocated (via mpool_repool),
 * otherwise they will leak.
 */
#ifndef LG_POOL_AUTO
#define LG_POOL_AUTO 1
#endif

/* Turn on debugging traces */
#define DBG 0

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

mpool *mpool_init(int min2, int max2);
void *mpool_alloc(mpool *mp, int sz);
void mpool_repool(mpool *mp, void *p, int sz);
void mpool_free(mpool *mp);

#endif
