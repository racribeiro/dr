/* neocmd — see neocmd.h. */
#include "neocmd.h"
#include "neoconn.h"

#include <string.h>

int neocmd_wrap_duml(uint8_t *out, size_t cap, uint16_t session,
                     const uint8_t *duml, size_t duml_len) {
    if (!out || !duml || duml_len == 0) return -1;
    return neoconn_wrap(out, cap, session, 0, NEOCONN_T_CMD, duml, duml_len);
}

int neocmd_wrap_rc(uint8_t *out, size_t cap, uint16_t session, uint16_t wrap_f45,
                   uint16_t body_id, uint8_t sub_ctr,
                   const uint8_t *duml, size_t duml_len) {
    if (!out || !duml || duml_len == 0) return -1;
    uint8_t body[12 + NEOCONN_MAX_FRAME];
    size_t body_len = 12 + duml_len;
    if (body_len > sizeof body) return -1;
    body[0] = (uint8_t)(body_id >> 8);   /* body_id, big-endian (== CONNECT body[0:2]) */
    body[1] = (uint8_t)body_id;
    body[2] = (uint8_t)wrap_f45;         /* f45, little-endian (== wrapper f45) */
    body[3] = (uint8_t)(wrap_f45 >> 8);
    body[4] = body[5] = body[6] = body[7] = 0;
    body[8] = sub_ctr;                   /* +1 per uplink frame */
    body[9] = 0x01;
    body[10] = 0x60;
    body[11] = 0x00;                     /* flag: 0x00 throughout the gimbal capture */
    memcpy(body + 12, duml, duml_len);
    return neoconn_wrap(out, cap, session, wrap_f45, NEOCONN_T_CMD, body, body_len);
}
