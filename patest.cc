#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

#include <pulse/simple.h>


#define BUFSIZE 1024


int main(int argc, char* argv[]) {

    static const pa_sample_spec ss = {
	.format = PA_SAMPLE_S16LE,
	.rate = 44100, 
	.channels = 2
    };

    pa_simple *s = NULL;
    int ret = 1;
    int error;

    if (argc > 1) {
	int fd; 
	fd = open(argv[1], O_RDONLY);
	dup2(fd, STDIN_FILENO);
	close(fd);
    }	


    s = pa_simple_new(NULL, argv[0], PA_STREAM_PLAYBACK, NULL, "playback", &ss, NULL, NULL, &error);

    for (;;) {
	uint8_t buf[BUFSIZE];
	pa_usec_t latency;
	ssize_t r;

	r = read(STDIN_FILENO, buf, sizeof(buf));
	if (r == 0) 
	    break;
	
	pa_simple_write(s, buf, (size_t) r, &error);
    }
    
    pa_simple_drain(s, &error);
    ret = 0;

    if (s)
	pa_simple_free(s);
    
    return ret;
}
     
     
