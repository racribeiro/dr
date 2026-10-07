#include "dji_neo/dji_neo.h"
#include <assert.h>
#include <string.h>

static int sent;
static int send_packet(void *user, const uint8_t *data, size_t n) { (void)user; assert(data && n >= 8); ++sent; return 0; }
int main(void) {
    dji_neo_config_t cfg = {0}; cfg.udp_send = send_packet; cfg.session_id = 0x4fb0; cfg.body_id = 0x707d;
    dji_neo_t *neo = dji_neo_create(&cfg); assert(neo);
    dji_neo_capabilities_t caps; dji_neo_get_capabilities(neo, &caps); assert(!caps.has_link && !caps.has_actuation);
    assert(dji_neo_set_session_armed(neo, 1) == DJI_NEO_OK);
    assert(dji_neo_poll(neo, 10) == DJI_NEO_OK && sent == 1);
    assert(dji_neo_set_command_armed(neo, 1) == DJI_NEO_ESTATE);
    dji_neo_destroy(neo);
    return 0;
}
