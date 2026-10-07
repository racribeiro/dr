#include "zpub_msg.h"
#include "pbenc.h"
#include <math.h>
#include <string.h>
#include <time.h>

#define ZMSG_D2R (3.14159265358979323846 / 180.0)

size_t zmsg_geo_position(const flightdata_t *fd, uint8_t *out, size_t cap) {
    pbw_t w; pb_init(&w, out, cap);
    pb_double(&w, 1, fd->lat);
    pb_double(&w, 2, fd->lon);
    pb_double(&w, 3, fd->alt_m);
    /* velocity (field 4) omitted: not decoded from the DJI OSD yet. */
    return w.err ? 0 : w.len;
}

size_t zmsg_attitude(const flightdata_t *fd, uint8_t *out, size_t cap) {
    pbw_t w; pb_init(&w, out, cap);
    pb_float(&w, 1, fd->roll_deg);
    pb_float(&w, 2, fd->pitch_deg);
    pb_float(&w, 3, fd->yaw_deg);
    return w.err ? 0 : w.len;
}

size_t zmsg_wrap(int64_t time_enc_us, const uint8_t *payload, size_t n,
                 uint8_t *out, size_t cap) {
    pbw_t w; pb_init(&w, out, cap);
    pb_int64(&w, 1, time_enc_us);
    pb_bytes(&w, 2, payload, n);
    return w.err ? 0 : w.len;
}

int64_t zmsg_time_enc(int64_t unix_us) {
    time_t secs = (time_t)(unix_us / 1000000);
    int ms = (int)((unix_us % 1000000) / 1000);
    struct tm tmv;
    gmtime_r(&secs, &tmv);
    int64_t v = (int64_t)(tmv.tm_year + 1900);
    v = v * 100 + (tmv.tm_mon + 1);
    v = v * 100 + tmv.tm_mday;
    v = v * 100 + tmv.tm_hour;
    v = v * 100 + tmv.tm_min;
    v = v * 100 + tmv.tm_sec;
    v = v * 1000 + ms;           /* yyyyMMddHHmmssfff (17 digits) */
    return v;
}

size_t zmsg_aircraft_state(const flightdata_t *fd, const char *aircraft_id,
                           int64_t time_enc, uint8_t *out, size_t cap) {
    if (!fd || !aircraft_id || !out) return 0;
    /* Location (radians / metres) */
    uint8_t loc[64]; pbw_t wl; pb_init(&wl, loc, sizeof loc);
    pb_double(&wl, 1, fd->lat * ZMSG_D2R);   /* lat_rad */
    pb_double(&wl, 2, fd->lon * ZMSG_D2R);   /* lng_rad */
    pb_double(&wl, 3, fd->alt_m);            /* alt_m */
    if (wl.err) return 0;
    /* EulerAttitude (radians): psi=yaw, theta=pitch, phi=roll */
    uint8_t att[64]; pbw_t wa; pb_init(&wa, att, sizeof att);
    pb_double(&wa, 1, (double)fd->yaw_deg * ZMSG_D2R);
    pb_double(&wa, 2, (double)fd->pitch_deg * ZMSG_D2R);
    pb_double(&wa, 3, (double)fd->roll_deg * ZMSG_D2R);
    if (wa.err) return 0;
    /* AircraftState */
    uint8_t ac[256]; pbw_t wac; pb_init(&wac, ac, sizeof ac);
    pb_bytes(&wac, 1, (const uint8_t *)aircraft_id, strlen(aircraft_id)); /* k_aircraft_id */
    pb_int64(&wac, 2, time_enc);                 /* time_enc (decoded one) */
    pb_bytes(&wac, 3, loc, wl.len);              /* location */
    pb_bytes(&wac, 4, att, wa.len);              /* attitude */
    pb_double(&wac, 6, (double)fd->yaw_deg * ZMSG_D2R);  /* track_rad = heading */
    if (wac.err) return 0;
    /* SystemMessageWrapper */
    pbw_t w; pb_init(&w, out, cap);
    pb_int64(&w, 1, time_enc);                   /* wrapper time_enc */
    pb_bytes(&w, 100, ac, wac.len);              /* aircraft_state */
    return w.err ? 0 : w.len;
}
