#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <iostream>
#include <thread>
#include <pulse/simple.h>
#include <vector>

#include "socket.hh"
#include "util.hh"
#include "poller.hh"

#define BUFSIZE 256

using namespace std;
using namespace PollerShortNames;

int main( int argc, char *argv[] )
{
    if ( argc != 4 ) {
	cerr << "Usage: " << argv[ 0 ] << " HOST PORT outfile" << endl;
	return EXIT_FAILURE;
    }

    static const pa_sample_spec ss = {
	.format = PA_SAMPLE_S16LE,
	.rate = 44100, 
	.channels = 2
    };

    pa_simple *s = NULL;
    int error;
    s = pa_simple_new(NULL, argv[0], PA_STREAM_PLAYBACK, NULL, "playback", &ss, NULL, NULL, &error);

    FILE *fd; 
    fd = fopen(argv[3], "wb");


    string host { argv[ 1 ] }, port { argv[ 2 ] };
    Address server( host, port );

    vector<string> audioBuffer;

    UDPSocket socket;
    socket.connect( server );

    socket.write("Hi!!\n");
    /* now read and write from the server using an event-driven "poller" */
    Poller poller;
    /* first rule: if the socket has data ready (in the "In" direction),
       print it to the screen (cout) */
    poller.add_action( Action( socket, Direction::In,
			     [&] () {
				   pair<Address, string> p = socket.recvfrom();
				   /* exit if the server closes the connection */
				   if (p.second == "eof") {
				       return ResultType::Exit;
				   } else {
					   audioBuffer.push_back(p.second);
					   char* buf = (char*)p.second.data();
					   fwrite(buf, sizeof(char), BUFSIZE, fd);
					   //pa_simple_write(s, buf, (size_t) BUFSIZE, &error);
				       return ResultType::Continue;
				   }
			     } ) );

    int i = 0;    
    /* run these two rules forever until it's time to quit */
    while ( true ) {
		const auto ret = poller.poll( -1 );

		if ( ret.result == PollResult::Exit ) {
			cout << "reached end " << audioBuffer.size() << endl;
			for (int i = 0; i < audioBuffer.size(); i++) {
				string audioChunk = audioBuffer.at(i);
				char* buf = (char*)audioChunk.data();
				pa_simple_write(s, buf, (size_t) BUFSIZE, &error);
	  		}	

			pa_simple_drain(s, &error);
			
			if (s)
				pa_simple_free(s);
			
			return ret.exit_status;
		}
    }

    fclose(fd);
}
