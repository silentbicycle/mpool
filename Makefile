COPTS=-Os -g -Wall
# COPTS+= -DNDEBUG
# COPTS+= -p

all: test

mpool.o: mpool.c
	${CC} -c mpool.c ${COPTS}

mpool.c: mpool.h

test: test.c mpool.o mpool.h
	${CC} -o $@ test.c mpool.o ${COPTS} -lm

clean:
	rm -f *.o test *.core gmon.out
