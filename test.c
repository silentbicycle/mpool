/* Randomly test / stress the allocator */

#include <stdlib.h>
#include <stdio.h>

#include "mpool.h"

int main() {
        int i, sz, res, max_pool_sz;
        int *ip;

        /* Init a new mpool for values 2^4 to 2^11 */
        mpool *mp = mpool_init(4, 11);
        max_pool_sz = mp->max_pool;
        srandomdev();

        for (i=0; i<5000000; i++) {
                sz = random() % 64;
                /* also alloc some larger chunks  */
                if (random() % 100 == 0) sz = random() % 10000;
                ip = (int *)mpool_alloc(mp, sz);
                *ip = 7;

                /* randomly repool some of them */
                if (random() % 10 == 0) { /* repool, known size */
                        mpool_repool(mp, ip, sz);
                } else if (random() % 25 == 0) { /* repool, look up size */
                        /* don't use unknown size w/ manual large pools */
                        if (LG_POOL_AUTO || sz < max_pool_sz)
                                mpool_repool(mp, ip, -1);
                }
                if (i % 10000 == 0 && i > 0) {
                        putchar('.');
                        if (i % 700000 == 0) putchar('\n');
                }
        }

        mpool_free(mp);
        putchar('\n');
        return 0;
}
