/*
 * The host end of Consul 2 -- kernel task 29c, and the one host-compiled C program in this
 * directory.  Everything else here is BESM-6 code; this runs on the build machine, beside the
 * simulator, and its whole job is to be the terminal that `/dev/tty1' talks to.
 *
 *      ttyhost PORT OUTFILE READYFILE TIMEOUT
 *
 * IT TYPES NOTHING.  The dialogue lives in test/multi.ini, as `send TTY:26,"..."' -- SCP's own
 * injection, on the line SIMH holds -- so that both terminals are driven from one file, in one
 * order, by the same expect/send machinery every other test in this suite uses.  This program
 * only holds the line open and writes down what Consul 2 printed, which run-multi.sh then diffs
 * against multi.expected.  That transcript is the only record in the tree of what a SECOND
 * terminal saw.
 *
 * WHY IT LISTENS AND SIMH DIALS OUT, rather than the obvious way round.  A Consul line is a
 * terminal only while a client is attached: vt_putc()/vt_puts() (besm6_tty.c) return early on
 * !t->conn, so everything the guest prints to an unconnected line is discarded, silently.  With
 * SIMH listening (`attach tty26 Line=26,PORT') the connection is accepted in tmxr_poll_conn(),
 * which the BESM-6 tty polls at most once per second of HOST time -- and measured here, the
 * accept lands about a second after `go', which is AFTER init's merge() has forked the getty on
 * /dev/tty1 and after that getty has printed its prompt.  The prompt is then gone, and the
 * dialogue has nothing to expect.  Turning it round removes the race instead of papering over
 * it: this program binds the port before the simulator is started at all, `attach tty26
 * Line=26,Connect=127.0.0.1:PORT' opens the connection when the .ini is PARSED, i.e. before
 * `go', and the first poll of the run completes it -- long before the root is even mounted.
 *
 * READYFILE is that handshake: it is created once the port is bound and listening, and
 * run-multi.sh waits for it before starting the simulator.  Without it the shell would be
 * racing the bind.
 *
 * TIMEOUT (seconds) bounds both the wait for the connection and any single read, so that a
 * simulator which dies without closing the line cannot leave this process behind.  Normal exit
 * is the peer closing at `quit'.
 */
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc != 5) {
        fprintf(stderr, "usage: %s PORT OUTFILE READYFILE TIMEOUT\n", argv[0]);
        return 2;
    }
    int port              = atoi(argv[1]);
    const char *outfile   = argv[2];
    const char *readyfile = argv[3];
    int timeout           = atoi(argv[4]);
    if (port <= 0 || timeout <= 0) {
        fprintf(stderr, "ttyhost: bad port or timeout\n");
        return 2;
    }

    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) {
        perror("ttyhost: socket");
        return 1;
    }
    int on = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port        = htons((unsigned short)port);
    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "ttyhost: bind %d: %s\n", port, strerror(errno));
        return 1;
    }
    if (listen(srv, 1) < 0) {
        perror("ttyhost: listen");
        return 1;
    }

    /* The port is bound: the caller may now start the simulator. */
    FILE *ready = fopen(readyfile, "w");
    if (!ready) {
        fprintf(stderr, "ttyhost: %s: %s\n", readyfile, strerror(errno));
        return 1;
    }
    fclose(ready);

    struct timeval tv;
    tv.tv_sec  = timeout;
    tv.tv_usec = 0;
    setsockopt(srv, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    int conn = accept(srv, NULL, NULL);
    if (conn < 0) {
        fprintf(stderr, "ttyhost: no connection within %d s: %s\n", timeout, strerror(errno));
        return 1;
    }
    close(srv);
    setsockopt(conn, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    FILE *out = fopen(outfile, "wb");
    if (!out) {
        fprintf(stderr, "ttyhost: %s: %s\n", outfile, strerror(errno));
        return 1;
    }
    for (;;) {
        char buf[4096];
        ssize_t n = read(conn, buf, sizeof(buf));
        if (n == 0) /* the simulator quit and closed the line */
            break;
        if (n < 0) {
            if (errno == EINTR)
                continue;
            fprintf(stderr, "ttyhost: read: %s\n", strerror(errno));
            fclose(out);
            return 1;
        }
        fwrite(buf, 1, (size_t)n, out);
        fflush(out);
    }
    fclose(out);
    close(conn);
    return 0;
}
