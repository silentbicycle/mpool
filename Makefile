COPTS=	-O2 -Wall
# COPTS+= -g
# COPTS+= -DNDEBUG
# COPTS+= -p

all: libmpool.a

mpool.o: mpool.c Makefile
	${CC} -c mpool.c ${COPTS}

libmpool.a: mpool.o
	${AR} rl $@ mpool.o

mpool.c: mpool.h

test: test.c libmpool.a
	${CC} -o $@ test.c -L. -lmpool ${COPTS}
	./test

clean:
	rm -f *.o *.a test *.core gmon.out
