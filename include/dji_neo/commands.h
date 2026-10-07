#ifndef DJI_NEO_COMMANDS_H
#define DJI_NEO_COMMANDS_H
#include "dji_neo.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Pure encoders: return frame length, or a negative dji_neo_result_t.
 * Encoding a frame does not grant permission to transmit it. */
int dji_neo_duml_build(uint8_t *out, size_t capacity, uint8_t source,
                       uint8_t destination, uint16_t sequence, uint8_t command_type,
                       uint8_t command_set, uint8_t command_id,
                       const uint8_t *payload, size_t payload_size);
int dji_neo_duml_valid(const uint8_t *frame, size_t size);
dji_neo_result_t dji_neo_duml_reseq(uint8_t *frame, size_t size, uint16_t sequence);
int dji_neo_build_heartbeat(uint8_t *out, size_t capacity, uint16_t sequence);
int dji_neo_build_liveview(uint8_t *out, size_t capacity, uint16_t sequence,
                           uint16_t timer_ms, int start_edge);
/* Channels: roll/pitch/throttle/yaw, center 1024, allowed 364..1684.
 * Pitch/throttle polarity is not yet validated on the drone. */
int dji_neo_build_stick(uint8_t *out, size_t capacity, uint16_t sequence,
                        const uint16_t channels[4], uint32_t timer_ms);
/* Gimbal rate is in observed wire units, NOT claimed to be degrees/sec.
 * Range -200..200; sign-to-direction remains unverified. */
int dji_neo_build_gimbal_rate(uint8_t *out, size_t capacity, uint16_t sequence,
                              int rate);
/* Enable stages 0..3: long 04/10 payload, then three short 04/10 frames. */
int dji_neo_build_gimbal_enable(uint8_t *out, size_t capacity, uint16_t sequence,
                                unsigned stage);
int dji_neo_build_gimbal_keepalive(uint8_t *out, size_t capacity, uint16_t sequence);
#ifdef __cplusplus
}
#endif
#endif
