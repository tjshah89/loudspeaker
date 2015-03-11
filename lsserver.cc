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

#include "sourdough/src/socket.hh"
#include "sourdough/src/util.hh"
#include "sourdough/src/poller.hh"

#include "loudspeaker.hh"

#define CLEAR_LINE "\x1B[K"

using namespace std;
using namespace PollerShortNames;

static void *buffer = NULL;
static size_t data_start = 0, data_end = 0;

static pa_context *context = NULL;
static pa_stream *stream = NULL;
static pa_mainloop_api *mainloop_api = NULL;
static int DEBUG = 0;

static pa_stream_flags_t flags = (pa_stream_flags_t) 0;

static int frag_size = 256;

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
    if(buffer)
	free(buffer);
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
    assert(buffer);

    //this is dropping some audio data, may want to revisit this.
    if ( data_end < data_start ) {
	if ( length > data_start - data_end ) 
	    length = data_start - data_end;

    }	    
    else if ( length > BUFFER_LENGTH - data_end ) {
	length = BUFFER_LENGTH - data_end;
    }

    memcpy((uint8_t*) buffer + data_end, data, length);
    data_end += length;

    if ( data_end == BUFFER_LENGTH ) {
	data_end = 0;
    }
    if (DEBUG)
        print_time("got audio from server\n");
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
            buffer_attr.fragsize = (uint32_t) frag_size;
            buffer_attr.maxlength = (uint32_t) -1;
            buffer_attr.prebuf = (uint32_t) -1; // Playback should never stop in case of buffer underrun (play silence).

	    flags = (pa_stream_flags_t)PA_STREAM_ADJUST_LATENCY;

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

static int read_from_recording_buffer(char* outBuffer, pa_simple *pbStream) {
    int error;
    if (!buffer)
        return 0;

    if ( data_end < data_start ) {
	if ( BUFFER_LENGTH - data_start < AUDIO_PACKET_SIZE ) {
	    data_start = 0;
	    if ( data_end < AUDIO_PACKET_SIZE ) {
		return 0;
	    }
	}
    }
    else if ( data_end - data_start < AUDIO_PACKET_SIZE )
    	return 0;

    memcpy(outBuffer, (uint8_t *)buffer + data_start, AUDIO_PACKET_SIZE); 
    
    //pa_simple_write(pbStream, outBuffer, (size_t) AUDIO_PACKET_SIZE, &error);

    data_start += AUDIO_PACKET_SIZE;
    return AUDIO_PACKET_SIZE;
}

int main(int argc, char* argv[]) {
    signal(SIGINT, quit);

    if (argc < 2) {
	fprintf(stderr, "Usage: %s HOST PORT -d:debug -p:packet_size -f:frag_size\n", argv[0]);
	return 0;
    }
    string port { argv[ 1 ] };

    int c;
    while ((c = getopt(argc, argv, "dp:f:")) != -1) {
	switch (c) {
	case 'd':
	    DEBUG = 1;
	    break;
	case 'p':
	    AUDIO_PACKET_SIZE = atoi(optarg);
	    break;
	case 'f':
	    frag_size = atoi(optarg);
	    break;
	case '?':
	    fprintf(stderr, "Usage: %s PORT -d:debug -p:packet_size -f:frag_size\n", argv[0]);
	    return 1;
	default:
	    abort();
	}
	
    }

    data_start = 0;
    data_end = 0;
    buffer = malloc(BUFFER_LENGTH);

    srand(time(NULL));
    pa_simple *s = NULL;
    pa_simple *pbStream = NULL;
    int ret = 1;
    int error;

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

    //print_time("Creating listening socket...");
    UDPSocket listening_socket;
    listening_socket.bind( Address( "::0", port ) );

    vector<Address> clients;
    Poller poller;
    poller.add_action(Action(listening_socket, Direction::In, [&] () {
		print_time("Incoming packet!");
		pair<Address, string> incoming_packet = listening_socket.recvfrom();
		Address client_address = incoming_packet.first;
		string data = incoming_packet.second;

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
	
	    char buf[AUDIO_PACKET_SIZE];
	    pa_usec_t latency;
	    ssize_t r;

	    /* Run the recording stream main loop */
	    if (pa_mainloop_iterate(m, 0 /* no blocking */, &ret) < 0) {
		fprintf(stderr, "pa_mainloop_run() failed.\n");
		break;
	    }
	    
	    /* Stage some data to be sent */
	    if ( read_from_recording_buffer(buf, pbStream)) {
		if (DEBUG)
		    printf("Sending audio byte %d...\n", byte);
		    

		//print_time("Sending audio");
		for (vector<Address>::iterator client = clients.begin(); client != clients.end(); ++client) {
		    if (DEBUG)
			printf("Sending to: %s:%d\n", (*client).ip().c_str(), (*client).port());
		    
		    listening_socket.sendto(*client, string(buf, AUDIO_PACKET_SIZE));
		}
		byte += AUDIO_PACKET_SIZE;
	    }
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
    if(buffer)
	free(buffer);

    return EXIT_SUCCESS;
}
     
