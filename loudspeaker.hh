#include <stdio.h>
#include <stdlib.h> 
#include <string.h>

#define AUDIO_PACKET_SIZE 256
#define BUFFER_LENGTH AUDIO_PACKET_SIZE * 2048

static const pa_sample_spec ss = {
    .format = PA_SAMPLE_S16LE,
    .rate = 44100, 
    .channels = 2
};

