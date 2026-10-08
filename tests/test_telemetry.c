#include "support.h"
#include "../reference/tests/test_fixtures.h"
#include <math.h>

typedef struct { unsigned telemetry, video; dji_neo_telemetry_t last; } observed_t;
static void telemetry(void *user, const dji_neo_telemetry_t *t) {
    observed_t *o = user; o->last = *t; ++o->telemetry;
}
static void video(void *user, const dji_neo_video_packet_t *v) {
    observed_t *o = user;
    assert(v->size == 3 && v->data[0] == 1 && v->codec_h265);
    ++o->video;
}
int main(void) {
    observed_t o = {0}; test_io_t io = {.reject_sign=1, .signer_ready=1};
    dji_neo_config_t cfg = {0};
    cfg.session_id = test_le16(OSD_DGRAM + 2); cfg.body_id = 0x707d;
    cfg.udp_send = test_send; cfg.udp_user = &io;
    cfg.on_telemetry = telemetry; cfg.on_video = video; cfg.callback_user = &o;
    dji_neo_t *n = dji_neo_create(&cfg); assert(n);
    dji_neo_signer_t declining = {.sign=test_sign, .user=&io, .ready=test_ready};
    dji_neo_set_signer(n, &declining);
    assert(dji_neo_on_datagram(n, OSD_DGRAM, sizeof OSD_DGRAM, 0) == DJI_NEO_OK);
    assert(o.telemetry == 1 && o.last.attitude_valid && !o.last.gps_valid);
    assert(fabsf(o.last.yaw_deg - 318.9f) < 0.01f && fabsf(o.last.roll_deg - 0.2f) < 0.01f);
    assert(o.last.pitch_deg == 0 && o.last.satellites == 4);
    uint8_t mismatched[sizeof OSD_DGRAM]; memcpy(mismatched, OSD_DGRAM, sizeof mismatched);
    mismatched[0] = 34; mismatched[1] = 0x80; test_checksum(mismatched);
    assert(dji_neo_on_datagram(n, mismatched, sizeof mismatched, 1) == DJI_NEO_OK);
    assert(o.telemetry == 2); /* Scan beyond advisory 26-byte idle prefix. */
    for (size_t i = 8; i + sizeof OSD_DUML <= sizeof mismatched; ++i) {
        if (memcmp(mismatched + i, OSD_DUML, sizeof OSD_DUML) == 0) {
            mismatched[i + 20] ^= 1; break;
        }
    }
    assert(dji_neo_on_datagram(n, mismatched, sizeof mismatched, 2) == DJI_NEO_OK);
    assert(o.telemetry == 2); /* Corrupted inner CRC cannot produce telemetry. */
    for (size_t size = 0; size < sizeof OSD_DGRAM; ++size) {
        /* Exercise truncated wrapper/frame boundaries with increasing time. */
        (void)dji_neo_on_datagram(n, OSD_DGRAM, size, 3);
    }
    uint8_t v[] = {11,0x80,0,0,0,0,2,0,1,2,3}; test_checksum(v);
    assert(dji_neo_on_datagram(n, v, sizeof v, 4) == DJI_NEO_OK && o.video == 1);
    assert(io.signs == 0 && io.sends == 0);
    v[7] ^= 1;
    assert(dji_neo_on_datagram(n, v, sizeof v, 5) == DJI_NEO_EINVAL);
    assert(dji_neo_on_datagram(n, NULL, 0, 5) == DJI_NEO_EINVAL);
    dji_neo_destroy(n);
    return 0;
}
