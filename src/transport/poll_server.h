#ifndef PLACES_POLL_SERVER_H
#define PLACES_POLL_SERVER_H

// Caller owns listener. The loop closes every accepted socket before returning.
int run_poll_server(int listener, const char *host, int port);

#endif
