/*
 * VitaPlay timing clock: handle both PT210 requests and PT211 responses.
 * The original port ignored responses, so its media clock never synchronized.
 */
#include "ntp.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define PT_REQUEST 210
#define PT_RESPONSE 211
#define PACKET_BYTES 32
#define ORIGINATE_OFFSET 8
#define RECEIVE_OFFSET 16
#define TRANSMIT_OFFSET 24
#define NTP_EPOCH_OFFSET 2208988800ULL
#define MAX_RTT_TICKS (3ULL << 32)

struct ntp_clock {
    int fd;
    struct sockaddr_in peer;
    bool has_peer;
    volatile bool stopping;
    int refs;
    pthread_mutex_t lock;
    int64_t ntp_offset;
    uint64_t pending_t1;
    unsigned response_count;
    unsigned request_count;
    bool synchronized;
};

static uint64_t be64(const uint8_t *p) {
    return ((uint64_t)get_u32be(p) << 32) | get_u32be(p + 4);
}
static void put_be64(uint8_t *p, uint64_t x) {
    put_u32be(p, (uint32_t)(x >> 32));
    put_u32be(p + 4, (uint32_t)x);
}
static void ntp_release(ntp_clock *c) {
    if (__atomic_sub_fetch(&c->refs, 1, __ATOMIC_ACQ_REL) == 0) {
        pthread_mutex_destroy(&c->lock);
        free(c);
    }
}
static uint64_t ntp_now(ntp_clock *c) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    uint64_t ticks = ((uint64_t)tv.tv_sec + NTP_EPOCH_OFFSET) << 32;
    ticks |= ((uint64_t)tv.tv_usec << 32) / 1000000ULL;
    pthread_mutex_lock(&c->lock);
    ticks += (uint64_t)c->ntp_offset;
    pthread_mutex_unlock(&c->lock);
    return ticks;
}
static void handle_response(ntp_clock *c, const uint8_t *p,
                            const struct sockaddr_in *from) {
    if (!c->has_peer || from->sin_addr.s_addr != c->peer.sin_addr.s_addr) return;
    uint64_t t4 = ntp_now(c);
    uint64_t t1 = be64(p + ORIGINATE_OFFSET);
    uint64_t t2 = be64(p + RECEIVE_OFFSET);
    uint64_t t3 = be64(p + TRANSMIT_OFFSET);
    pthread_mutex_lock(&c->lock);
    if (!c->pending_t1 || c->pending_t1 != t1) {
        pthread_mutex_unlock(&c->lock);
        return;
    }
    c->pending_t1 = 0;
    int64_t rtt = (int64_t)(t4 - t1) - (int64_t)(t3 - t2);
    if (rtt < 0 || (uint64_t)rtt > MAX_RTT_TICKS) {
        pthread_mutex_unlock(&c->lock);
        return;
    }
    int64_t adjustment = (int64_t)(t2 - t1) / 2 + (int64_t)(t3 - t4) / 2;
    if (!c->synchronized) {
        c->ntp_offset += adjustment;
        c->synchronized = true;
    } else {
        c->ntp_offset += adjustment / 8;
    }
    unsigned count = ++c->response_count;
    pthread_mutex_unlock(&c->lock);
    if (count == 1 || (count % 16) == 0)
        vp_log("timing PT211 synchronized: responses=%u rtt_ms=%u", count,
               (unsigned)(((uint64_t)rtt * 1000ULL) >> 32));
}
static void *ntp_receiver(void *arg) {
    ntp_clock *c = arg;
    uint8_t packet[256];
    while (!c->stopping) {
        struct sockaddr_in from;
        socklen_t from_len = sizeof(from);
        int n = recvfrom(c->fd, packet, sizeof(packet), 0,
                         (struct sockaddr *)&from, &from_len);
        if (n < PACKET_BYTES) {
            if (n < 0 && c->stopping) break;
            continue;
        }
        if (packet[1] == PT_RESPONSE) {
            handle_response(c, packet, &from);
        } else if (packet[1] == PT_REQUEST) {
            uint8_t response[PACKET_BYTES] = {0x80, PT_RESPONSE, 0, 7};
            memcpy(response + ORIGINATE_OFFSET, packet + TRANSMIT_OFFSET, 8);
            put_be64(response + RECEIVE_OFFSET, ntp_now(c));
            put_be64(response + TRANSMIT_OFFSET, ntp_now(c));
            sendto(c->fd, response, sizeof(response), 0,
                   (struct sockaddr *)&from, from_len);
            if (c->request_count++ == 0)
                vp_log("timing PT210 phone request received and answered");
        }
    }
    ntp_release(c);
    return NULL;
}
static void *ntp_sender(void *arg) {
    ntp_clock *c = arg;
    bool reported = false;
    while (!c->stopping) {
        uint8_t request[PACKET_BYTES] = {0x80, PT_REQUEST, 0, 7};
        uint64_t t1 = ntp_now(c);
        put_be64(request + TRANSMIT_OFFSET, t1);
        pthread_mutex_lock(&c->lock);
        c->pending_t1 = t1;
        pthread_mutex_unlock(&c->lock);
        int rc = sendto(c->fd, request, sizeof(request), 0,
                        (struct sockaddr *)&c->peer, sizeof(c->peer));
        if (!reported) {
            vp_log("timing PT210 request sent: rc=%d", rc);
            reported = true;
        }
        for (int i = 0; i < 10 && !c->stopping; i++) usleep(100 * 1000);
    }
    ntp_release(c);
    return NULL;
}
ntp_clock *ntp_start(uint32_t peer_ip, int peer_port) {
    ntp_clock *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->fd = udp_bind(TIMING_PORT);
    if (c->fd < 0) {
        vp_log("timing port %d bind failed", TIMING_PORT);
        free(c);
        return NULL;
    }
    pthread_mutex_init(&c->lock, NULL);
    c->refs = 1;
    if (peer_port > 0) {
        c->peer.sin_family = AF_INET;
        c->peer.sin_port = htons(peer_port);
        c->peer.sin_addr.s_addr = peer_ip;
        c->has_peer = true;
        c->refs = 2;
        spawn_thread(ntp_sender, c);
    }
    spawn_thread(ntp_receiver, c);
    vp_log("timing port active: local=%d peer_port=%d", TIMING_PORT, peer_port);
    return c;
}
void ntp_stop(ntp_clock *clock) {
    if (!clock) return;
    int fd = clock->fd;
    clock->stopping = true;
    shutdown(fd, SHUT_RDWR);
    close(fd);
}
