#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <iostream>
#include <thread>

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

    FILE *fd; 
    fd = fopen(argv[3], "wb");


    string host { argv[ 1 ] }, port { argv[ 2 ] };
    Address server( host, port );


    TCPSocket socket;
    socket.connect( server );

    /* now read and write from the server using an event-driven "poller" */
    Poller poller;

    /* first rule: if the socket has data ready (in the "In" direction),
       print it to the screen (cout) */
    poller.add_action( Action( socket, Direction::In,
			       [&] () {
				   const char *c = socket.read().c_str();
				   fwrite(c, sizeof(char), BUFSIZE, fd);

				   /* exit if the server closes the connection */
				   if ( socket.eof() ) {
				       return ResultType::Exit;
				   } else {
				       return ResultType::Continue;
				   }
			       } ) );

    /* run these two rules forever until it's time to quit */
    while ( true ) {
	const auto ret = poller.poll( -1 );
	if ( ret.result == PollResult::Exit ) {
	    return ret.exit_status;
	}
    }

    fclose(fd);
}
