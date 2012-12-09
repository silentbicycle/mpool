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
 * regions. When a pointer is repooled, it is linked back into the
 * pool with the given size's free list.
 *
 * Note that repooling with the wrong size leads to subtle/ugly memory
 * clobbering bugs. Turning on memory use logging via MPOOL_DEBUG
 * can help pin down the location of most such errors.
 * 
 * Allocations larger than the page size are allocated whole via
 * mmap, and those larger than mp->max_pool (configurable) are
 * freed immediately via munmap; no free list is used.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <string.h>

#include "mpool.h"

#define DBG MPOOL_DEBUG

static void *get_mmap(long sz) {
        void *p = mmap(0, sz, PROT_READ|PROT_WRITE,
            MAP_PRIVATE|MAP_ANON, -1, 0);
        if (p == MAP_FAILED) return NULL;
        return p;
}

/* Optimized base-2 integer ceiling, from _Hacker's Delight_
 * by Henry S. Warren, pg. 48. Called 'clp2' there. */
static unsigned int iceil2(unsigned int x) {
        x = x - 1;
        x = x | (x >> 1);
        x = x | (x >> 2);
        x = x | (x >> 4);
        x = x | (x >> 8);
        x = x | (x >> 16);
        return x + 1;
}

/* mmap a new memory pool of TOTAL_SZ bytes, then build an internal
 * freelist of SZ-byte cells, with the head at (result)[0].
 * Returns NULL on error. */
void **mpool_new_pool(unsigned int sz, unsigned int total_sz) {
        void *p = get_mmap(sz > total_sz ? sz : total_sz);
        int i, o=0, lim;               /* o=offset */
        int **pool;
        void *last = NULL;
        if (p == NULL) return NULL;
        pool = (int **)p;;
        assert(pool);
        assert(sz > sizeof(void *));

        lim = (total_sz/sz);
        if (DBG) fprintf(stderr,
            "mpool_new_pool sz: %d lim: %d => %d %p\n",
            sz, lim, lim * sz, p);
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
static int add_pool(mpool *mp, void *p, int sz) {
        void **nps, *nsizes;       /* new pools, new sizes */
        assert(p);
        assert(sz > 0);
        if (DBG) fprintf(stderr, "mpool_add_pool (%d / %d) @ %p, sz %d\n",
            mp->ct, mp->pal, p, sz);
        if (mp->ct == mp->pal) {
                mp->pal *= 2;   /* ram will exhaust before overflow... */
                nps = MPOOL_REALLOC(mp->ps, mp->pal * sizeof(void **));
                nsizes = MPOOL_REALLOC(mp->sizes, mp->pal * sizeof(int *));
                if (nps == NULL || nsizes == NULL) return -1;
                mp->sizes = nsizes;
                mp->ps = nps;
        }

        mp->ps[mp->ct] = p;
        mp->sizes[mp->ct] = sz;
        mp->ct++;
        return 0;
}

/* Initialize a memory pool set, with pools in sizes
 * 2^min2 to 2^max2. Returns NULL on error. */
mpool *mpool_init(int min2, int max2) {
        int palen;                     /* length of pool array */
        int ct = ct = max2 - min2 + 1; /* pool array count */
        long pgsz = sysconf(_SC_PAGESIZE);
        mpool *mp;
        void *pools;
        int *sizes;

        palen = iceil2(ct);
        if (DBG) fprintf(stderr, "mpool_init for cells %d - %d bytes\n",
            1 << min2, 1 << max2);

        assert(ct > 0);
        mp = MPOOL_MALLOC(sizeof(mpool) + (ct-1)*sizeof(void *));
        pools = MPOOL_MALLOC(palen*sizeof(void **));
        sizes = MPOOL_MALLOC(palen*sizeof(int));
        if (mp == NULL || pools == NULL || sizes == NULL) return NULL;
        mp->ct = ct;
        mp->ps = pools;
        mp->pal = palen;
        mp->pg_sz = pgsz;
        mp->sizes = sizes;
        mp->min_pool = 1 << min2;
        mp->max_pool = 1 << max2;
        bzero(sizes, palen * sizeof(int));
        bzero(pools, palen * sizeof(void *));
        bzero(mp->hs, ct * sizeof(void *));

        return mp;
}

/* Free a memory pool set. */
void mpool_free(mpool *mp) {
        long  i, sz, pgsz = mp->pg_sz;
        assert(mp);
        if (DBG) fprintf(stderr, "%d/%d pools, freeing...\n", mp->ct, mp->pal);
        for (i=0; i<mp->ct; i++) {
                void *p = mp->ps[i];
                if (p) {
                        sz = mp->sizes[i];
                        assert(sz > 0);
                        sz = sz >= pgsz ? sz : pgsz;
                        if (DBG) fprintf(stderr, "mpool_free %ld, sz %ld (%p)\n", i, sz, mp->ps[i]);
                        if (munmap(mp->ps[i], sz) == -1) {
                                fprintf(stderr, "munmap error while unmapping %lu bytes at %p\n",
                                    sz, mp->ps[i]);
                        }
                }
        }
        MPOOL_FREE(mp->ps, mp->ct * sizeof(*ps));
        MPOOL_FREE(mp, sizeof(*mp));
}

/* Allocate memory out of the relevant memory pool.
 * If larger than max_pool, just mmap it. If pool is full, mmap a new one and
 * link it to the end of the current one. Returns NULL on error. */
void *mpool_alloc(mpool *mp, int sz) {
        void **cur, **np;    /* new pool */
        int i, p, szceil = 0;
        assert(mp);
        if (sz >= mp->max_pool) {
                cur = get_mmap(sz); /* just mmap it */
                if (cur == NULL) return NULL;
                if (DBG) fprintf(stderr,
                    "mpool_alloc mmap %d bytes @ %p\n", sz, cur);
                return cur;
        }
        
        for (i=0, p=mp->min_pool; ; i++, p*=2) {
                if (p > sz) { szceil = p; break; }
        }
        assert(szceil > 0);
        cur = mp->hs[i];        /* get current head */
        if (cur == NULL) {      /* lazily allocate & init pool */
                void **pool = mpool_new_pool(szceil, mp->pg_sz);
                if (pool == NULL) return NULL;
                mp->ps[i] = pool;
                mp->hs[i] = &pool[0];
                mp->sizes[i] = szceil;
                cur = mp->hs[i];
        }
        assert(cur);

        if (*cur == NULL) {     /* if at end, attach to a new page */
                if (DBG) fprintf(stderr,
                    "mpool_alloc adding pool w/ cell size %d\n", szceil);
                np = mpool_new_pool(szceil, mp->pg_sz);
                if (np == NULL) return NULL;
                *cur = &np[0];
                assert(*cur);
                if (add_pool(mp, np, szceil) < 0) return NULL;
        }

        assert(*cur > (void *)4096);
        if (DBG) fprintf(stderr,
            "mpool_alloc pool %d bytes @ %p (list %d, szceil %d )\n",
            sz, (void*) cur, i, szceil);

        mp->hs[i] = *cur;       /* set head to next head */
        return cur;
}

/* Push an individual pointer P back on the freelist for
 * the pool with size SZ cells.
 * if SZ is > the max pool size, just munmap it. */
void mpool_repool(mpool *mp, void *p, int sz) {
        int i=0, szceil, max_pool = mp->max_pool;
        void **ip;

        if (sz > max_pool) {
                if (DBG) fprintf(stderr, "mpool_repool munmap sz %d @ %p\n", sz, p);
                if (munmap(p, sz) == -1) {
                        fprintf(stderr, "munmap error while unmapping %d bytes at %p\n",
                            sz, p);
                }
                return;
        }

        szceil = iceil2(sz);
        szceil = szceil > mp->min_pool ? szceil : mp->min_pool;

        ip = (void **)p;
        *ip = mp->hs[i];
        assert(ip);
        mp->hs[i] = ip;
        if (DBG) fprintf(stderr,
            "mpool_repool list %d, %d bytes (ceil %d): %p\n",
            i, sz, szceil, ip);
}

/* Reallocate data, growing or shrinking and copying the contents.
 * Returns NULL on reallocation error. */
void *mpool_realloc(mpool *mp, void *p, int old_sz, int new_sz) {
        void *r = mpool_alloc(mp, new_sz);
        if (r == NULL) return NULL;
        memcpy(r, p, old_sz);
        mpool_repool(mp, p, old_sz);
        return r;
}
