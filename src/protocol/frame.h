#ifndef PLACES_FRAME_H
#define PLACES_FRAME_H

#include <stdbool.h>
#include <stddef.h>

enum { FRAME_HEADER_SIZE = 4, FRAME_MAX_SIZE = 64 * 1024 };

typedef enum { FRAME_MORE, FRAME_READY, FRAME_ERROR } FrameStatus;

/* Start with {0}. Reset after READY or ERROR, and before closing the connection. */
typedef struct {
    unsigned char header[FRAME_HEADER_SIZE];
    size_t header_used;
    char *body;
    size_t body_size;
    size_t body_used;
} FrameReader;

bool frame_encode_header(size_t body_size, unsigned char header[FRAME_HEADER_SIZE]);

/* Stops at the end of one frame. body stays valid until frame_reset(). */
FrameStatus frame_feed(FrameReader *reader, const unsigned char *bytes, size_t size, size_t *used);
bool frame_at_boundary(const FrameReader *reader);
void frame_reset(FrameReader *reader);

#endif
