#include "duml.h"
#include "dji_neo/dji_neo.h"

static uint8_t reflect8(uint8_t value) {
    uint8_t out = 0;
    for (unsigned i = 0; i < 8; ++i) { out = (uint8_t)((out << 1) | (value & 1)); value >>= 1; }
    return out;
}
static uint16_t reflect16(uint16_t value) {
    uint16_t out = 0;
    for (unsigned i = 0; i < 16; ++i) { out = (uint16_t)((out << 1) | (value & 1)); value >>= 1; }
    return out;
}
static uint8_t crc8(const uint8_t *data, size_t size) {
    uint8_t crc = 0x50;
    for (size_t i = 0; i < size; ++i) {
        crc ^= reflect8(data[i]);
        for (unsigned bit = 0; bit < 8; ++bit) crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
    }
    return (uint8_t)(reflect8(crc) ^ 0xff);
}
static uint16_t crc16(const uint8_t *data, size_t size) {
    uint16_t crc = 0x496c;
    for (size_t i = 0; i < size; ++i) {
        crc ^= (uint16_t)reflect8(data[i]) << 8;
        for (unsigned bit = 0; bit < 8; ++bit) crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return reflect16(crc);
}
int dji_neo_duml_valid(const uint8_t *frame, size_t size) {
    if (!frame || size < 13 || size > DJI_NEO_MAX_DUML || frame[0] != 0x55) return 0;
    size_t encoded = (size_t)frame[1] | ((size_t)(frame[2] & 3) << 8);
    if (encoded != size || crc8(frame, 3) != frame[3]) return 0;
    uint16_t got = (uint16_t)frame[size - 2] | ((uint16_t)frame[size - 1] << 8);
    return crc16(frame, size - 2) == got;
}
