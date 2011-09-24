COPTS=	-O3 -Wall
# COPTS+= -g
# COPTS+= -DNDEBUG
# COPTS+= -p

all: mpool.a

mpool.o: mpool.c Makefile
	${CC} -c mpool.c ${COPTS}

mpool.a: mpool.o
	${AR} rl $@ mpool.o

mpool.c: mpool.h

test: test.c mpool.a
	${CC} -o $@ test.c mpool.a ${COPTS}
	./test

clean:
	rm -f *.o *.a test *.core gmon.out
