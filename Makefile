CC=g++
CFLAGS= -lpulse -lpulse-simple -pthread -I ./sourdough -std=c++11 -L ./sourdough/src/ -L sourdough/src/libsourdough.a

all: parec-simple pacat-simple patest lsclient lsserver 

lsclient: lsclient.cc
	$(CC) -o lsclient lsclient.cc $(CFLAGS)

lsserver: lsserver.cc
	$(CC) -o lsserver lsserver.cc $(CFLAGS)

patest: patest.cc
	$(CC) -o patest patest.cc $(CFLAGS)

parec-simple: parec-simple.cc
	$(CC) -o parec-simple parec-simple.cc $(CFLAGS)

pacat-simple: pacat-simple.cc
	$(CC) -o pacat-simple pacat-simple.cc $(CFLAGS)

clean:
	rm -f *.o *~ core parec-simple pacat-simple patest lsserver lsclient