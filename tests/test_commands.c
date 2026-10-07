#include "support.h"
#include "command_golden.h"
#include "../reference/tests/test_fixtures.h"
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdio.h>

typedef struct {
    int tx, rx, use_socket;
    struct sockaddr_in peer;
    unsigned sent, head, tail;
    uint8_t packets[16][1100];
    size_t sizes[16];
} udp_test_t;
static int udp_send(void *user, const uint8_t *p, size_t size) {
    udp_test_t *u = user;
    if (u->use_socket) {
        ssize_t bytes = sendto(u->tx, p, size, 0, (const struct sockaddr *)&u->peer, sizeof u->peer);
        if (bytes != (ssize_t)size) return -1;
    } else {
        assert(u->head - u->tail < 16 && size <= 1100);
        memcpy(u->packets[u->head % 16], p, size);
        u->sizes[u->head++ % 16] = size;
    }
    ++u->sent; return 0;
}
static void receive(udp_test_t *u, uint8_t *p, size_t *size) {
    if (u->use_socket) {
        ssize_t n = recv(u->rx, p, 1100, MSG_DONTWAIT);
        assert(n > 0); *size = (size_t)n;
    } else {
        assert(u->tail < u->head);
        *size = u->sizes[u->tail % 16];
        memcpy(p, u->packets[u->tail++ % 16], *size);
    }
    test_wire(p, *size);
}
static void empty(udp_test_t *u) {
    if (!u->use_socket) { assert(u->head == u->tail); return; }
    uint8_t p[1100]; assert(recv(u->rx, p, sizeof p, MSG_DONTWAIT) < 0);
    assert(errno == EAGAIN || errno == EWOULDBLOCK);
}
int main(int argc, char **argv) {
    (void)argv;
    uint8_t frame[1100], received[1100]; size_t size;
    int length = dji_neo_build_gimbal_rate(frame, sizeof frame, 0x9e3c, -36);
    assert(length == sizeof GIMBAL_DUML && memcmp(frame, GIMBAL_DUML, sizeof GIMBAL_DUML) == 0);
    assert(dji_neo_build_gimbal_rate(frame, 20, 0, -36) == DJI_NEO_ENOSPACE);
    assert(dji_neo_build_gimbal_rate(frame, sizeof frame, 0, 201) == DJI_NEO_EINVAL);
    assert(dji_neo_build_gimbal_enable(frame, sizeof frame, 0, 4) == DJI_NEO_EINVAL);
    uint8_t large[1011] = {0};
    assert(dji_neo_duml_build(frame, sizeof frame, 2, 4, 0, 0, 4, 12, large, 1010) == 1023);
    assert(dji_neo_duml_valid(frame, 1023));
    assert(dji_neo_duml_build(frame, sizeof frame, 2, 4, 0, 0, 4, 12, large, 1011) == DJI_NEO_EINVAL);
    assert(dji_neo_duml_build(frame, sizeof frame, 2, 4, 0, 0, 4, 12, NULL, 1) == DJI_NEO_EINVAL);
    udp_test_t u = {.tx=-1, .rx=-1, .use_socket=argc > 1};
    if (u.use_socket) {
        u.rx = socket(AF_INET, SOCK_DGRAM, 0); u.tx = socket(AF_INET, SOCK_DGRAM, 0);
        if (u.rx < 0 || u.tx < 0) {
            perror("UDP loopback unavailable");
            if (u.rx >= 0) close(u.rx);
            if (u.tx >= 0) close(u.tx);
            return 77;
        }
        u.peer.sin_family = AF_INET; u.peer.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        assert(bind(u.rx, (const struct sockaddr *)&u.peer, sizeof u.peer) == 0);
        socklen_t addr_size = sizeof u.peer;
        assert(getsockname(u.rx, (struct sockaddr *)&u.peer, &addr_size) == 0);
    }
    dji_neo_config_t cfg = {0}; cfg.udp_send = udp_send; cfg.udp_user = &u;
    cfg.session_id = 0x4fb0; cfg.body_id = 0x707d; cfg.disable_activation = 1;
    dji_neo_t *n = dji_neo_create(&cfg); assert(n);
    assert(dji_neo_gimbal_start(n) == DJI_NEO_ESTATE); empty(&u);
    test_connect(n); receive(&u, received, &size); assert(size == 48);
    assert(dji_neo_gimbal_start(n) == DJI_NEO_ESTATE); empty(&u);
    test_io_t io = {.signer_ready=1};
    dji_neo_signer_t signer = {.sign=test_sign, .user=&io, .ready=test_ready};
    dji_neo_set_signer(n, &signer);
    assert(dji_neo_set_command_armed(n, 1) == DJI_NEO_OK);
    assert(dji_neo_gimbal_start(n) == DJI_NEO_ESTATE); empty(&u);
    assert(dji_neo_confirm_takeover(n, 1) == DJI_NEO_OK);
    assert(dji_neo_gimbal_start(n) == DJI_NEO_ESTATE); empty(&u);
    assert(dji_neo_set_stick_enabled(n, 1) == DJI_NEO_OK);
    assert(dji_neo_gimbal_start(n) == DJI_NEO_OK);
    for (unsigned stage = 0; stage < 4; ++stage) {
        receive(&u, received, &size);
        int len = dji_neo_build_gimbal_enable(frame, sizeof frame, (uint16_t)stage, stage);
        assert(size == (size_t)len + 20 && memcmp(received + 20, frame, (size_t)len) == 0);
        assert(test_le16(received + 4) == stage * 8 && received[16] == stage && received[19] == 0xa5);
    }
    assert(dji_neo_gimbal_start(n) == DJI_NEO_OK); empty(&u); /* idempotent */
    assert(dji_neo_gimbal_set_rate(n, -36, 1) == DJI_NEO_OK);
    assert(dji_neo_poll(n, 1) == DJI_NEO_OK);
    receive(&u, received, &size);
    assert(size == sizeof golden_gimbal_packet && memcmp(received, golden_gimbal_packet, size) == 0);
    assert(dji_neo_poll(n, 40) == DJI_NEO_OK);
    receive(&u, received, &size); assert(received[6] == 4); empty(&u);
    assert(dji_neo_poll(n, 41) == DJI_NEO_OK);
    receive(&u, received, &size); assert(received[29] == 4 && received[30] == 0x0c);
    assert((int16_t)test_le16(received + 35) == -36);
    /* Watchdog emits zero; gimbal keepalive is still emitted at 1 Hz. */
    test_idle_telemetry(n, 601);
    assert(dji_neo_poll(n, 601) == DJI_NEO_OK);
    receive(&u, received, &size); assert(received[6] == 4);
    receive(&u, received, &size); assert(test_le16(received + 35) == 0); empty(&u);
    test_idle_telemetry(n, 1001);
    assert(dji_neo_poll(n, 1001) == DJI_NEO_OK);
    receive(&u, received, &size); assert(received[6] == 4);
    receive(&u, received, &size); assert(received[30] == 0x12);
    assert(memcmp(received + 31, (uint8_t[]){0xe6,1,0x48}, 3) == 0); empty(&u);
    assert(dji_neo_gimbal_stop(n) == DJI_NEO_OK);
    receive(&u, received, &size); assert(received[30] == 0x0c && test_le16(received + 35) == 0);
    assert(dji_neo_set_command_armed(n, 0) == DJI_NEO_OK);
    assert(dji_neo_gimbal_start(n) == DJI_NEO_ESTATE); empty(&u);
    dji_neo_destroy(n);
    if (u.use_socket) { close(u.tx); close(u.rx); }
    return 0;
}
