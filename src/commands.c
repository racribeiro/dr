#include "dji_neo/commands.h"
#include <string.h>

int dji_neo_build_heartbeat(uint8_t *out, size_t cap, uint16_t seq) {
    return dji_neo_duml_build(out, cap, 2, 0x0e, seq, 0x40, 0, 1, NULL, 0);
}

int dji_neo_build_liveview(uint8_t *out, size_t cap, uint16_t seq,
                           uint16_t timer, int edge) {
    return dji_neo_build_liveview_ex(out, cap, seq, timer, 0x1a, edge);
}

int dji_neo_build_liveview_ex(uint8_t *out, size_t cap, uint16_t seq,
                              uint16_t timer, uint8_t token, int edge) {
    if (edge != 0 && edge != 1) return DJI_NEO_EINVAL;
    uint8_t p[10] = {0, 8, (uint8_t)timer, (uint8_t)(timer >> 8), token, 0,
                     (uint8_t)edge, 0, 0, 0};
    return dji_neo_duml_build(out, cap, 2, 0xe9, seq, 0, 0x18, 0x47, p, sizeof p);
}

int dji_neo_build_stick(uint8_t *out, size_t cap, uint16_t seq,
                        const uint16_t ch[4], uint32_t timer) {
    if (!ch) return DJI_NEO_EINVAL;
    uint8_t p[28] = {1, 0x0d, 0};
    const uint8_t middle[] = {0x40,0,2,0,0,6,0x55,1,4,0x56,8};
    uint64_t packed = 0;
    for (unsigned i = 0; i < 4; ++i) {
        if (ch[i] < 364 || ch[i] > 1684) return DJI_NEO_EINVAL;
        packed |= (uint64_t)ch[i] << (11 * i);
    }
    for (unsigned i = 0; i < 6; ++i) p[3 + i] = (uint8_t)(packed >> (8 * i));
    memcpy(p + 9, middle, sizeof middle);
    for (unsigned i = 0; i < 4; ++i) p[20 + i] = (uint8_t)(timer >> (8 * i));
    return dji_neo_duml_build(out, cap, 2, 0xa9, seq, 0, 1, 0x0a, p, sizeof p);
}

int dji_neo_build_gimbal_rate(uint8_t *out, size_t cap, uint16_t seq, int rate) {
    if (rate < -200 || rate > 200) return DJI_NEO_EINVAL;
    uint16_t wire = (uint16_t)rate;
    uint8_t p[8] = {0,0,0,0,(uint8_t)wire,(uint8_t)(wire >> 8),0x80,0};
    return dji_neo_duml_build(out, cap, 2, 4, seq, 0, 4, 0x0c, p, sizeof p);
}

int dji_neo_build_gimbal_enable(uint8_t *out, size_t cap, uint16_t seq,
                                unsigned stage) {
    static const uint8_t p[] = {6,7,0x26,8,9,0x27,0x29,0x2a,0x2b,0x1a,0x1b,0x28};
    static const uint8_t short_p[] = {0x0a};
    if (stage > 3) return DJI_NEO_EINVAL;
    return dji_neo_duml_build(out, cap, 2, 4, seq, 0x40, 4, 0x10,
                              stage ? short_p : p, stage ? sizeof short_p : sizeof p);
}

int dji_neo_build_gimbal_keepalive(uint8_t *out, size_t cap, uint16_t seq) {
    static const uint8_t p[] = {0xe6,1,0x48};
    return dji_neo_duml_build(out, cap, 2, 4, seq, 0x40, 4, 0x12, p, sizeof p);
}
