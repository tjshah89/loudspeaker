CC=gcc
CFLAGS=-I -lpulse -lpulse-simple

all: parec-simple pacat-simple patest lsclient lsserver

lsclient: lsclient.c
	$(CC) -o lsclient lsclient.c $(CFLAGS)

lsserver: lsserver.c
	$(CC) -o lsserver lsserver.c $(CFLAGS)

patest: patest.c
	$(CC) -o patest patest.c $(CFLAGS)

parec-simple: parec-simple.c
	$(CC) -o parec-simple parec-simple.c $(CFLAGS)

pacat-simple: pacat-simple.c
	$(CC) -o pacat-simple pacat-simple.c $(CFLAGS)

clean:
	rm -f *.o *~ core parec-simple pacat-simple patest lsserver lsclient