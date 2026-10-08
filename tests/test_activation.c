#include "support.h"

typedef struct {
    test_io_t io;
    uint16_t f45, seq;
    uint8_t counter;
    unsigned view, stick, heartbeat, subscription;
    uint64_t now, last_stick;
    int has_stick, saw_deflection, saw_recenter, signing;
} activation_io_t;
static int capture(void *user, const uint8_t *p, size_t size) {
    activation_io_t *a = user;
    test_wire(p, size);
    if (p[6] == 5) {
        assert(p[19] == (a->signing ? 0xa5 : 0));
        assert(test_le16(p + 4) == a->f45 && p[16] == a->counter);
        assert(test_le16(p + 26) == a->seq);
        a->f45 = (uint16_t)(a->f45 + 8); ++a->counter; ++a->seq;
        const uint8_t *f = p + 20;
        if (f[9] == 0x18 && f[10] == 0x47) {
            assert(f[15] == 0x1a && f[17] == (a->view < 2));
            assert(test_le16(f + 13) == (uint16_t)a->now);
            ++a->view;
        } else if (f[9] == 1 && f[10] == 0x0a) {
            if (a->has_stick) assert(a->now - a->last_stick >= 52);
            a->last_stick = a->now; a->has_stick = 1; ++a->stick;
            static const uint8_t neutral[] = {0,4,0x20,0,1,8};
            int centered = memcmp(f + 14, neutral, sizeof neutral) == 0;
            if (!centered) { assert(p[19] == 0xa5); a->saw_deflection = 1; }
            else { if (a->saw_deflection) a->saw_recenter = 1; }
        } else if (f[9] == 0 && f[10] == 1) ++a->heartbeat;
        else if (f[9] == 0x51) ++a->subscription;
    } else if (p[6] == 4 && a->seq) {
        assert(test_le16(p + 26) == (uint16_t)(a->f45 - 8));
    }
    return test_send(&a->io, p, size);
}
int main(void) {
    activation_io_t a = {0};
    dji_neo_config_t cfg = {0}; cfg.udp_send = capture; cfg.udp_user = &a;
    cfg.session_id = 0x4fb0; cfg.body_id = 0x707d;
    dji_neo_t *n = dji_neo_create(&cfg); assert(n);
    test_connect(n);
    for (a.now = 5; a.now <= 4000; a.now += 5) {
        if (a.now % 100 == 0) test_idle_telemetry(n, a.now);
        assert(dji_neo_poll(n, a.now) == DJI_NEO_OK);
    }
    assert(a.view == 30 && a.stick >= 70 && a.heartbeat >= 26 && a.subscription == 4);
    assert(a.io.signs == 0); /* Entire activation works without a signer. */
    test_arm_all(n, &a.io);
    a.signing = 1;
    assert(dji_neo_set_stick(n, 660, -660, 0, 1, 4001) == DJI_NEO_OK);
    for (a.now = 4005; a.now <= 4700; a.now += 5) {
        if (a.now % 100 == 0) test_idle_telemetry(n, a.now);
        assert(dji_neo_poll(n, a.now) == DJI_NEO_OK);
    }
    assert(a.saw_deflection && a.saw_recenter && a.io.signs > 0);
    assert(a.view == 30); /* Subscription rebroadcast does not restart liveview. */
    assert(dji_neo_restart_liveview(n) == DJI_NEO_OK);
    a.view = 0;
    for (a.now = 4705; a.now <= 4800; a.now += 5) {
        if (a.now % 100 == 0) test_idle_telemetry(n, a.now);
        assert(dji_neo_poll(n, a.now) == DJI_NEO_OK);
    }
    assert(a.view == 2); /* Start edge is asserted again only for the new burst. */
    assert(dji_neo_set_session_armed(n, 0) == DJI_NEO_OK);
    unsigned before = a.io.sends;
    assert(dji_neo_poll(n, 5000) == DJI_NEO_OK && a.io.sends == before);
    dji_neo_destroy(n);
    return 0;
}
