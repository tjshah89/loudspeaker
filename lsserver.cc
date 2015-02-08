#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <thread>
#include <iostream>

#include <pulse/simple.h>
#include "socket.hh"
#include "util.hh"


#define BUFSIZE 256


int main(int argc, char* argv[]) {

    if (argc < 3) 
	printf("Usage: ./lsserver <localport> <raw audio file>");

    
    printf("Opening audio file...\n");
    FILE* fd; 
    fd = fopen(argv[2], "rb");

    printf("Creating listening socket...\n");
    TCPSocket listening_socket;
    listening_socket.bind( Address( "::0", argv[ 1 ] ) );

    listening_socket.listen();


    /* Wait for clients to connect */
    while ( true ) {
	printf("Waiting for clients...\n");
    
	/* This line does a lot. It waits for a client to connect
	   ("listening_socket.accept()"). When that returns a new socket,
	   it starts a thread to handle that client and passes in the
	   result of accept() as the "client" parameter to the handler. */
	
	std::thread client_handler( [fd] ( TCPSocket client ) {
		int byte = 0;
		
		for (;;) {
		    printf("Sending audio byte %d...\n", byte);

		    char buf[BUFSIZE];
		    pa_usec_t latency;
		    ssize_t r;
		    
		    r = fread((uint8_t*)buf, sizeof(buf[0]), BUFSIZE, fd);
		    if (r == 0) 
			break;
		    
		    client.write(buf);
		    byte += BUFSIZE;
		}
		printf("Closing connection...\n");

		client.write("Close\n");
	    }, listening_socket.accept() );

	/* Let the client handler continue to run without having
	   to keep track of it. The main thread can go back to accepting
	   new incoming connections. */
	printf("Waiting for clients...\n");
	
	client_handler.detach();
    }

    fclose(fd);
    return EXIT_SUCCESS;
}
     
     
