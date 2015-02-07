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

    if (argc > 1) {
	int fd; 
	fd = open(argv[1], O_RDONLY);
	dup2(fd, STDIN_FILENO);
	close(fd);
    }	

    TCPSocket listening_socket;
    listening_socket.bind( Address( "::0", argv[ 1 ] ) );

    listening_socket.listen();

    /* Wait for clients to connect */
    while ( true ) {

	/* This line does a lot. It waits for a client to connect
	   ("listening_socket.accept()"). When that returns a new socket,
	   it starts a thread to handle that client and passes in the
	   result of accept() as the "client" parameter to the handler. */

	std::thread client_handler( [] ( TCPSocket client ) {

		for (;;) {
		    uint8_t buf[BUFSIZE];
		    pa_usec_t latency;
		    ssize_t r;
		    
		    r = read(STDIN_FILENO, buf, sizeof(buf));
		    if (r == 0) 
			break;
		    
		    client.write("hello\n");
		}
		client.write("Close\n");
	    }, listening_socket.accept() );

	/* Let the client handler continue to run without having
	   to keep track of it. The main thread can go back to accepting
	   new incoming connections. */
	
	client_handler.detach();
    }

    return EXIT_SUCCESS;
}
     
     
