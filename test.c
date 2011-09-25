/* Randomly test / stress the allocator */

#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>

#include "mpool.h"

#define PMAX 11

int main(int argc, char **argv) {
        int i, sz, max_pool_sz;
        long seed;
        int *ip;

        /* Init a new mpool for values 2^4 to 2^PMAX */
        mpool *mp = mpool_init(4, PMAX);
        max_pool_sz = mp->max_pool;
        if (argc > 1) {
                seed = atol(argv[1]);
        } else {
                struct timeval tv;
                if (gettimeofday(&tv, NULL) < 0) {
                        fprintf(stderr, "gettimeofday fail\n");
                        return 1;
                }
                seed = tv.tv_usec;
        }
        srandom(seed);
        printf("seed is %ld\n", seed);

        for (i=0; i<5000000; i++) {
                sz = random() % 64;
                /* also alloc some larger chunks  */
                if (random() % 100 == 0) sz = random() % 10000;
                sz = sz ? sz : 1; /* no 0 allocs */
                ip = (int *)mpool_alloc(mp, sz);
                *ip = 7;

                /* randomly repool some of them */
                if (random() % 10 == 0) { /* repool, known size */
                        mpool_repool(mp, ip, sz);
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
