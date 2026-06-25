#ifndef HEARTBEAT_H
#define HEARTBEAT_H

#include <netinet/in.h>
#include <stdint.h>

#define HB_MAGIC   0x4B414C56u  /* "KALV" */
#define HB_VERSION 1

/* flags */
#define HB_FLAG_GOODBYE 0x01

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t  version;
    uint8_t  priority;
    uint8_t  state;   /* 0 = BACKUP, 1 = MASTER */
    uint8_t  flags;
    uint32_t seq;
} hb_packet_t;

int  hb_socket_open(uint16_t port);
void hb_fill(hb_packet_t *pkt, uint8_t priority, uint8_t state,
             uint32_t seq, int goodbye);
int  hb_send(int sock, const struct sockaddr_in *peer,
             const hb_packet_t *pkt);
int  hb_recv(int sock, hb_packet_t *pkt, struct sockaddr_in *from);

#endif /* HEARTBEAT_H */
