/* duml — see duml.h. CRC params proven against a live capture. */
#include "duml.h"

#include <string.h>

static uint8_t reflect8(uint8_t x) {
    uint8_t r = 0;
    for (int i = 0; i < 8; i++) {
        r = (uint8_t)((r << 1) | (x & 1));
        x >>= 1;
    }
    return r;
}

static uint16_t reflect16(uint16_t x) {
    uint16_t r = 0;
    for (int i = 0; i < 16; i++) {
        r = (uint16_t)((r << 1) | (x & 1));
        x >>= 1;
    }
    return r;
}

/* CRC-8: poly 0x31, init 0x50, refin, refout, xorout 0xff. */
static uint8_t crc8(const uint8_t *buf, size_t len) {
    uint8_t c = 0x50;
    for (size_t i = 0; i < len; i++) {
        c ^= reflect8(buf[i]);
        for (int b = 0; b < 8; b++) {
            c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x31) : (uint8_t)(c << 1);
        }
    }
    return reflect8(c) ^ 0xff;
}

/* CRC-16: poly 0x1021, init 0x496c, refin, refout, xorout 0x0. */
static uint16_t crc16(const uint8_t *buf, size_t len) {
    uint16_t c = 0x496c;
    for (size_t i = 0; i < len; i++) {
        c ^= (uint16_t)reflect8(buf[i]) << 8;
        for (int b = 0; b < 8; b++) {
            c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021)
                             : (uint16_t)(c << 1);
        }
    }
    return reflect16(c);
}

int duml_build(uint8_t *out, uint8_t src, uint8_t dst, uint16_t seq,
               uint8_t cmd_type, uint8_t cmd_set, uint8_t cmd_id,
               const uint8_t *payload, size_t payload_len) {
    size_t length = 13 + payload_len;
    if (length > DUML_MAX) {
        return -1;
    }
    out[0] = DUML_SOF;
    out[1] = (uint8_t)(length & 0xFF);
    /* version 1 in high 6 bits, length high 2 bits in low 2 bits. */
    out[2] = (uint8_t)((1 << 2) | ((length >> 8) & 0x03));
    out[3] = crc8(out, 3);
    out[4] = src;
    out[5] = dst;
    out[6] = (uint8_t)(seq & 0xFF);
    out[7] = (uint8_t)((seq >> 8) & 0xFF);
    out[8] = cmd_type;
    out[9] = cmd_set;
    out[10] = cmd_id;
    if (payload_len > 0 && payload != NULL) {
        memcpy(out + 11, payload, payload_len);
    }
    uint16_t c = crc16(out, length - 2);
    out[length - 2] = (uint8_t)(c & 0xFF);
    out[length - 1] = (uint8_t)((c >> 8) & 0xFF);
    return (int)length;
}

int duml_reseq(uint8_t *f, size_t len, uint16_t seq) {
    if (f == NULL || len < 13 || f[0] != DUML_SOF) return -1;
    f[6] = (uint8_t)(seq & 0xFF);
    f[7] = (uint8_t)((seq >> 8) & 0xFF);
    uint16_t c = crc16(f, len - 2);
    f[len - 2] = (uint8_t)(c & 0xFF);
    f[len - 1] = (uint8_t)((c >> 8) & 0xFF);
    return 0;
}

int duml_check_header(const uint8_t *f) {
    return f[0] == DUML_SOF && crc8(f, 3) == f[3];
}

int duml_check(const uint8_t *f, size_t len) {
    if (f == NULL || len < 13 || len > DUML_MAX || f[0] != DUML_SOF) return 0;
    size_t fl = (size_t)(f[1] | ((f[2] & 0x03) << 8));
    if (fl != len || crc8(f, 3) != f[3]) return 0;
    uint16_t c = crc16(f, len - 2);
    return f[len - 2] == (uint8_t)(c & 0xFF) && f[len - 1] == (uint8_t)(c >> 8);
}

void duml_hex(const uint8_t *in, size_t len, char *out) {
    static const char H[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        out[i * 2] = H[(in[i] >> 4) & 0xF];
        out[i * 2 + 1] = H[in[i] & 0xF];
    }
    out[len * 2] = '\0';
}
