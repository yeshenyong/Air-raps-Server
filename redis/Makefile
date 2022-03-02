
LIBDIR= -L/usr/local/lib
LIBSO = -lhiredis
CFLAG = -Wall -g
 
all:redis
 
redis:redis.o
	gcc ${CFLAG} -o $@ $< ${LIBDIR} ${LIBSO}
%.o%.c:
	gcc -c -o $@ $^
 
clear:
	rm -f *.o

