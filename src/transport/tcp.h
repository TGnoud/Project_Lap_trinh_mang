#ifndef PLACES_TCP_H
#define PLACES_TCP_H

#include <stdint.h>

/* A successful call returns a socket fd that the caller must close. */
int open_listener(const char *host, uint16_t port);
int connect_to_server(const char *host, uint16_t port);

/* Port of the local endpoint, or -1 on failure. */
int local_port(int fd);
int set_nonblocking(int fd, int enabled);
int64_t monotonic_ms(void);

#endif
