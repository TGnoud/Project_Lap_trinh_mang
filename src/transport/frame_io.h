#ifndef PLACES_FRAME_IO_H
#define PLACES_FRAME_IO_H

#include "protocol/frame.h"

typedef struct {
    unsigned char bytes[1024];
    size_t size;
    size_t offset;
    FrameReader reader;
} FrameInput;

int send_frame(int fd, const char *body, size_t body_size);

/* Blocking D03 demo. Start input with {0}; reset its reader before closing. */
/* Returns 1 for a frame, 0 for clean EOF, or -1 for an error. */
int receive_frame(int fd, FrameInput *input);

#endif
