#include <stdio.h>
#include <stdlib.h> 
#include <string.h>

#define BUFFER_LENGTH 256 * 2048

static int AUDIO_PACKET_SIZE = 256;
static const pa_sample_spec ss = {
    .format = PA_SAMPLE_S16LE,
    .rate = 44100, 
    .channels = 2
};

