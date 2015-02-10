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

#define AUDIO_PACKET_SIZE 256
#define CLEAR_LINE "\x1B[K"

using namespace std;
using namespace PollerShortNames;


static pa_context *context = NULL;
static pa_stream *stream = NULL;
static pa_mainloop_api *mainloop_api = NULL;
static int DEBUG = 0;

static pa_stream_flags_t flags = (pa_stream_flags_t) 0;

static void *buffer = NULL;
static size_t buffer_length = 0, buffer_index = 0;

static const pa_sample_spec ss = {
    .format = PA_SAMPLE_S16LE,
    .rate = 44100, 
    .channels = 2
};


/* A shortcut for terminating the application */
static void quit(int ret) {
    assert(mainloop_api);
    mainloop_api->quit(mainloop_api, ret);
}


/* Write some data to the stream */
static void do_stream_write(size_t length) {
    size_t l;
    assert(length);

    if (!buffer || !buffer_length)
        return;

    l = length;
    if (l > buffer_length)
        l = buffer_length;

    if (pa_stream_write(stream, (uint8_t*) buffer + buffer_index, l, NULL, 0, PA_SEEK_RELATIVE) < 0) {
        fprintf(stderr, "pa_stream_write() failed: %s\n", pa_strerror(pa_context_errno(context)));
        quit(1);
        return;
    }

    buffer_length -= l;
    buffer_index += l;

    if (!buffer_length) {
        pa_xfree(buffer);
        buffer = NULL;
        buffer_index = buffer_length = 0;
    }
}


/* This is called whenever new data may be written to the stream */
static void stream_write_callback(pa_stream *s, size_t length, void *userdata) {
    assert(s);
    assert(length > 0);

    if (!buffer)
        return;

    do_stream_write(length);
}


/* This routine is called whenever the stream state changes */
static void stream_state_callback(pa_stream *s, void *userdata) {
    assert(s);

    switch (pa_stream_get_state(s)) {
        case PA_STREAM_CREATING:
        case PA_STREAM_TERMINATED:
            break;
        case PA_STREAM_READY:
            if (DEBUG) {
                // Can print info about stream metrics
            }  
            break;
        case PA_STREAM_FAILED:
        default:
            fprintf(stderr, "Stream error: %s\n", pa_strerror(pa_context_errno(pa_stream_get_context(s))));
            quit(1);
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

static void stream_moved_callback(pa_stream *s, void *userdata) {
    assert(s);
    if (DEBUG)
        fprintf(stderr, "Stream moved to device %s (%u, %ssuspended).%s \n",
            pa_stream_get_device_name(s), pa_stream_get_device_index(s),
            pa_stream_is_suspended(s) ? "" : "not ", 
            CLEAR_LINE);
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
            pa_stream_set_state_callback(stream, stream_state_callback, NULL);
            pa_stream_set_write_callback(stream, stream_write_callback, NULL);
            pa_stream_set_suspended_callback(stream, stream_suspended_callback, NULL);
            pa_stream_set_moved_callback(stream, stream_moved_callback, NULL);
            pa_stream_set_underflow_callback(stream, stream_underflow_callback, NULL);
            pa_stream_set_overflow_callback(stream, stream_overflow_callback, NULL);
            pa_stream_set_started_callback(stream, stream_started_callback, NULL);
            pa_stream_set_buffer_attr_callback(stream, stream_buffer_attr_callback, NULL);

            // Set the playback buffer attributes.
            memset(&buffer_attr, 0, sizeof(buffer_attr));
            buffer_attr.tlength = (uint32_t) -1;
            buffer_attr.minreq = (uint32_t) AUDIO_PACKET_SIZE;
            buffer_attr.maxlength = (uint32_t) -1;
            buffer_attr.prebuf = 0; // Playback should never stop in case of buffer underrun (play silence).
            
            pa_cvolume cv;
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
        fprintf(stderr, "Playback stream drained.\n");

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


/* Updates buffer with new data received from the LoudSpeaker server */
static void writeToPlaybackBuffer(char* data) {
    if (buffer) {
        buffer = pa_xrealloc(buffer, buffer_length + AUDIO_PACKET_SIZE);
        memcpy((uint8_t*) buffer + buffer_length, data, AUDIO_PACKET_SIZE);
        buffer_length += AUDIO_PACKET_SIZE;
    } else {
        buffer = pa_xmalloc(AUDIO_PACKET_SIZE);
        memcpy(buffer, data, AUDIO_PACKET_SIZE);
        buffer_length = AUDIO_PACKET_SIZE;
        buffer_index = 0;
    }
}


int main( int argc, char *argv[] ) {
    if ( argc <= 4 ) {
        cerr << "Usage: " << argv[ 0 ] << " HOST PORT outfile DEBUG(0,1)" << endl;
        return EXIT_FAILURE;
    }

    FILE *fd; 
    fd = fopen(argv[3], "wb");

    int ret = 1, r, c;

    // Set up a new main loop
    pa_mainloop* m = pa_mainloop_new();    
    if (!m) {
        fprintf(stderr, "pa_mainloop_new() failed.\n");
        quit(1);
    }
    mainloop_api = pa_mainloop_get_api(m);
    r = pa_signal_init(mainloop_api);
    assert(r == 0);

    // Create a new connection context and connect it
    context = pa_context_new(mainloop_api, NULL /* client_name */);
    if (!context) {
        fprintf(stderr, "pa_context_new() failed.\n");
        quit(1);
    }
    pa_context_set_state_callback(context, context_state_callback, NULL);
    if (pa_context_connect(context, NULL, (pa_context_flags_t) 0, NULL) < 0) {
        fprintf(stderr, "pa_context_connect() failed: %s\n", pa_strerror(pa_context_errno(context)));
        quit(1);
    }


    string host { argv[ 1 ] }, port { argv[ 2 ] };
    Address server( host, port );

    if (argc == 5) {
        DEBUG = atoi(argv[4]);
    }

    UDPSocket socket;
    socket.connect( server );

    socket.write("Hi!!\n");
    /* now read and write from the server using an event-driven "poller" */
    Poller poller;
    /* first rule: if the socket has data ready (in the "In" direction),
       print it to the screen (cout) */
    poller.add_action(
        Action(socket, Direction::In,
            [&] () {
                pair<Address, string> p = socket.recvfrom();
                /* exit if the server closes the connection */
                if (p.second == "eof") {
                   return ResultType::Exit;
                } else {
                   char* buf = (char*)p.second.data();
                   writeToPlaybackBuffer(buf);
                   fwrite(buf, sizeof(char), AUDIO_PACKET_SIZE, fd);
                   return ResultType::Continue;
                }
            }
        )
    );

    int i = 0;    
    /* run these two rules forever until it's time to quit */
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
    fclose(fd);
    return ret;
}
