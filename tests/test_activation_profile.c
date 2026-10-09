#include "support.h"
#include <stdio.h>
#include <stdlib.h>

enum { SOURCE_COUNT = 55, PASSIVE_COUNT = 51 };
typedef struct {
    uint8_t data[DJI_NEO_MAX_DUML];
    size_t size;
} frame_t;
typedef struct {
    test_io_t io;
    frame_t expected[PASSIVE_COUNT];
    size_t index;
    unsigned rounds, type5, views, heartbeat, sticks;
    uint64_t now, heartbeat_at;
    int signing, fail_profile_once, failure_seen;
} profile_io_t;

static int excluded(unsigned set, unsigned id) {
    return set == 1 || set == 4 || (set == 0x18 && id == 0x47) ||
           (set == 7 && (id == 7 || id == 0x0c || id == 0x0e));
}
static unsigned hex_digit(char digit) {
    if (digit >= '0' && digit <= '9') return (unsigned)(digit - '0');
    if (digit >= 'a' && digit <= 'f') return (unsigned)(digit - 'a' + 10);
    assert(!"invalid fixture hex"); return 0;
}
static void load_profile(profile_io_t *p) {
    FILE *file = fopen(PROFILE_PATH, "r"); assert(file);
    char line[2200], hex[2047]; unsigned source_count = 0, retained = 0;
    while (fgets(line, sizeof line, file)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        unsigned index, set, id;
        assert(sscanf(line, "%u %x/%x %2046s", &index, &set, &id, hex) == 4);
        assert(index == source_count++ && set != 1 && set != 4);
        size_t size = strlen(hex) / 2; assert(size <= DJI_NEO_MAX_DUML && strlen(hex) == 2*size);
        uint8_t frame[DJI_NEO_MAX_DUML];
        for (size_t i = 0; i < size; ++i)
            frame[i] = (uint8_t)((hex_digit(hex[2*i]) << 4) | hex_digit(hex[2*i+1]));
        assert(dji_neo_duml_valid(frame, size));
        assert(frame[9] == set && frame[10] == id);
        if (excluded(set, id)) continue;
        assert(retained < PASSIVE_COUNT);
        p->expected[retained].size = size;
        memcpy(p->expected[retained++].data, frame, size);
    }
    assert(!ferror(file)); fclose(file);
    assert(source_count == SOURCE_COUNT && retained == PASSIVE_COUNT);
    assert(p->expected[0].data[9] == 0 && p->expected[0].data[10] == 1);
}
static int capture_profile(void *user, const uint8_t *packet, size_t size) {
    profile_io_t *p = user;
    test_wire(packet, size);
    if (packet[6] != 5) return test_send(&p->io, packet, size);
    assert(test_le16(packet + 4) == (uint16_t)(TEST_FIRST_F45 + p->type5 * 8));
    assert(packet[16] == (uint8_t)(p->type5 + 1));
    assert(packet[19] == (p->signing ? 0xa5 : 0));
    const uint8_t *frame = packet + 20;
    assert(test_le16(frame + 6) == (uint16_t)p->type5);
    unsigned set = frame[9], id = frame[10];
    int is_profile = 1;
    if (set == 0 && id == 1 && p->now >= p->heartbeat_at) {
        p->heartbeat_at = p->now + 150; ++p->heartbeat; is_profile = 0;
    } else if (set == 1 && id == 0x0a) {
        static const uint8_t neutral[] = {0,4,0x20,0,1,8};
        assert(size == 61 && !memcmp(frame + 14, neutral, sizeof neutral));
        ++p->sticks; is_profile = 0;
    } else if (set == 0x18 && id == 0x47) {
        assert(size == 43 && frame[17] == (p->views < 2));
        ++p->views; is_profile = 0;
    }
    if (is_profile) {
        assert(!excluded(set, id));
        frame_t *expected = &p->expected[p->index];
        uint8_t golden[DJI_NEO_MAX_DUML]; memcpy(golden, expected->data, expected->size);
        assert(dji_neo_duml_reseq(golden, expected->size, test_le16(frame + 6)) == DJI_NEO_OK);
        assert(size == 20 + expected->size && !memcmp(golden, frame, expected->size));
        /* Send failure inside a batch must retry the exact same profile slot
         * and SDK counters, not silently skip an activation command. */
        if (p->fail_profile_once && p->index == 12 && !p->failure_seen) {
            p->failure_seen = 1; return -1;
        }
        if (++p->index == PASSIVE_COUNT) { p->index = 0; ++p->rounds; }
    }
    ++p->type5;
    return test_send(&p->io, packet, size);
}
int main(void) {
    for (int signing = 0; signing <= 1; ++signing) {
        profile_io_t p = {0}; load_profile(&p);
        p.signing = signing; p.fail_profile_once = 1;
        dji_neo_config_t cfg = {0};
        cfg.session_id = 0x4fb0; cfg.body_id = 0x707d;
        cfg.udp_send = capture_profile; cfg.udp_user = &p;
        dji_neo_t *n = dji_neo_create(&cfg); assert(n);
        if (signing) {
            dji_neo_signer_t signer = {.sign=test_sign, .user=&p.io, .ready=test_ready};
            p.io.signer_ready = 1; dji_neo_set_signer(n, &signer);
        }
        test_connect(n);
        unsigned send_errors = 0;
        for (p.now = 5; p.now <= 2150; p.now += 5) {
            if (p.now % 100 == 0) test_idle_telemetry(n, p.now);
            dji_neo_result_t result = dji_neo_poll(n, p.now);
            if (result == DJI_NEO_EIO) { assert(p.failure_seen); ++send_errors; }
            else assert(result == DJI_NEO_OK);
        }
        assert(send_errors == 1 && p.failure_seen && p.rounds == 2 && p.index == 0);
        assert(p.views == 30 && p.sticks >= 38 && p.heartbeat >= 14);
        assert(p.io.signs == (signing ? p.type5 + 1 : 0));
        uint8_t movement[32]; int length = dji_neo_build_gimbal_rate(movement, sizeof movement, 0, 100);
        unsigned sends = p.io.sends;
        assert(dji_neo_send_actuation(n, movement, (size_t)length) == DJI_NEO_ESTATE);
        assert(p.io.sends == sends); /* Signed activation did not open movement gates. */
        dji_neo_destroy(n);
    }
    printf("activation profile: 55 source frames validated; 51 emitted byte-exact after resequencing; unsigned/signed/retry cases passed\n");
    return 0;
}
