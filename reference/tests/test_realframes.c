/* Real-frame regression test: validates CRC + OSD decode against actual captured
 * DJI Neo frames (fixtures in test_fixtures.h). Covers the decode path that the
 * trivial scaffold test does not. Returns nonzero on failure (CTest-friendly). */
#include "dji_neo/dji_neo.h"
#include "duml.h"
#include "test_fixtures.h"
#include <assert.h>
#include <stdio.h>
#include <math.h>

static int tele_hits;
static dji_neo_telemetry_t last;
static void on_t(void *u, const dji_neo_telemetry_t *t) { (void)u; last = *t; tele_hits++; }
static int send_fn(void *u, const uint8_t *d, size_t n) { (void)u; (void)d; (void)n; return 0; }

int main(void) {
    /* 1. CRC-8 + CRC-16 must accept real DUML frames. */
    assert(dji_neo_duml_valid(OSD_DUML, sizeof OSD_DUML) == 1);
    assert(dji_neo_duml_valid(GIMBAL_DUML, sizeof GIMBAL_DUML) == 1);
    /* a one-byte corruption must be rejected */
    { uint8_t bad[sizeof OSD_DUML]; for (size_t i=0;i<sizeof bad;i++) bad[i]=OSD_DUML[i]; bad[20]^=0x01;
      assert(dji_neo_duml_valid(bad, sizeof bad) == 0); }

    /* 2. A real OSD telemetry datagram must decode to sane attitude. */
    dji_neo_config_t cfg = {0};
    cfg.udp_send = send_fn; cfg.on_telemetry = on_t;
    cfg.session_id = (uint16_t)(OSD_DGRAM[2] | (OSD_DGRAM[3] << 8));
    dji_neo_t *neo = dji_neo_create(&cfg);
    assert(neo);
    assert(dji_neo_on_datagram(neo, OSD_DGRAM, sizeof OSD_DGRAM, 1000) == DJI_NEO_OK);
    assert(tele_hits >= 1);
    assert(last.attitude_valid);
    assert(last.yaw_deg >= 0.0f && last.yaw_deg < 360.0f);
    assert(fabsf(last.roll_deg) <= 180.0f && fabsf(last.pitch_deg) <= 180.0f);
    assert(last.satellites >= -1 && last.satellites <= 40);
    dji_neo_destroy(neo);

    printf("test_realframes OK: duml CRC + OSD decode validated vs real frames "
           "(yaw=%.1f roll=%.1f pitch=%.1f sats=%d)\n",
           last.yaw_deg, last.roll_deg, last.pitch_deg, last.satellites);
    return 0;
}
