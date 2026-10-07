#include "neotlm.h"
#include "net.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static double rd_f64le(const uint8_t *p) {
    uint64_t u = 0;
    for (int i = 7; i >= 0; i--) u = (u << 8) | p[i];
    double d;
    memcpy(&d, &u, sizeof d);
    return d;
}

static int rd_s16le(const uint8_t *p) {
    return (int16_t)(uint16_t)(p[0] | (p[1] << 8));
}

int neotlm_decode_osd(const uint8_t *payload, size_t len, flightdata_t *out) {
    if (!payload || !out || len < 30) return 0;

    /* PROVEN: lon f64le @0, lat f64le @8, both in radians. */
    double lon = rd_f64le(payload + 0);
    double lat = rd_f64le(payload + 8);
    if (!isfinite(lon) || !isfinite(lat)) return 0;
    if (fabs(lon) > M_PI + 1e-6 || fabs(lat) > M_PI / 2 + 1e-6) return 0;

    flightdata_t fd;
    memset(&fd, 0, sizeof fd);
    fd.lon = lon * 180.0 / M_PI;
    fd.lat = lat * 180.0 / M_PI;
    fd.valid = (lon != 0.0 && lat != 0.0) ? 1 : 0;
    fd.att_valid = 1;   /* OSD decoded -> attitude/heading usable (no GPS needed) */

    /* CONFIRMED from a GPS-fix flight capture (vpn-1791162968184.pcap,
     * REVERSE_ENGINEERING.md §12.16): canonical DJI OSD layout — s16le/10 deg.
     * With the craft still on the ground: yaw steady ~47.5, pitch/roll ~0; all
     * three swing during pre-arm handling, exactly as expected. yaw = heading. */
    fd.pitch_deg = (float)rd_s16le(payload + 24) / 10.0f;  /* @24 CONFIRMED */
    fd.roll_deg  = (float)rd_s16le(payload + 26) / 10.0f;  /* @26 CONFIRMED */
    float yaw    = (float)rd_s16le(payload + 28) / 10.0f;  /* @28 CONFIRMED (heading) */
    yaw = fmodf(yaw, 360.0f);
    if (yaw < 0) yaw += 360.0f;
    fd.yaw_deg = yaw;

    /* @16 = relative height s16le in 0.1 m (rose 0->7 on a small lift in the
     * capture). Canonical slot between lat/lon and the velocity triple
     * (@18 vx, @20 vy, @22 vz, 0.1 m/s — not surfaced in flightdata_t). */
    if (len >= 18) fd.alt_m = (double)rd_s16le(payload + 16) / 10.0;
    else fd.alt_m = 0.0;
    /* @36 = GNSS satellite count (u8). CONFIRMED from the GPS-fix capture
     * (vpn-1791162968184.pcap): median 3 with no fix, 8..16 once locked. */
    fd.sats = (len > 36) ? (int)payload[36] : -1;

    *out = fd;
    return 1;
}

int neotlm_decode_frame(const uint8_t *frame, size_t len, flightdata_t *out) {
    if (!frame || len < 11 + 2 + 30) return 0;
    if (frame[0] != 0x55 || frame[9] != 0x03 || frame[10] != 0x43) return 0;
    return neotlm_decode_osd(frame + 11, len - 11 - 2, out);
}

#define NEO_DRONE_IP  0xC0A80201u   /* 192.168.2.1 */
#define NEO_UDP_PORT  9003

int neotlm_decode_ipv4(const uint8_t *pkt, size_t len, flightdata_t *out) {
    if (!pkt || !out) return 0;
    net_ipv4_t ip;
    if (net_ipv4_parse(pkt, len, &ip) != 0) return 0;
    if (ip.protocol != 17 /* UDP */ || ip.src != NEO_DRONE_IP) return 0;
    const uint8_t *seg = pkt + ip.hdr_len;
    size_t seglen = len - ip.hdr_len;
    net_udp_t udp;
    if (net_udp_parse(seg, seglen, &udp) != 0) return 0;
    if (udp.sport != NEO_UDP_PORT) return 0;
    if (udp.length < 8 + 8) return 0;               /* UDP hdr + wrapper */
    const uint8_t *pl = seg + 8;                    /* UDP payload */
    size_t pllen = (size_t)udp.length - 8;
    if (pllen > seglen - 8) pllen = seglen - 8;     /* clamp to captured bytes */
    if (pllen < 8) return 0;
    if (pl[6] != 1) return 0;                       /* wrapper type 1 = telemetry */
    const uint8_t *body = pl + 8;
    size_t bodylen = pllen - 8;
    /* Scan the wrapper body for an OSD 0x03/0x43 DUML frame. Video (type 2) is
     * filtered out above, so this runs only on the small telemetry packets. */
    for (size_t i = 0; i + 13 <= bodylen; i++) {
        if (body[i] != 0x55) continue;
        size_t flen = body[i + 1] | ((size_t)(body[i + 2] & 0x03) << 8);
        if (flen < 13 || i + flen > bodylen) continue;
        if (body[i + 9] == 0x03 && body[i + 10] == 0x43) {
            if (neotlm_decode_frame(body + i, flen, out)) return 1;
        }
        i += flen - 1;
    }
    return 0;
}
