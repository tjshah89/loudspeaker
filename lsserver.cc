#include <stdio.h>
#include <stdlib.h> 
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <iostream>
#include <thread>
#include <vector>

#include <pulse/simple.h>
#include <pulse/pulseaudio.h>

#include "socket.hh"
#include "util.hh"
#include "poller.hh"

#include "loudspeaker.hh"

using namespace std;
using namespace PollerShortNames;

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

    vector<Address> clients;
    Poller poller;
    poller.add_action(Action(listening_socket, Direction::In, [&] () {
		printf("Incoming packet!\n");
		pair<Address, string> incoming_packet = listening_socket.recvfrom();
		Address client_address = incoming_packet.first;
		string data = incoming_packet.second;

                /* exit if the server closes the connection */
		if ( data == "Connect Request" ) {
		    printf("Got connection from: %s:%d\n", client_address.ip().c_str(), client_address.port());
		    clients.push_back(client_address);
		    return ResultType::Continue;
                } 
	    })
	);

    /* Wait for clients to connect */
    while ( true ) {
	int byte = 0, i=0;
	for (;;) {
	    if ( i%256 == 0)
		const auto retrn = poller.poll( 1 );
	    if (DEBUG)
		printf("Sending audio byte %d...\n", byte);
	    
	    //int jitter = (rand() % 2902) - 1451;
	    int sleeptime = 1451;// + jitter;
	    
	    usleep(sleeptime);
	    char buf[AUDIO_PACKET_SIZE];
	    pa_usec_t latency;
	    ssize_t r;
	    
	    r = fread((char*)buf, sizeof(char), AUDIO_PACKET_SIZE, fd);
	    if (r == 0) 
		break;
	    
	    for ( vector<Address>::iterator client = clients.begin(); client != clients.end(); ++client) {
		printf("Sending to: %s:%d\n", (*client).ip().c_str(), (*client).port());
		listening_socket.sendto(*client, string(buf, AUDIO_PACKET_SIZE));
	    }
	    byte += AUDIO_PACKET_SIZE;
	    i++;
	}
	printf("Closing connection...\n");
	for ( vector<Address>::iterator client = clients.begin(); client != clients.end(); ++client) {
	    printf("Sending to: %s:%d\n", (*client).ip().c_str(), (*client).port());
	    listening_socket.sendto(*client, "EOF");
	}
    }

    fclose(fd);
    return EXIT_SUCCESS;
}
     
     
