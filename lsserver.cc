#include <stdio.h>
#include <stdlib.h> 
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <iostream>
#include <thread>
#include <vector>
#include <time.h>

#include <pulse/error.h>
#include <pulse/simple.h>
#include <pulse/pulseaudio.h>

#include "socket.hh"
#include "util.hh"
#include "poller.hh"

#include "loudspeaker.hh"

#define CLEAR_LINE "\x1B[K"

using namespace std;
using namespace PollerShortNames;

static void *buffer = NULL;
static size_t buffer_length = 0, buffer_index = 0;

static pa_context *context = NULL;
static pa_stream *stream = NULL;
static pa_mainloop_api *mainloop_api = NULL;
static int DEBUG = 0;

static pa_stream_flags_t flags = (pa_stream_flags_t) 0;

struct timeval *tval_start = NULL;
struct timeval *tval_last = NULL;

static void print_time(const string message) {
    struct timeval tval_now, tval_result;

    if (!tval_start) {
	tval_start = (struct timeval*)malloc(sizeof(tval_start));
	gettimeofday(tval_start, NULL);
    }
    gettimeofday(&tval_now, NULL);
    timersub(&tval_now, tval_start, &tval_result);
    printf("At time: %ld.%06ld %s\n", (long int)tval_result.tv_sec, (long int)tval_result.tv_usec, message.c_str());
}

/* A shortcut for terminating the application */
static void quit(int ret) {
    assert(mainloop_api);
    mainloop_api->quit(mainloop_api, ret);
}


/********************************************************************************
  Callback functions triggered by events on the pulseaudio stream
********************************************************************************/

/* This is called whenever new recorded data is available so that we can pass
 it into "buffer" to be later read and sent to the clients. */
static void stream_read_callback(pa_stream *s, size_t length, void *userdata) {
    const void *data;
    assert(s);
    assert(length > 0);

    if (pa_stream_peek(s, &data, &length) < 0) {
        fprintf(stderr, "pa_stream_peek() failed: %s\n", pa_strerror(pa_context_errno(context)));
        quit(1);
        return;
    }

    assert(data);
    assert(length > 0);

    if (buffer) {
        buffer = pa_xrealloc(buffer, buffer_length + length);
        buffer_index = 0;
        memcpy((uint8_t*) buffer + buffer_length, data, length);
        buffer_length += length;
    } else {
        buffer = pa_xmalloc(length);
        memcpy(buffer, data, length);
        buffer_length = length;
        buffer_index = 0;
    }

    pa_stream_drop(s);
}


static void stream_suspended_callback(pa_stream *s, void *userdata) {
    assert(s);
    if (DEBUG) {
        if (pa_stream_is_suspended(s))
            fprintf(stderr, "Stream device suspended.%s \n", CLEAR_LINE);
        else
            fprintf(stderr, "Stream device resumed.%s \n", CLEAR_LINE);
    }
}

static void stream_moved_callback(pa_stream *s, void *userdata) {
    assert(s);
    if (DEBUG)
        fprintf(stderr, "Stream moved to device %s (%u, %ssuspended).%s \n",
            pa_stream_get_device_name(s), pa_stream_get_device_index(s),
            pa_stream_is_suspended(s) ? "" : "not ", 
            CLEAR_LINE);
}

static void stream_underflow_callback(pa_stream *s, void *userdata) {
    assert(s);
    if (DEBUG)
        fprintf(stderr, "Stream underrun.%s \n",  CLEAR_LINE);
}

static void stream_overflow_callback(pa_stream *s, void *userdata) {
    assert(s);
    if (DEBUG)
        fprintf(stderr, "Stream overrun.%s \n", CLEAR_LINE);
}

static void stream_started_callback(pa_stream *s, void *userdata) {
    assert(s);
    if (DEBUG)
        fprintf(stderr, "Stream started.%s \n", CLEAR_LINE);
}

static void stream_buffer_attr_callback(pa_stream *s, void *userdata) {
    assert(s);
    if (DEBUG)
        fprintf(stderr, "Stream buffer attributes changed.%s \n",  CLEAR_LINE);
}


/* This is called whenever the context status changes */
static void context_state_callback(pa_context *c, void *userdata) {
    assert(c);

    switch (pa_context_get_state(c)) {
        case PA_CONTEXT_CONNECTING:
        case PA_CONTEXT_AUTHORIZING:
        case PA_CONTEXT_SETTING_NAME:
            break;

    /* Once the Context is ready, create the recording stream */
        case PA_CONTEXT_READY: {
            int r;
            pa_buffer_attr buffer_attr;

            assert(c);
            assert(!stream);

            if (DEBUG)
                fprintf(stderr, "Connection established.%s \n", CLEAR_LINE);

            // Create the stream.
            stream = pa_stream_new(c, "recording stream" /* stream_name */, &ss, NULL /* channelMap */);
            if (!stream) {
                fprintf(stderr, "pa_stream_new() failed: %s\n", pa_strerror(pa_context_errno(c)));
                quit(1);
            }

            // Set the stream callbacks.
            pa_stream_set_read_callback(stream, stream_read_callback, NULL);
            pa_stream_set_suspended_callback(stream, stream_suspended_callback, NULL);
            pa_stream_set_moved_callback(stream, stream_moved_callback, NULL);
            pa_stream_set_underflow_callback(stream, stream_underflow_callback, NULL);
            pa_stream_set_overflow_callback(stream, stream_overflow_callback, NULL);
            pa_stream_set_started_callback(stream, stream_started_callback, NULL);
            pa_stream_set_buffer_attr_callback(stream, stream_buffer_attr_callback, NULL);

            // Set the record buffer attributes.
            memset(&buffer_attr, 0, sizeof(buffer_attr));
            buffer_attr.tlength = (uint32_t) -1;
            buffer_attr.minreq = (uint32_t) -1;
            buffer_attr.fragsize = (uint32_t) -1;
            buffer_attr.maxlength = (uint32_t) -1;
            buffer_attr.prebuf = (uint32_t) -1; // Playback should never stop in case of buffer underrun (play silence).
            
            r = pa_stream_connect_record(stream, NULL /* device */, &buffer_attr, (pa_stream_flags_t) flags);
            if (r < 0) {
                fprintf(stderr, "pa_stream_connect_playback() failed: %s\n", pa_strerror(pa_context_errno(c)));
                quit(1);
            }

            break;
        }

        case PA_CONTEXT_TERMINATED:
            quit(0);
            break;

        case PA_CONTEXT_FAILED:
        default:
            fprintf(stderr, "Connection failure: %s\n", pa_strerror(pa_context_errno(c)));
            quit(1);
    }

    return;
}


/* Connection draining complete */
static void context_drain_complete(pa_context*c, void *userdata) {
    pa_context_disconnect(c);
}

/* Stream draining complete */
static void stream_drain_complete(pa_stream*s, int success, void *userdata) {

    if (!success) {
        fprintf(stderr, "Failed to drain stream: %s\n", pa_strerror(pa_context_errno(context)));
        quit(1);
    }

    if (DEBUG)
        fprintf(stderr, "Record stream drained.\n");

    pa_stream_disconnect(stream);
    pa_stream_unref(stream);
    stream = NULL;

    if (!pa_context_drain(context, context_drain_complete, NULL))
        pa_context_disconnect(context);
    else {
        if (DEBUG)
            fprintf(stderr, "Draining connection to server.\n");
    }
}


int init_pa_context(pa_mainloop* m){
    int r;
    
    mainloop_api = pa_mainloop_get_api(m);
    r = pa_signal_init(mainloop_api);
    assert(r == 0);

    // Create a new connection context and connect it
    context = pa_context_new(mainloop_api, "loudspeaker_server");
    if (!context) {
        fprintf(stderr, "pa_context_new() failed.\n");
        return 1;
    }

    pa_context_set_state_callback(context, context_state_callback, NULL);
    if (pa_context_connect(context, NULL, (pa_context_flags_t) 0, NULL) < 0) {
        fprintf(stderr, "pa_context_connect() failed: %s\n", pa_strerror(pa_context_errno(context)));
        return 1;
    }
}


/* This is called whenever new recorded data is available so that we can pass
   it into "buffer" to be later read and sent to the clients. */
static void record_from_mic(pa_simple *s, pa_simple *pbStream) {
    print_time("\n");
    print_time("==================================================================");
    print_time("-------------------------Start record_from_mic");
    int error;
    int sampleLength = 256;
    char* buf[sampleLength];
    assert(s);
    print_time("Read from mic");
    if (pa_simple_read(s, buf, sampleLength, &error) < 0) {
	fprintf(stderr, __FILE__": pa_simple_read() failed: %s\n", pa_strerror(error));
    }

    //pa_simple_write(pbStream, buf, (size_t) sampleLength, &error);

    print_time("Write from buf to buffer");
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
    print_time("-------------------------End record_from_mic");
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

    pa_simple_write(pbStream, outBuffer, (size_t) AUDIO_PACKET_SIZE, &error);

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

    /* 
    printf("Creating recording stream...\n");
    if (!(s = pa_simple_new(NULL, argv[0], PA_STREAM_RECORD, NULL, "record", &ss, NULL, NULL, &error))) {
	fprintf(stderr, __FILE__": pa_simple_new() failed: %s\n", pa_strerror(error));
	return ret;
    }*/

    pa_mainloop* m = pa_mainloop_new();    
    if (!m) {
        fprintf(stderr, "pa_mainloop_new() failed.\n");
        return 1;
    }

    int init_pa = init_pa_context(m);
    if (init_pa){
        quit(1);
    }

    pbStream = pa_simple_new(NULL, argv[0], PA_STREAM_PLAYBACK, NULL, "playback", &ss, NULL, NULL, &error);


    /*
      printf("Opening audio file...\n");
      FILE* fd; 
      fd = fopen(argv[2], "rb");
    */

    //print_time("Creating listening socket...");
    UDPSocket listening_socket;
    listening_socket.bind( Address( "::0", argv[ 1 ] ) );

    vector<Address> clients;
    Poller poller;
    poller.add_action(Action(listening_socket, Direction::In, [&] () {
		print_time("Incoming packet!");
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
	    //print_time(".........................Starting for loop");
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

        /* Run the recording stream main loop */
        if (pa_mainloop_iterate(m, 0 /* no blocking */, &ret) < 0) {
            fprintf(stderr, "pa_mainloop_run() failed.\n");
            break;
        }
	    
	    //print_time("Recording from Mic");
	    /* Record some data ... */
	    //record_from_mic(s, pbStream);
		
	    //print_time("Writing to buf");
	    /* Stage some data to be sent */
	    read_from_recording_buffer(buf, pbStream);

	    //print_time("Writing audio");
	    //pa_simple_write(pbStream, buf, (size_t) AUDIO_PACKET_SIZE, &error); 
	    
	    /*
	      r = fread((char*)buf, sizeof(char), AUDIO_PACKET_SIZE, fd);
	      if (r == 0) 
	      break;
	    */
		    
	    //print_time("Sending audio");
	    for (vector<Address>::iterator client = clients.begin(); client != clients.end(); ++client) {
		printf("Sending to: %s:%d\n", (*client).ip().c_str(), (*client).port());
		listening_socket.sendto(*client, string(buf, AUDIO_PACKET_SIZE));
	    }
	    byte += AUDIO_PACKET_SIZE;
	    i++;
	    //print_time("-------------------------Ending for loop");
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
     
