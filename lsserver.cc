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

    if (argc < 3) {
	printf("Usage: ./lsserver <localport> <raw audio file>\n");
	return 0;
    }

    static const pa_sample_spec ss = {
	.format = PA_SAMPLE_S16LE,
	.rate = 44100, 
	.channels = 2
    };

    pa_simple *s = NULL;
    int ret = 1;
    int error;

    srand(time(NULL));

    printf("Opening audio file...\n");
    FILE* fd; 
    fd = fopen(argv[2], "rb");

    printf("Creating listening socket...\n");
    UDPSocket listening_socket;
    listening_socket.bind( Address( "::0", argv[ 1 ] ) );

    s = pa_simple_new(NULL, argv[0], PA_STREAM_PLAYBACK, NULL, "playback", &ss, NULL, NULL, &error);

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
	    
	    int jitter = (rand() % 2902) - 1451;
	    int sleeptime = 1451 + jitter;

	    usleep(sleeptime);
	    char buf[BUFSIZE];
	    pa_usec_t latency;
	    ssize_t r;
	    
	    r = fread((char*)buf, sizeof(char), BUFSIZE, fd);
	    if (r == 0) 
		break;
	    
	    pa_simple_write(s, buf, (size_t) r, &error);
	    listening_socket.sendto(p.first, string(buf, BUFSIZE));
	    byte += BUFSIZE;
	}
	printf("Closing connection...\n");
	listening_socket.sendto(p.first, "eof");

    }

    pa_simple_drain(s, &error);
    ret = 0;

    if (s)
	pa_simple_free(s);
    
    fclose(fd);
    return EXIT_SUCCESS;
}
     
     
