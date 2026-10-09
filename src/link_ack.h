#ifndef DJI_NEO_LINK_ACK_H
#define DJI_NEO_LINK_ACK_H
#include <stddef.h>
#include <stdint.h>

enum { NEO_RX_WINDOW = 1024, NEO_ACK_MAX_SIZE = 8 + NEO_RX_WINDOW / 4 };
typedef struct {
    uint16_t ack, high;
    unsigned head;
    uint8_t pending[NEO_RX_WINDOW / 8];
} neo_rx_t;
typedef struct {
    uint64_t generation;
    uint16_t base;
    uint8_t count, known;
} neo_video_message_t;
typedef struct {
    neo_rx_t rx;
    uint64_t latest;
    int initialized;
    uint64_t invalid, outside_window, conflicts, unanchored, retransmits;
    neo_video_message_t messages[256];
} neo_video_rx_t;
typedef struct {
    uint16_t base, high, count;
    const uint8_t *status;
    size_t size;
} neo_ack_block_t;

void neo_rx_reset(neo_rx_t *rx, uint16_t seed);
int neo_rx_arrive(neo_rx_t *rx, uint16_t sequence);
int neo_rx_announce(neo_rx_t *rx, uint16_t high);
size_t neo_rx_encode(const neo_rx_t *rx, uint8_t *out);
/* tag=0 is the compact block (the other word can be opaque); tag=1 adds
 * count two-bit statuses, four per byte. Other tags are not guessed. */
int neo_ack_parse(const uint8_t *data, size_t size, neo_ack_block_t *out);
/* Video retransmits use the range endpoint in the wrapper, NOT the fragment's
 * original sequence. Cache/adjacent message anchors recover original indices.
 * Return 0 for invalid/unusable packets. Valid unanchored retransmissions
 * can announce a missing range but never manufacture a received ACK. */
int neo_video_arrive(neo_video_rx_t *video, uint16_t wrapper_sequence,
                       const uint8_t *body, size_t size);
#endif
