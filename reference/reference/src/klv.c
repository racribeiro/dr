#include "klv.h"
#include <math.h>
#include <string.h>

const uint8_t KLV_0601_UL[KLV_UL_LEN] = {
    0x06, 0x0E, 0x2B, 0x34, 0x02, 0x0B, 0x01, 0x01,
    0x0E, 0x01, 0x03, 0x01, 0x01, 0x00, 0x00, 0x00};

static double clampd(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }

static uint8_t *put_be(uint8_t *p, uint64_t v, int n) {
    for (int i = n - 1; i >= 0; i--) *p++ = (uint8_t)(v >> (8 * i));
    return p;
}

/* BER length: short form <128, else 0x81/0x82 long form. */
static size_t ber_put(uint8_t *p, size_t v) {
    if (v < 128) { p[0] = (uint8_t)v; return 1; }
    if (v < 256) { p[0] = 0x81; p[1] = (uint8_t)v; return 2; }
    p[0] = 0x82; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)v; return 3;
}

uint16_t klv_checksum(const uint8_t *buf, size_t len) {
    uint16_t bcc = 0;
    for (size_t i = 0; i < len; i++) bcc = (uint16_t)(bcc + ((uint16_t)buf[i] << (8 * ((i + 1) & 1))));
    return bcc;
}

/* Linear map [lo,hi] -> unsigned [0,max] (ST 0601 uint mapping). */
static uint32_t map_u(double v, double lo, double hi, double max) {
    return (uint32_t)llround((clampd(v, lo, hi) - lo) * max / (hi - lo));
}
/* Linear map [-r,+r] -> signed [-max,+max]; 0x8000/0x80000000 stay "invalid". */
static int64_t map_s(double v, double r, double max) {
    return llround(clampd(v, -r, r) * max / r);
}

size_t klv_encode_0601(const flightdata_t *fd, uint64_t ts_us, uint8_t *out, size_t cap) {
    if (!out) return 0;
    uint8_t val[64];                    /* value area; max ~50 bytes */
    uint8_t *p = val;

    /* Tag 2: Precision Time Stamp, uint64 microseconds since 1970-01-01 UTC. */
    *p++ = 2; *p++ = 8; p = put_be(p, ts_us, 8);
    if (fd && fd->valid) {
        /* Tag 5: Platform Heading Angle, uint16, 0..360 deg -> 0..65535. */
        *p++ = 5;  *p++ = 2; p = put_be(p, map_u(fmod(fmod(fd->yaw_deg, 360.0) + 360.0, 360.0), 0, 360, 65535.0), 2);
        /* Tag 6: Platform Pitch Angle, int16, +/-20 deg -> +/-32767 (0x8000 = invalid). */
        *p++ = 6;  *p++ = 2; p = put_be(p, (uint16_t)(int16_t)map_s(fd->pitch_deg, 20.0, 32767.0), 2);
        /* Tag 7: Platform Roll Angle, int16, +/-50 deg -> +/-32767 (0x8000 = invalid). */
        *p++ = 7;  *p++ = 2; p = put_be(p, (uint16_t)(int16_t)map_s(fd->roll_deg, 50.0, 32767.0), 2);
        /* Tag 13: Sensor Latitude, int32, +/-90 deg -> +/-(2^31-1). */
        *p++ = 13; *p++ = 4; p = put_be(p, (uint32_t)(int32_t)map_s(fd->lat, 90.0, 2147483647.0), 4);
        /* Tag 14: Sensor Longitude, int32, +/-180 deg -> +/-(2^31-1). */
        *p++ = 14; *p++ = 4; p = put_be(p, (uint32_t)(int32_t)map_s(fd->lon, 180.0, 2147483647.0), 4);
        /* Tag 15: Sensor True Altitude (MSL), uint16, -900..19000 m -> 0..65535. */
        *p++ = 15; *p++ = 2; p = put_be(p, map_u(fd->alt_m, -900.0, 19000.0, 65535.0), 2);
    }
    /* Tag 65: UAS Datalink LS version number, uint8 (17 = ST 0601.17). */
    *p++ = 65; *p++ = 1; *p++ = 17;
    /* Tag 1: Checksum (value filled below), must be last. */
    *p++ = 1; *p++ = 2;
    size_t vlen = (size_t)(p - val) + 2;          /* + 2 checksum bytes */

    uint8_t lenb[3];
    size_t ll = ber_put(lenb, vlen);
    size_t total = KLV_UL_LEN + ll + vlen;
    if (cap < total) return 0;

    memcpy(out, KLV_0601_UL, KLV_UL_LEN);
    memcpy(out + KLV_UL_LEN, lenb, ll);
    memcpy(out + KLV_UL_LEN + ll, val, vlen - 2);
    uint16_t ck = klv_checksum(out, total - 2);   /* through the checksum length byte */
    out[total - 2] = (uint8_t)(ck >> 8);
    out[total - 1] = (uint8_t)ck;
    return total;
}

const uint8_t *klv_find(const uint8_t *set, size_t len, uint8_t tag, size_t *vlen) {
    if (!set || len < KLV_UL_LEN + 1 || memcmp(set, KLV_0601_UL, KLV_UL_LEN) != 0) return NULL;
    size_t o = KLV_UL_LEN, end;
    if (set[o] < 128) { end = set[o] + o + 1; o += 1; }
    else if (set[o] == 0x81) { end = o + 2 + set[o + 1]; o += 2; }
    else if (set[o] == 0x82) { end = o + 3 + ((size_t)set[o + 1] << 8 | set[o + 2]); o += 3; }
    else return NULL;
    if (end > len) return NULL;
    while (o + 2 <= end) {
        uint8_t t = set[o], l = set[o + 1];          /* single-byte lengths only */
        if (o + 2 + l > end) return NULL;
        if (t == tag) { if (vlen) *vlen = l; return set + o + 2; }
        o += 2 + (size_t)l;
    }
    return NULL;
}
