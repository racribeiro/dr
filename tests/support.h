#ifndef TEST_SUPPORT_H
#define TEST_SUPPORT_H
#include "dji_neo/dji_neo.h"
#include "dji_neo/commands.h"
#include <assert.h>
#include <string.h>

typedef struct {
    uint8_t last[1100];
    size_t size;
    unsigned sends, signs;
    int fail_send, reject_sign, corrupt_identity, signer_ready;
} test_io_t;
static inline uint16_t test_le16(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }
static inline int test_send(void *user, const uint8_t *p, size_t size) {
    test_io_t *io = user;
    if (io->fail_send) { io->fail_send = 0; return -1; }
    assert(size <= sizeof io->last);
    memcpy(io->last, p, size); io->size = size; ++io->sends;
    return 0;
}
static inline int test_ready(void *user, uint16_t session, uint16_t body) {
    (void)session; (void)body;
    return ((test_io_t *)user)->signer_ready;
}
static inline int test_sign(void *user, dji_neo_sign_request_t *request) {
    test_io_t *io = user; ++io->signs;
    assert(request->session_id == 0x4fb0 && request->body_id == 0x707d);
    assert(test_le16(request->rc_subheader + 2) == request->field45);
    assert(request->rc_subheader[8] == request->counter);
    if (io->reject_sign) return -1;
    if (io->corrupt_identity) request->rc_subheader[0] ^= 1;
    request->rc_subheader[11] = 0xa5; /* mock only; not a DJI rolling code */
    return 0;
}
static inline void test_checksum(uint8_t *p) {
    p[7] = 0; for (unsigned i = 0; i < 7; ++i) p[7] ^= p[i];
}
static inline void test_accept(dji_neo_t *n, uint64_t now) {
    uint8_t p[] = {0x08,0x80,0xb0,0x4f,0,0,0,0,1}; /* advisory length mismatch */
    test_checksum(p);
    assert(dji_neo_on_datagram(n, p, sizeof p, now) == DJI_NEO_OK);
}
static inline void test_idle_telemetry(dji_neo_t *n, uint64_t now) {
    uint8_t p[] = {8,0x80,0xb0,0x4f,0,0,1,0};
    test_checksum(p);
    assert(dji_neo_on_datagram(n, p, sizeof p, now) == DJI_NEO_OK);
}
static inline dji_neo_t *test_client(test_io_t *io, int disable_activation) {
    dji_neo_config_t cfg = {0};
    cfg.udp_send = test_send; cfg.udp_user = io;
    cfg.session_id = 0x4fb0; cfg.body_id = 0x707d;
    cfg.disable_activation = disable_activation;
    dji_neo_t *n = dji_neo_create(&cfg); assert(n);
    return n;
}
static inline void test_connect(dji_neo_t *n) {
    assert(dji_neo_set_session_armed(n, 1) == DJI_NEO_OK);
    assert(dji_neo_poll(n, 0) == DJI_NEO_OK);
    test_accept(n, 1);
}
static inline void test_arm_all(dji_neo_t *n, test_io_t *io) {
    dji_neo_signer_t signer = {.sign=test_sign, .user=io, .ready=test_ready};
    io->signer_ready = 1; dji_neo_set_signer(n, &signer);
    assert(dji_neo_set_command_armed(n, 1) == DJI_NEO_OK);
    assert(dji_neo_confirm_takeover(n, 1) == DJI_NEO_OK);
    assert(dji_neo_set_stick_enabled(n, 1) == DJI_NEO_OK);
}
static inline void test_wire(const uint8_t *p, size_t size) {
    assert(size >= 8 && (test_le16(p) & 0x7fff) == size);
    uint8_t x = 0; for (unsigned i = 0; i < 7; ++i) x ^= p[i]; assert(x == p[7]);
    if (p[6] == 5) {
        assert(size >= 33 && test_le16(p + 4) == test_le16(p + 10));
        assert(p[8] == 0x70 && p[9] == 0x7d);
        assert(dji_neo_duml_valid(p + 20, size - 20));
    } else assert(test_le16(p + 4) == 0);
}
#endif
