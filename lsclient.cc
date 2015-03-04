#include <stdio.h>
#include <stdlib.h> 
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <iostream>
#include <thread>
#include <pulse/simple.h>
#include <pulse/pulseaudio.h>

#include "socket.hh"
#include "util.hh"
#include "poller.hh"

#include "loudspeaker.hh"

#define CLEAR_LINE "\x1B[K"

using namespace std;
using namespace PollerShortNames;


static pa_context *context = NULL;
static pa_stream *stream = NULL;
static pa_mainloop_api *mainloop_api = NULL;
static int DEBUG = 0;

static int flags = 0;

static void *buffer = NULL;
static size_t data_start = 0, data_end = 0;


/* A shortcut for terminating the application */
static void quit(int ret) {
    assert(mainloop_api);
    mainloop_api->quit(mainloop_api, ret);
}


/********************************************************************************
Callback functions triggered by events on the pulseaudio stream
********************************************************************************/
static void stream_write_callback(pa_stream *s, size_t length, void *userdata) {
    assert(s);
    assert(length > 0);

    if (!buffer)
        return;

    if ( data_end < data_start ) {
	length = BUFFER_LENGTH - data_start;
    }
    else if ( data_end - data_start < length) {
	length = data_end - data_start;
    }

    if (pa_stream_write(stream, (uint8_t*) buffer + data_start, length, NULL, 0, PA_SEEK_RELATIVE) < 0) {
        fprintf(stderr, "pa_stream_write() failed: %s\n", pa_strerror(pa_context_errno(context)));
        quit(1);
        return;
    }

    data_start += length;
    if ( data_start == BUFFER_LENGTH ) {
	if (DEBUG)
	    printf("wrapping start, value was %d\n", (int) data_start);
	data_start = 0;
    }
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

        /* Once the Context is ready, create the playback stream */
    case PA_CONTEXT_READY: {
        int r;
        pa_buffer_attr buffer_attr;

        assert(c);
        assert(!stream);

        if (DEBUG)
            fprintf(stderr, "Connection established.%s \n", CLEAR_LINE);

        // Create the stream.
        stream = pa_stream_new(c, "name" /* stream_name */, &ss, NULL /* channelMap */);
        if (!stream) {
            fprintf(stderr, "pa_stream_new() failed: %s\n", pa_strerror(pa_context_errno(c)));
            quit(1);
        }

        // Set the stream callbacks.
        pa_stream_set_write_callback(stream, stream_write_callback, NULL);
        pa_stream_set_suspended_callback(stream, stream_suspended_callback, NULL);
        pa_stream_set_moved_callback(stream, stream_moved_callback, NULL);
        pa_stream_set_underflow_callback(stream, stream_underflow_callback, NULL);
        pa_stream_set_overflow_callback(stream, stream_overflow_callback, NULL);
        pa_stream_set_started_callback(stream, stream_started_callback, NULL);
        pa_stream_set_buffer_attr_callback(stream, stream_buffer_attr_callback, NULL);

        // Set the playback buffer attributes.
        memset(&buffer_attr, 0, sizeof(buffer_attr));
        buffer_attr.tlength = (uint32_t) AUDIO_PACKET_SIZE;
        buffer_attr.minreq = (uint32_t) -1;
        buffer_attr.maxlength = (uint32_t) -1;
        buffer_attr.prebuf = (uint32_t) -1; // Playback should never stop in case of buffer underrun (play silence)
        flags |= PA_STREAM_ADJUST_LATENCY;
            
        r = pa_stream_connect_playback(stream, NULL /* device */, &buffer_attr, (pa_stream_flags_t) flags, NULL, NULL);
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

/* Updates buffer with new data received from the LoudSpeaker server */
static void write_to_playback_stream(char* data) {
    if ( BUFFER_LENGTH - data_end < AUDIO_PACKET_SIZE ) {
	if (DEBUG)
	    printf("Wrapping buffer, space used was %d, start is %d\n", (int) (data_end), (int) data_start);
	data_end = 0;
    }

    memcpy((uint8_t*) buffer + data_end, data, AUDIO_PACKET_SIZE);

    data_end += AUDIO_PACKET_SIZE;
}

int init_pa_context(pa_mainloop* m){
    int r;
    
    mainloop_api = pa_mainloop_get_api(m);
    r = pa_signal_init(mainloop_api);
    assert(r == 0);

    // Create a new connection context and connect it
    context = pa_context_new(mainloop_api, "loudspeaker_client");
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

int main( int argc, char *argv[] ) {
    if ( argc < 3 ) {
        cerr << "Usage: " << argv[ 0 ] << " HOST PORT packet_size DEBUG(0,1)" << endl;
        return EXIT_FAILURE;
    }

    if (argc > 3) {
	AUDIO_PACKET_SIZE = atoi(argv[3]);
    }
    
    if (argc > 4) {
        DEBUG = atoi(argv[4]);
    }

    data_start = 0;
    data_end = 0;
    buffer = malloc(BUFFER_LENGTH);

    pa_mainloop* m = pa_mainloop_new();    
    if (!m) {
        fprintf(stderr, "pa_mainloop_new() failed.\n");
        return 1;
    }

    int init_pa = init_pa_context(m);
    if (init_pa){
        quit(1);
    }

    string host { argv[ 1 ] }, port { argv[ 2 ] };
    Address server( host, port );

    UDPSocket socket;
    socket.connect( server );

    socket.write("Connect Request");
    /* now read and write from the server using an event-driven "poller" */
    Poller poller;
    poller.add_action(Action(socket, Direction::In, [&] () {
                pair<Address, string> packet = socket.recvfrom();
                string data = packet.second;

                /* exit if the server closes the connection */
                if (data == "EOF") {
                    printf("Received EOF from server\n");
                    return ResultType::Exit;
                } else {
                    char* buf = (char*)data.data();
                    write_to_playback_stream(buf);
                    return ResultType::Continue;
                }
            })
        );
    
    /* run these two rules forever until it's time to quit */
    int ret = 1;
    while ( true ) {
        const auto retrn = poller.poll( -1 );

        /* Run the playback stream main loop */
        if (pa_mainloop_iterate(m, 0 /* no blocking */, &ret) < 0) {
            fprintf(stderr, "pa_mainloop_run() failed.\n");
            break;
        }

        if (retrn.result == PollResult::Exit) {
            cout << "reached end " << endl;
            break;
        }
    }

    if (stream)
        pa_stream_unref(stream);
    if (context)
        pa_context_unref(context);
    if (m) {
        pa_signal_done();
        pa_mainloop_free(m);
    }
    if(buffer)
	free(buffer);
    
    return ret;
}

