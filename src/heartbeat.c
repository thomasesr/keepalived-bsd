#include <arpa/inet.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "heartbeat.h"
#include "logger.h"

int hb_socket_open(uint16_t port)
{
    int sock;
    int opt = 1;
    struct sockaddr_in addr;

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        log_err("hb_socket_open: socket: %s", strerror(errno));
        return -1;
    }

    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        log_err("hb_socket_open: bind port %u: %s", port, strerror(errno));
        close(sock);
        return -1;
    }

    return sock;
}

void hb_fill(hb_packet_t *pkt, uint8_t priority, uint8_t state,
             uint32_t seq, int goodbye)
{
    pkt->magic    = htonl(HB_MAGIC);
    pkt->version  = HB_VERSION;
    pkt->priority = priority;
    pkt->state    = state;
    pkt->flags    = goodbye ? HB_FLAG_GOODBYE : 0;
    pkt->seq      = htonl(seq);
}

int hb_send(int sock, const struct sockaddr_in *peer, const hb_packet_t *pkt)
{
    ssize_t n = sendto(sock, pkt, sizeof(*pkt), 0,
                       (const struct sockaddr *)peer, sizeof(*peer));
    if (n < 0) {
        log_warn("hb_send: %s", strerror(errno));
        return -1;
    }
    return 0;
}

int hb_recv(int sock, hb_packet_t *pkt, struct sockaddr_in *from)
{
    socklen_t fromlen = sizeof(*from);
    ssize_t   n;

    n = recvfrom(sock, pkt, sizeof(*pkt), 0,
                 (struct sockaddr *)from, &fromlen);
    if (n < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            log_warn("hb_recv: %s", strerror(errno));
        return -1;
    }
    if ((size_t)n < sizeof(*pkt)) {
        log_warn("hb_recv: short packet (%zd bytes)", n);
        return -1;
    }
    if (ntohl(pkt->magic) != HB_MAGIC) {
        log_warn("hb_recv: bad magic");
        return -1;
    }
    if (pkt->version != HB_VERSION) {
        log_warn("hb_recv: unsupported version %u", pkt->version);
        return -1;
    }
    pkt->seq = ntohl(pkt->seq);
    return 0;
}
