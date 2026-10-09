#include "support.h"

int main(void) {
    test_io_t io = {0};
    dji_neo_t *n = test_client(&io, 1);
    uint8_t frame[32];
    int length = dji_neo_build_gimbal_rate(frame, sizeof frame, 0x9e3c, -36);
    assert(length == 21);
    assert(dji_neo_send_actuation(n, frame, (size_t)length) == DJI_NEO_ESTATE);
    assert(dji_neo_poll(n, 0) == DJI_NEO_OK && io.sends == 0);
    test_connect(n); test_wire(io.last, io.size);
    assert(io.last[6] == 0 && io.size == 48);
    dji_neo_capabilities_t caps; dji_neo_get_capabilities(n, &caps);
    assert(caps.has_link && !caps.has_actuation);
    assert(dji_neo_set_command_armed(n, 1) == DJI_NEO_EAUTH);
    dji_neo_signer_t noop = {.sign=test_sign, .user=&io};
    dji_neo_set_signer(n, &noop);
    dji_neo_get_capabilities(n, &caps); assert(!caps.has_actuation);
    unsigned before = io.sends;
    assert(dji_neo_send_query(n, frame, (size_t)length) == DJI_NEO_EINVAL);
    assert(io.sends == before && io.signs == 0);

    assert(dji_neo_poll(n, 21) == DJI_NEO_OK);
    static const uint8_t expected[] = {
        0x70,0x7d,0x70,0x7d,0,0,0,0,0x70,0x7d,0x70,0x7d,0,0,0,0,
        0x70,0x7d,0x70,0x7d,0,0,0,0,0,0
    };
    assert(io.size == 34 && memcmp(io.last + 8, expected, sizeof expected) == 0);
    test_wire(io.last, io.size);
    int hb = dji_neo_build_heartbeat(frame, sizeof frame, 0);
    assert(dji_neo_send_query(n, frame, (size_t)hb) == DJI_NEO_OK);
    assert(test_le16(io.last + 4) == TEST_FIRST_F45 && io.last[16] == 1);
    assert(dji_neo_send_query(n, frame, (size_t)hb) == DJI_NEO_OK);
    assert(test_le16(io.last + 4) == TEST_FIRST_F45 + 8 && io.last[16] == 2);
    assert(dji_neo_poll(n, 41) == DJI_NEO_OK);
    assert(io.last[6] == 4 && test_le16(io.last + 4) == 0);
    assert(test_le16(io.last + 26) == TEST_FIRST_F45 + 8); /* KA body offset 18 */
    /* Both counter widths wrap naturally; interleaved KA must not count. */
    for (unsigned i = 2; i <= 8192; ++i) {
        assert(dji_neo_send_query(n, frame, (size_t)hb) == DJI_NEO_OK);
        assert(test_le16(io.last + 4) == (uint16_t)(TEST_FIRST_F45 + i * 8));
        assert(io.last[16] == (uint8_t)(i + 1));
    }
    assert(dji_neo_poll(n, 40) == DJI_NEO_EINVAL);
    test_arm_all(n, &io);
    length = dji_neo_build_gimbal_rate(frame, sizeof frame, 7, -36);
    before = io.sends; io.reject_sign = 1;
    assert(dji_neo_send_actuation(n, frame, (size_t)length) == DJI_NEO_EAUTH);
    assert(io.sends == before);
    io.reject_sign = 0; io.corrupt_identity = 1;
    assert(dji_neo_send_actuation(n, frame, (size_t)length) == DJI_NEO_EAUTH);
    assert(io.sends == before);
    io.corrupt_identity = 0; io.fail_send = 1;
    assert(dji_neo_send_actuation(n, frame, (size_t)length) == DJI_NEO_EIO);
    assert(io.sends == before);
    assert(dji_neo_send_actuation(n, frame, (size_t)length) == DJI_NEO_OK);
    assert(test_le16(io.last + 4) == TEST_FIRST_F45 + 8 && io.last[16] == 2 && io.last[19] == 0xa5);
    io.signer_ready = 0;
    assert(dji_neo_send_actuation(n, frame, (size_t)length) == DJI_NEO_EAUTH);
    io.signer_ready = 1;
    assert(dji_neo_send_actuation(n, frame, (size_t)length) == DJI_NEO_ESTATE);
    assert(dji_neo_poll(n, 2001) == DJI_NEO_OK);
    assert(dji_neo_link_state(n) == DJI_NEO_LINK_LOST);
    before = io.sends;
    assert(dji_neo_poll(n, 3000) == DJI_NEO_OK && io.sends == before);
    assert(dji_neo_set_session_armed(n, 1) == DJI_NEO_ESTATE);
    assert(dji_neo_reset_session(n, 0x4fb0, 0x707d) == DJI_NEO_EINVAL);
    assert(dji_neo_reset_session(n, 0x4fb1, 0x707e) == DJI_NEO_OK);
    assert(dji_neo_set_session_armed(n, 1) == DJI_NEO_OK);
    assert(dji_neo_poll(n, 3001) == DJI_NEO_OK);
    assert(test_le16(io.last + 2) == 0x4fb1 && io.last[8] == 0x70 && io.last[9] == 0x7e);
    dji_neo_destroy(n);

    /* Retry is 1 s; CONNECT never consumes the seeded command f45. */
    memset(&io, 0, sizeof io); n = test_client(&io, 1);
    assert(dji_neo_set_session_armed(n, 1) == DJI_NEO_OK);
    assert(dji_neo_poll(n, 0) == DJI_NEO_OK && io.sends == 1);
    assert(dji_neo_poll(n, 999) == DJI_NEO_OK && io.sends == 1);
    assert(dji_neo_poll(n, 1000) == DJI_NEO_OK && io.sends == 2);
    test_accept(n, 1001);
    hb = dji_neo_build_heartbeat(frame, sizeof frame, 0);
    assert(dji_neo_send_query(n, frame, (size_t)hb) == DJI_NEO_OK);
    assert(test_le16(io.last + 4) == TEST_FIRST_F45);
    dji_neo_destroy(n);
    return 0;
}
