#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <thread>
#include <iostream>

#include <pulse/simple.h>
#include "socket.hh"
#include "util.hh"
#include "loudspeaker.hh"


using namespace std;
static int DEBUG = 0;


int main(int argc, char* argv[]) {

    if (argc < 3) {
	printf("Usage: ./lsserver <localport> <raw audio file> DEBUG(0,1)\n");
	return 0;
    }

    if (argc > 3) {
        DEBUG = atoi(argv[3]);
    }

    int ret = 1;
    int error;

    srand(time(NULL));

    printf("Opening audio file...\n");
    FILE* fd; 
    fd = fopen(argv[2], "rb");

    printf("Creating listening socket...\n");
    UDPSocket listening_socket;
    listening_socket.bind( Address( "::0", argv[ 1 ] ) );

    /* Wait for clients to connect */
    while ( true ) {
	printf("Waiting for clients...\n");
	pair<Address, string> incoming_packet = listening_socket.recvfrom();
	Address client_address = incoming_packet.first;
	string data = incoming_packet.second;
	
	if ( data == "Connect Request" ) {
	    printf("Got connection from: %s:%d\n", client_address.ip().c_str(), client_address.port());
	    int byte = 0;
	
	    for (;;) {
		if (DEBUG)
		    printf("Sending audio byte %d...\n", byte);
		
		int jitter = (rand() % 2902) - 1451;
		int sleeptime = 1451 + jitter;
		
		usleep(sleeptime);
		char buf[AUDIO_PACKET_SIZE];
		pa_usec_t latency;
		ssize_t r;
		
		r = fread((char*)buf, sizeof(char), AUDIO_PACKET_SIZE, fd);
		if (r == 0) 
		    break;
		
		//pa_simple_write(s, buf, (size_t) r, &error);
		listening_socket.sendto(client_address, string(buf, AUDIO_PACKET_SIZE));
		byte += AUDIO_PACKET_SIZE;
	    }
	    printf("Closing connection...\n");
	    listening_socket.sendto(client_address, "EOF");
	    
	}
    }

    fclose(fd);
    return EXIT_SUCCESS;
}
     
     
