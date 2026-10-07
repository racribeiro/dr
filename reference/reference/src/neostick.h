/*
 * neostick — DJI Neo virtual-stick frame builder (CARD-029). Pure, host-testable.
 *
 * Layout decoded in CARD-024 (REVERSE_ENGINEERING.md 12.10a): DUML cmd-set 0x01
 * / cmd-id 0x0a, 28-byte payload:
 *   @0-2   01 0d 00                      fixed
 *   @3-8   4 x 11-bit LITTLE-ENDIAN bit-packed channels (v = u48le; ch_i =
 *          (v >> 11*i) & 0x7ff)
 *   @9-19  40 00 02 00 00 06 55 01 04 56 08   fixed (all 2947 captured frames)
 *   @20-23 u32 LE millisecond timer
 *   @24-27 zero
 * Channel range: neutral 1024, min 364, max 1684 (+-660, DJI RC convention).
 *
 * Channel identity (mode-2 order):
 *   ch0 ROLL     high = right            HIGH confidence
 *   ch1 PITCH    high = forward          HIGH identity; the nose-DOWN sign
 *                                        is a HYPOTHESIS until on-device test
 *   ch2 THROTTLE high = up               MEDIUM-HIGH; POLARITY IS A HYPOTHESIS
 *   ch3 YAW      high = clockwise        HIGH
 * ch1 sign and ch2 polarity are UNCONFIRMED until an on-device test.
 */
#ifndef NEOSTICK_H
#define NEOSTICK_H
#include <stddef.h>
#include <stdint.h>

#define NEOSTICK_MIN     364
#define NEOSTICK_CENTER  1024
#define NEOSTICK_MAX     1684
#define NEOSTICK_RANGE   660     /* deflection units either side of center */
#define NEOSTICK_PAYLOAD_LEN 28
/* DUML routing as seen on the wire (phone -> Neo). */
#define NEOSTICK_SRC 0x02
#define NEOSTICK_DST 0xa9
#define NEOSTICK_CMD_SET 0x01
#define NEOSTICK_CMD_ID  0x0a

/* Clamp each channel to 364..1684, pack into out28, return 28. */
int neostick_pack(uint8_t *out28, uint16_t roll, uint16_t pitch,
                  uint16_t throttle, uint16_t yaw, uint32_t ms);
/* Inverse of the packing (channels only, unclamped raw 11-bit values). */
void neostick_unpack(const uint8_t *in28, uint16_t ch[4]);
/* Deflection -660..+660 (0 = center) -> channel 364..1684 (clamped). */
uint16_t neostick_from_deflection(int d);
/* Full DUML 0x01/0x0a frame (payload packed internally). Returns frame length
 * or -1. `out` needs >= 64 bytes. */
int neostick_build_frame(uint8_t *out, size_t cap, uint16_t seq,
                         uint16_t roll, uint16_t pitch, uint16_t throttle,
                         uint16_t yaw, uint32_t ms);
#endif
