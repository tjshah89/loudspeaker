#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <thread>
#include <iostream>

#include <pulse/simple.h>
#include "socket.hh"
#include "util.hh"


#define BUFSIZE 256
using namespace std;


int main(int argc, char* argv[]) {

    if (argc < 3) 
	printf("Usage: ./lsserver <localport> <raw audio file>");

    
    printf("Opening audio file...\n");
    FILE* fd; 
    fd = fopen(argv[2], "rb");

    printf("Creating listening socket...\n");
    UDPSocket listening_socket;
    listening_socket.bind( Address( "::0", argv[ 1 ] ) );


    /* Wait for clients to connect */
    while ( true ) {
	printf("Waiting for clients...\n");
	/* This line does a lot. It waits for a client to connect
	   ("listening_socket.accept()"). When that returns a new socket,
	   it starts a thread to handle that client and passes in the
	   result of accept() as the "client" parameter to the handler. */
	
	pair<Address, string> p = listening_socket.recvfrom();

	printf("Got connection:%s\n", p.second.c_str());
	int byte = 0;
	
	for (;;) {
	    printf("Sending audio byte %d...\n", byte);
	    
	    char buf[BUFSIZE];
	    pa_usec_t latency;
	    ssize_t r;
	    
	    r = fread((uint8_t*)buf, sizeof(buf[0]), BUFSIZE, fd);
	    if (r == 0) 
		break;
	    
	    listening_socket.sendto(p.first,buf);
	    byte += 1;
	}
	printf("Closing connection...\n");
	listening_socket.sendto(p.first, "eof");

    }

    fclose(fd);
    return EXIT_SUCCESS;
}
     
     
