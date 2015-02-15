CC=g++
CFLAGS= -std=c++11 -I sourdough/src/ -g 
LIBS = -lpulse -lpulse-simple -lpthread -L sourdough/src/
SOURDOUGH =  sourdough/src/libsourdough.a

all: parec-simple pacat-simple patest lsclient lsserver 

lsclient: lsclient.cc
	$(CC) -o lsclient lsclient.cc $(CFLAGS) $(LIBS) $(SOURDOUGH)

lsserver: lsserver.cc
	$(CC) -o lsserver lsserver.cc $(CFLAGS) $(LIBS) $(SOURDOUGH)

patest: patest.cc
	$(CC) -o patest patest.cc $(CFLAGS) $(LIBS)

parec-simple: parec-simple.cc
	$(CC) -o parec-simple parec-simple.cc $(CFLAGS) $(LIBS)

pacat-simple: pacat-simple.cc
	$(CC) -o pacat-simple pacat-simple.cc $(CFLAGS) $(LIBS)

clean:
	rm -f *.o *~ core parec-simple pacat-simple patest lsserver lsclient
