COPTS=-lm -Os -g
# COPTS+= -DNDEBUG
# COPTS+= -p

all: test

mpool.o: mpool.c
mpool.c: mpool.h

test: test.c mpool.o
	${CC} -o $@ test.c mpool.o ${COPTS}

clean:
	rm -f *.o test *.core gmon.out
