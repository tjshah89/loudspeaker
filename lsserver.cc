#include <stdio.h>
#include <stdlib.h> 
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <iostream>
#include <thread>
#include <vector>

#include <pulse/error.h>
#include <pulse/simple.h>
#include <pulse/pulseaudio.h>

#include "socket.hh"
#include "util.hh"
#include "poller.hh"

#include "loudspeaker.hh"

using namespace std;
using namespace PollerShortNames;

static void *buffer = NULL;
static size_t buffer_length = 0, buffer_index = 0;

static int DEBUG = 0;


/* This is called whenever new recorded data is available so that we can pass
 it into "buffer" to be later read and sent to the clients. */
static void recordFromMic(pa_simple *s, pa_simple *pbStream) {
    int error;
    int sampleLength = 2048;
    char* buf[sampleLength];
    assert(s);

    if (pa_simple_read(s, buf, sampleLength, &error) < 0) {
	fprintf(stderr, __FILE__": pa_simple_read() failed: %s\n", pa_strerror(error));
    }

    //pa_simple_write(pbStream, buf, (size_t) sampleLength, &error);

    if (buffer) {
        buffer = pa_xrealloc(buffer, buffer_length + sampleLength);
	buffer_index = 0;
        memcpy((uint8_t*) buffer + buffer_length, buf, sampleLength);
        buffer_length += sampleLength;
    } else {
        buffer = pa_xmalloc(sampleLength);
        memcpy(buffer, buf, sampleLength);
        buffer_length = sampleLength;
        buffer_index = 0;
    }
}


static void read_from_recording_buffer(char* outBuffer, pa_simple *pbStream) {
    int error;
    if (!buffer)
        return;

    if (!buffer || !buffer_length)
        return;

    if (AUDIO_PACKET_SIZE > buffer_length)
    	return;

    memcpy(outBuffer, (uint8_t *)buffer + buffer_index, AUDIO_PACKET_SIZE); 

    //pa_simple_write(pbStream, outBuffer, (size_t) AUDIO_PACKET_SIZE, &error);

    buffer_length -= AUDIO_PACKET_SIZE; 
    buffer_index += AUDIO_PACKET_SIZE;

    if (!buffer_length) {
        pa_xfree(buffer);
        buffer = NULL;
        buffer_index = buffer_length = 0;
    }
}


int main(int argc, char* argv[]) {

    if (argc < 3) {
		printf("Usage: ./lsserver <localport> <raw audio file> DEBUG(0,1)\n");
		return 0;
    }
    if (argc > 3) {
        DEBUG = atoi(argv[3]);
    }
    srand(time(NULL));
    pa_simple *s = NULL;
    pa_simple *pbStream = NULL;
    int ret = 1;
    int error;

    /* Create the recording stream */
    printf("Creating recording stream...\n");
	if (!(s = pa_simple_new(NULL, argv[0], PA_STREAM_RECORD, NULL, "record", &ss, NULL, NULL, &error))) {
		fprintf(stderr, __FILE__": pa_simple_new() failed: %s\n", pa_strerror(error));
		return ret;
	}

    pbStream = pa_simple_new(NULL, argv[0], PA_STREAM_PLAYBACK, NULL, "playback", &ss, NULL, NULL, &error);


    /*
    printf("Opening audio file...\n");
    FILE* fd; 
    fd = fopen(argv[2], "rb");
    */

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
		    
		    //usleep(sleeptime);
		    char buf[AUDIO_PACKET_SIZE];
		    pa_usec_t latency;
		    ssize_t r;

		    /* Record some data ... */
		    recordFromMic(s, pbStream);
		
		    /* Stage some data to be sent */
		    read_from_recording_buffer(buf, pbStream);

		    //pa_simple_write(pbStream, buf, (size_t) AUDIO_PACKET_SIZE, &error); 
		    
		    /*
		    r = fread((char*)buf, sizeof(char), AUDIO_PACKET_SIZE, fd);
		    if (r == 0) 
			break;
			*/
		    
		    for (vector<Address>::iterator client = clients.begin(); client != clients.end(); ++client) {
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

		if (s)
			pa_simple_free(s);
 	}

    //fclose(fd);
    return EXIT_SUCCESS;
}
     
