/* neostick — see neostick.h. */
#include "neostick.h"
#include "duml.h"
#include <string.h>

static uint16_t clampch(uint16_t v) {
    if (v < NEOSTICK_MIN) return NEOSTICK_MIN;
    if (v > NEOSTICK_MAX) return NEOSTICK_MAX;
    return v;
}

int neostick_pack(uint8_t *out, uint16_t roll, uint16_t pitch,
                  uint16_t throttle, uint16_t yaw, uint32_t ms) {
    static const uint8_t HEAD[3] = { 0x01, 0x0d, 0x00 };
    static const uint8_t MID[11] = { 0x40, 0x00, 0x02, 0x00, 0x00, 0x06,
                                     0x55, 0x01, 0x04, 0x56, 0x08 };
    if (!out) return -1;
    memset(out, 0, NEOSTICK_PAYLOAD_LEN);
    memcpy(out, HEAD, sizeof HEAD);
    uint64_t v = (uint64_t)clampch(roll) |
                 ((uint64_t)clampch(pitch) << 11) |
                 ((uint64_t)clampch(throttle) << 22) |
                 ((uint64_t)clampch(yaw) << 33);
    for (int i = 0; i < 6; i++) out[3 + i] = (uint8_t)(v >> (8 * i));
    memcpy(out + 9, MID, sizeof MID);
    out[20] = (uint8_t)ms; out[21] = (uint8_t)(ms >> 8);
    out[22] = (uint8_t)(ms >> 16); out[23] = (uint8_t)(ms >> 24);
    return NEOSTICK_PAYLOAD_LEN;
}

void neostick_unpack(const uint8_t *in, uint16_t ch[4]) {
    uint64_t v = 0;
    for (int i = 0; i < 6; i++) v |= (uint64_t)in[3 + i] << (8 * i);
    for (int i = 0; i < 4; i++) ch[i] = (uint16_t)((v >> (11 * i)) & 0x7ff);
}

uint16_t neostick_from_deflection(int d) {
    if (d > NEOSTICK_RANGE) d = NEOSTICK_RANGE;
    if (d < -NEOSTICK_RANGE) d = -NEOSTICK_RANGE;
    return (uint16_t)(NEOSTICK_CENTER + d);
}

int neostick_build_frame(uint8_t *out, size_t cap, uint16_t seq,
                         uint16_t roll, uint16_t pitch, uint16_t throttle,
                         uint16_t yaw, uint32_t ms) {
    uint8_t pl[NEOSTICK_PAYLOAD_LEN];
    if (!out || cap < DUML_MAX) return -1;
    neostick_pack(pl, roll, pitch, throttle, yaw, ms);
    return duml_build(out, NEOSTICK_SRC, NEOSTICK_DST, seq, 0x00,
                      NEOSTICK_CMD_SET, NEOSTICK_CMD_ID, pl, sizeof pl);
}
