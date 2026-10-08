#include "support.h"
#include "../reference/tests/test_fixtures.h"

/* Deliberately artificial signer. Verifies wiring, never DJI authentication. */
typedef struct {
    test_io_t io;
    uint8_t signed_header[12], signed_duml[DJI_NEO_MAX_DUML];
    size_t signed_size;
    unsigned type5, connects, keepalives, subscriptions, views, sticks, heartbeats;
    unsigned telemetry, video;
    int expect_signed;
} signing_io_t;

static int sign_all(void *user, dji_neo_sign_request_t *request) {
    signing_io_t *s = user;
    assert(request->session_id == 0x4fb0 && request->body_id == 0x707d);
    assert(request->rc_subheader[0] == 0x70 && request->rc_subheader[1] == 0x7d);
    assert(test_le16(request->rc_subheader + 2) == request->field45);
    assert(request->rc_subheader[8] == request->counter);
    assert(request->field45 == (uint16_t)(s->type5 * 8));
    assert(request->counter == (uint8_t)s->type5);
    assert(request->rc_subheader[9] == 1 && request->rc_subheader[10] == 0x60);
    assert(dji_neo_duml_valid(request->duml, request->duml_size));
    ++s->io.signs;
    if (s->io.reject_sign) return -1;
    for (unsigned i = 4; i < 12; ++i)
        request->rc_subheader[i] = (uint8_t)(0x90 + i + s->io.signs);
    if (s->io.corrupt_identity) request->rc_subheader[0] ^= 1;
    memcpy(s->signed_header, request->rc_subheader, 12);
    memcpy(s->signed_duml, request->duml, request->duml_size);
    s->signed_size = request->duml_size;
    return 0;
}
static int send_all(void *user, const uint8_t *p, size_t size) {
    signing_io_t *s = user;
    test_wire(p, size);
    if (p[6] == 5) {
        if (s->expect_signed) {
            assert(size == 20 + s->signed_size);
            assert(!memcmp(p + 8, s->signed_header, 12));
            assert(!memcmp(p + 20, s->signed_duml, s->signed_size));
        } else {
            assert(!memcmp(p + 12, (uint8_t[4]){0}, 4));
            assert(p[17] == 1 && p[18] == 0x60 && p[19] == 0);
        }
        if (s->io.fail_send) { s->io.fail_send = 0; return -1; }
        ++s->type5;
        const uint8_t *f = p + 20;
        if (f[9] == 0x51) ++s->subscriptions;
        if (f[9] == 0x18 && f[10] == 0x47) ++s->views;
        if (f[9] == 1 && f[10] == 0x0a) ++s->sticks;
        if (f[9] == 0 && f[10] == 1) ++s->heartbeats;
    } else if (p[6] == 0) ++s->connects;
    else if (p[6] == 4) ++s->keepalives;
    return test_send(&s->io, p, size);
}
static void telemetry(void *user, const dji_neo_telemetry_t *value) {
    signing_io_t *s = user;
    assert(value->attitude_valid && value->satellites == 4);
    ++s->telemetry;
}
static void video(void *user, const dji_neo_video_packet_t *value) {
    signing_io_t *s = user;
    assert(value->size == 3 && value->codec_h265);
    ++s->video;
}
static dji_neo_t *create(signing_io_t *s, int with_ready) {
    dji_neo_config_t cfg = {0};
    cfg.session_id = 0x4fb0; cfg.body_id = 0x707d;
    cfg.udp_send = send_all; cfg.udp_user = s;
    cfg.on_telemetry = telemetry; cfg.on_video = video; cfg.callback_user = s;
    dji_neo_t *n = dji_neo_create(&cfg); assert(n);
    dji_neo_signer_t signer = {.sign=sign_all, .user=s, .ready=with_ready ? test_ready : NULL};
    dji_neo_set_signer(n, &signer);
    test_connect(n);
    assert(s->io.signs == 0 && s->connects == 1);
    return n;
}
static void receive_without_signing(dji_neo_t *n, signing_io_t *s, uint64_t ms) {
    unsigned signs = s->io.signs, sends = s->io.sends;
    uint8_t osd[sizeof OSD_DGRAM]; memcpy(osd, OSD_DGRAM, sizeof osd);
    osd[2] = 0xb0; osd[3] = 0x4f; test_checksum(osd);
    assert(dji_neo_on_datagram(n, osd, sizeof osd, ms) == DJI_NEO_OK);
    uint8_t v[] = {11,0x80,0xb0,0x4f,0,0,2,0,1,2,3}; test_checksum(v);
    assert(dji_neo_on_datagram(n, v, sizeof v, ms) == DJI_NEO_OK);
    assert(s->io.signs == signs && s->io.sends == sends);
    assert(s->telemetry && s->video);
}
int main(void) {
    signing_io_t s = {0}; s.io.signer_ready = s.expect_signed = 1;
    dji_neo_t *n = create(&s, 1);
    for (uint64_t ms = 5; ms <= 4000; ms += 5) {
        if (ms % 100 == 0) test_idle_telemetry(n, ms);
        assert(dji_neo_poll(n, ms) == DJI_NEO_OK);
    }
    assert(s.subscriptions && s.views == 30 && s.sticks >= 70 && s.heartbeats >= 26);
    assert(s.io.signs == s.type5 && s.keepalives > 100);
    /* Readiness authorises signing, not movement: all four arms remain shut. */
    uint8_t f[32]; int len = dji_neo_build_gimbal_rate(f, sizeof f, 0, -36);
    unsigned signs = s.io.signs, sends = s.io.sends;
    assert(dji_neo_send_actuation(n, f, (size_t)len) == DJI_NEO_ESTATE);
    assert(s.io.signs == signs && s.io.sends == sends);
    len = dji_neo_build_heartbeat(f, sizeof f, 55);
    assert(dji_neo_send_query(n, f, (size_t)len) == DJI_NEO_OK);
    assert(s.io.signs == signs + 1);
    s.io.reject_sign = 1;
    receive_without_signing(n, &s, 4001); /* Even a ready declining signer cannot block receive. */
    dji_neo_destroy(n);

    /* A ready signer must not be bypassed on rejection/corrupt identity. */
    memset(&s, 0, sizeof s); s.io.signer_ready = s.expect_signed = s.io.reject_sign = 1;
    n = create(&s, 1); sends = s.io.sends;
    assert(dji_neo_poll(n, 5) == DJI_NEO_EAUTH && s.io.sends == sends);
    assert(s.io.signs == 1 && s.type5 == 0);
    receive_without_signing(n, &s, 6);
    s.io.reject_sign = 0; s.io.corrupt_identity = 1;
    assert(dji_neo_poll(n, 7) == DJI_NEO_EAUTH && s.io.sends == sends);
    s.io.corrupt_identity = 0; s.io.fail_send = 1;
    assert(dji_neo_poll(n, 8) == DJI_NEO_EIO && s.io.sends == sends);
    assert(dji_neo_poll(n, 9) == DJI_NEO_OK);
    /* Three failed heartbeat attempts consumed no f45/DUML/sub-counter. */
    assert(test_le16(s.signed_header + 2) == (uint16_t)((s.type5 - 1) * 8));
    assert(s.io.signs == s.type5 + 3);
    dji_neo_destroy(n);

    /* A missing ready callback or ready==0 preserves the unsigned path. */
    for (int with_ready = 0; with_ready <= 1; ++with_ready) {
        memset(&s, 0, sizeof s); s.io.reject_sign = 1;
        n = create(&s, with_ready);
        assert(dji_neo_poll(n, 5) == DJI_NEO_OK && s.type5 > 0 && !s.io.signs);
        dji_neo_capabilities_t caps; dji_neo_get_capabilities(n, &caps);
        assert(caps.has_link && !caps.has_actuation);
        assert(dji_neo_set_command_armed(n, 1) == DJI_NEO_EAUTH);
        receive_without_signing(n, &s, 6);
        dji_neo_destroy(n);
    }
    /* Loss of signer readiness revokes arms, centers sticks, and degrades
     * activation to unsigned. Restoring readiness never silently rearms. */
    memset(&s, 0, sizeof s); s.io.signer_ready = s.expect_signed = 1;
    n = create(&s, 1);
    assert(dji_neo_set_command_armed(n, 1) == DJI_NEO_OK);
    assert(dji_neo_confirm_takeover(n, 1) == DJI_NEO_OK);
    assert(dji_neo_set_stick_enabled(n, 1) == DJI_NEO_OK);
    assert(dji_neo_set_stick(n, 100, 0, 0, 0, 2) == DJI_NEO_OK);
    assert(dji_neo_poll(n, 5) == DJI_NEO_OK);
    signs = s.io.signs; s.io.signer_ready = s.expect_signed = 0;
    assert(dji_neo_poll(n, 60) == DJI_NEO_OK && s.io.signs == signs);
    s.io.signer_ready = s.expect_signed = 1;
    len = dji_neo_build_gimbal_rate(f, sizeof f, 0, -36);
    assert(dji_neo_send_actuation(n, f, (size_t)len) == DJI_NEO_ESTATE);
    assert(dji_neo_poll(n, 65) == DJI_NEO_OK && s.io.signs > signs);
    dji_neo_destroy(n);

    /* Rejected type-5 activation cannot suppress unrelated type-4 liveness. */
    memset(&s, 0, sizeof s); s.io.signer_ready = s.expect_signed = s.io.reject_sign = 1;
    n = create(&s, 1);
    assert(dji_neo_poll(n, 21) == DJI_NEO_EAUTH);
    assert(s.keepalives == 1 && s.type5 == 0 && s.io.signs == 1);
    assert(dji_neo_poll(n, 41) == DJI_NEO_EAUTH);
    assert(s.keepalives == 2 && s.type5 == 0 && s.io.signs == 2);
    dji_neo_destroy(n);
    return 0;
}
