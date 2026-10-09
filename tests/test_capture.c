#include "support.h"
#include <stdio.h>
#include <stdlib.h>

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
int main(void) {
    FILE *file = fopen(CAPTURE_PATH, "rb"); assert(file);
    uint8_t global[24]; assert(fread(global, 1, sizeof global, file) == sizeof global);
    assert(le32(global) == 0xa1b2c3d4 && le32(global + 20) == 101); /* raw IPv4 */
    unsigned checked[6] = {0}, skipped = 0;
    uint8_t record[16];
    while (fread(record, 1, sizeof record, file) == sizeof record) {
        uint32_t size = le32(record + 8); assert(size <= 65535);
        uint8_t *ip = malloc(size ? size : 1); assert(ip);
        assert(fread(ip, 1, size, file) == size);
        if (size < 28 || ip[0] >> 4 != 4 || ip[9] != 17) { free(ip); continue; }
        size_t h = (ip[0] & 15) * 4;
        if (h < 20 || h + 8 > size) { free(ip); continue; }
        const uint8_t *udp = ip + h;
        unsigned port = ((unsigned)udp[2] << 8) | udp[3];
        if (port != 9003) { free(ip); continue; }
        size_t un = ((size_t)udp[4] << 8) | udp[5];
        if (un < 41 || h + un > size) { free(ip); continue; }
        const uint8_t *packet = udp + 8;
        if (packet[6] != 5) { free(ip); continue; }
        const uint8_t *f = packet + 20; size_t len = un - 28;
        if (!dji_neo_duml_valid(f, len)) { ++skipped; free(ip); continue; }
        uint8_t built[DJI_NEO_MAX_DUML]; int got = -1, index = -1;
        uint16_t seq = test_le16(f + 6);
        if (f[9] == 4 && f[10] == 0x0c && len == 21) {
            got = dji_neo_build_gimbal_rate(built, sizeof built, seq, (int16_t)test_le16(f + 15)); index = 0;
        } else if (f[9] == 4 && f[10] == 0x10 && (len == 25 || len == 14)) {
            got = dji_neo_build_gimbal_enable(built, sizeof built, seq, len == 25 ? 0 : 1); index = 1;
        } else if (f[9] == 4 && f[10] == 0x12 && len == 16) {
            got = dji_neo_build_gimbal_keepalive(built, sizeof built, seq); index = 2;
        } else if (f[9] == 0x18 && f[10] == 0x47 && len == 23) {
            got = dji_neo_build_liveview_ex(built, sizeof built, seq, test_le16(f + 13), f[15], f[17]); index = 3;
        } else if (f[9] == 1 && f[10] == 0x0a && len == 41) {
            uint64_t packed = 0; uint16_t ch[4];
            for (int i = 5; i >= 0; --i) packed = (packed << 8) | f[14 + i];
            for (unsigned i = 0; i < 4; ++i) ch[i] = (uint16_t)((packed >> (11*i)) & 0x7ff);
            got = dji_neo_build_stick(built, sizeof built, seq, ch, le32(f + 31)); index = 4;
        } else if (len == 13 && f[4] == 2 && f[5] == 0x0e && f[8] == 0x40 && f[9] == 0 && f[10] == 1) {
            got = dji_neo_build_heartbeat(built, sizeof built, seq); index = 5;
        }
        if (index >= 0) {
            if (got != (int)len || memcmp(built, f, len)) {
                fprintf(stderr, "capture mismatch builder=%d seq=%04x len=%zu\n", index, seq, len);
                abort();
            }
            ++checked[index];
            assert(dji_neo_duml_reseq(built, len, (uint16_t)(seq + 1)) == DJI_NEO_OK);
            assert(dji_neo_duml_valid(built, len));
        }
        free(ip);
    }
    assert(!ferror(file)); fclose(file);
    for (unsigned i = 0; i < 6; ++i) assert(checked[i] > 0);
    printf("byte-identical capture builders: gimbal=%u enable=%u control-KA=%u liveview=%u sticks=%u heartbeat=%u; invalid DUML skipped=%u\n",
        checked[0], checked[1], checked[2], checked[3], checked[4], checked[5], skipped);
    return 0;
}
