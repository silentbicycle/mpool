/* 
 * Copyright (c) 2011 Scott Vokes <vokes.s@gmail.com>
 *  
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *  
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * A memory pool allocator, designed for systems that need to
 * allocate/free pointers in amortized O(1) time. Memory is allocated a
 * page at a time, then added to a set of pools of equally sized
 * regions. A free list for each size is maintained in the unused
 * regions. When a pointer is repooled, its size is looked up (if not
 * provided), then it is put at the head of the appropriate pool's free
 * list.
 * 
 * Allocations larger than mp->max_pool (configurable, usually over 2048
 * bytes) are allocated whole via mmap and freed immediately via munmap;
 * no free list is used. Also, see the note about LG_POOL_AUTO in
 * mpool.h.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <err.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <math.h>

#include "mpool.h"

static void *get_mmap(long sz) {
        void *p = mmap(0, sz, PROT_READ|PROT_WRITE,
            MAP_PRIVATE|MAP_ANON, -1, 0);
        if (p == NULL) err(1, "mmap");
        return p;
}

/* Each cell in the pool should point to the next cell,
 * the last to NULL (later, to another pool). */
static void **new_pool(unsigned int sz, unsigned int totalsz) {
        void *p = get_mmap(totalsz);
        int i, o=0, lim;               /* o=offset */
        int **pool = (int **)p;
        void *last = NULL;
        assert(pool);
        assert(sz > sizeof(void *));

        lim = (totalsz/sz);
        if (DBG) fprintf(stderr, "sz: %d, lim: %d => %d\n", sz, lim, lim * sz);
        for (i=0; i<lim; i++) {
                if (last) assert(last == pool[o]);
                o = (i*sz)/sizeof(void *);
                pool[o] = (int *) &pool[o+(sz/sizeof(void *))];
                last = pool[o];
                if (DBG > 1) fprintf(stderr, "%d (%d / 0x%04x) -> %p = %p\n",
                    i, o, o, &pool[o], pool[o]);
        }
        pool[o] = NULL;
        return p;
}

/* Add a new pool, resizing the pool array if necessary. */
static void add_pool(mpool *mp, void *p, int sz) {
        void **nps, **ps;       /* new pools, current pools */
        int i, *nsizes, *sizes;

        if (DBG) fprintf(stderr, "Adding pool (%d/%d) at %p, sz %d\n",
            mp->ct, mp->pal, p, sz);
        if (mp->ct == mp->pal) {
                mp->pal *= 2;   /* ram will exhaust before overflow... */
                nps = malloc(mp->pal * sizeof(void **));
                nsizes = malloc(mp->pal * sizeof(int *));
                if (nps == NULL || nsizes == NULL) err(1, "malloc");
                sizes = mp->sizes;
                ps = mp->ps;
                for (i=0; i<mp->ct; i++) {
                        nps[i] = ps[i]; nsizes[i] = sizes[i];
                }
                mp->sizes = nsizes;
                mp->ps = nps;
                free(ps); free(sizes);
        }

        mp->ps[mp->ct] = p;
        mp->sizes[mp->ct] = sz;
        mp->ct++;
}

/* Initialize a memory pool set, with pools in sizes
 * 2^min2 to 2^max2. */
mpool *mpool_init(int min2, int max2) {
        int i, sz;                     /* size of each cell in this pool */
        int palen;                     /* length of pool array */
        int ct = ct = max2 - min2 + 1; /* pool array count */
        long pgsz = sysconf(_SC_PAGESIZE);
        mpool *mp;
        void **p, *pools;
        int *sizes;

        palen = pow(2, ceil(log2(ct))); /* round to next power of 2 */
        if (DBG) fprintf(stderr, "Initializing an mpool for cells %d - %d bytes\n",
            (int)pow(2, min2), (int)pow(2, max2));

        assert(ct > 0);
        mp = malloc(sizeof(mpool) + (ct-1)*sizeof(void *));
        pools = malloc(palen*sizeof(void **));
        sizes = malloc(palen*sizeof(int));
        if (mp == NULL || pools == NULL || sizes == NULL) err(1, "malloc");
        mp->ct = ct;
        mp->ps = pools;
        mp->pal = palen;
        mp->pg_sz = pgsz;
        mp->sizes = sizes;
        mp->min_pool = pow(2, min2);
        mp->max_pool = pow(2, max2);

        for (i=0; i<ct; i++) {
                sz = (1 << (i + min2));
                p = new_pool(sz, pgsz);
                if (DBG) fprintf(stderr, "%d: got %p (%d bytes per, %ld slots)\n",
                    i, p, sz, pgsz / sz);
                mp->ps[i] = p;
                mp->hs[i] = &p[0];
                assert(mp->hs[i]);
                mp->sizes[i] = sz;
        }
        
        return mp;
}

/* Free a memory pool set. */
void mpool_free(mpool *mp) {
        int i, sz, pgsz = mp->pg_sz;
        assert(mp);
        if (DBG) fprintf(stderr, "%d/%d pools, freeing...\n", mp->ct, mp->pal);
        for (i=0; i<mp->ct; i++) {
                sz = mp->sizes[i];
                if (sz > 0) {   /* 0'd pools are already-munmap'd large pools */
                        sz = sz >= pgsz ? sz : pgsz;
                        if (DBG) fprintf(stderr, "Freeing pool %d, sz %d (%p)\n", i, sz, mp->ps[i]);
                        if (munmap(mp->ps[i], sz) == -1) err(1, "munmap(1)");
                } else {
                        if (DBG) fprintf(stderr, "Skipping already freed large pool %d\n", i);
                }
        }
        free(mp->ps);
        free(mp);
}

/* Allocate memory out of the relevant memory pool.
 * If larger than max_pool, just mmap it. If pool is full, mmap a new one and
 * link it to the end of the current one. */
void *mpool_alloc(mpool *mp, int sz) {
        void **cur, **np;    /* new pool */
        int i, p, szceil;
        assert(mp);
        if (sz >= mp->max_pool) {
                if (DBG) fprintf(stderr, "(mmaping directly, %d bytes) ", sz);
                cur = get_mmap(sz); /* just mmap it */
                if (LG_POOL_AUTO) add_pool(mp, cur, sz);
                return cur;
        }
        
        for (i=0, p=mp->min_pool; ; i++, p*=2) {
                if (p > sz) { szceil = p; break; }
        }

        cur = mp->hs[i];        /* get current head */
        assert(cur);
        if (*cur == NULL) {     /* if at end, attach to a new page */
                if (DBG) fprintf(stderr, "Adding a new pool w/ cell size %d\n", szceil);
                np = new_pool(szceil, mp->pg_sz);
                *cur = &np[0];
                assert(*cur);
                if (DBG) fprintf(stderr, "-- Set cur to %p\n", *cur);
                add_pool(mp, np, szceil);
        }

        assert(*cur);
        mp->hs[i] = *cur;       /* set head to next head */
        return cur;
}

/* Free an individual pointer, pointing the head for that size to it, and setting
 * it to the previous head.
 * if sz is > the max pool size, just munmap it.
 * 
 * If sz == -1, determine size by linear scan over memory regions, starting from
 * most recently allocated pages. This is best avoided when possible.
 */
void mpool_repool(mpool *mp, void *p, int sz) {
        int i=0, szceil, e, poolsz, max_pool = mp->max_pool, pgsz = mp->pg_sz;
        void **ip;

        /* If size is unknown, look up the pool size and index by location.
         * When large pools are not immediately munmapped, get the index to 0 out the size. */
        if (sz == -1 || (LG_POOL_AUTO && sz > pgsz)) {
                for (i=mp->ct-1; i>=0; i--) {
                        if (!LG_POOL_AUTO) {
                                poolsz = pgsz;
                        } else {
                                poolsz = mp->sizes[i];
                                if (poolsz <= max_pool) poolsz = pgsz;
                        }
                        if (DBG) fprintf(stderr, "Checking pool %d (%p ~ %p) for %p\n",
                            i, mp->ps[i], mp->ps[i] + poolsz, p);
                        if (p >= mp->ps[i] && p < mp->ps[i] + poolsz) {
                                sz = mp->sizes[i];
                                if (DBG) fprintf(stderr, "Found it in pool %d, sz=%d\n", i, sz);
                                break;
                        }
                }
                assert(sz > 0);
        }

        if (sz > mp->max_pool) {
                if (DBG) printf("Unpooled large, sz:%d, p:%p\n", sz, p);
                /* if skip it during mpool_free, already freed */
                if (LG_POOL_AUTO) {
                        if (munmap(p, sz) == -1) err(1, "munmap(2)");
                        mp->sizes[i] = 0;
                }
                return;
        }

        for (i=0, e=mp->min_pool; e<=mp->max_pool; i++, e*=2) {
                if (e > sz) { szceil = e; break; }
        }

        ip = (void **)p;
        *ip = mp->hs[i];
        assert(ip);
        mp->hs[i] = ip;
        if (DBG) fprintf(stderr, "repool: %d, %d bytes: %p\n", i, e, ip);
}
