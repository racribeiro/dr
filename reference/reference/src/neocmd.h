/*
 * neocmd — type-5 command-uplink wrapping (CARD-023). Pure, host-testable.
 * A DUML command frame (built by drone.c) is wrapped in a DJI type-5 wrapper.
 *
 * CONFIRMED (two independent captures, REVERSE_ENGINEERING.md §12.14): every
 * type-5 uplink DJI Fly sends carries a 12-byte RC sub-header BETWEEN the
 * 8-byte wrapper and the DUML payload:
 *   [0:2]  body_id, BIG-ENDIAN  (identical encoding to CONNECT body[0:2])
 *   [2:4]  wrapper field45, LITTLE-ENDIAN (must equal the wrapper's own f45)
 *   [4:8]  00 00 00 00
 *   [8]    counter, +1 per uplink frame (1 byte, wraps)
 *   [9]    0x01
 *   [10]   0x60
 *   [11]   flag (0x00 across the whole gimbal-move capture)
 * The wrapper's field45 itself increments by +8 on each successive uplink.
 * neocmd_wrap_duml (no sub-header) is kept only for the host unit tests; the
 * live path uses neocmd_wrap_rc.
 */
#ifndef NEOCMD_H
#define NEOCMD_H
#include <stddef.h>
#include <stdint.h>

/* Returns wrapped length, or -1 (bad args / cap too small). */
int neocmd_wrap_duml(uint8_t *out, size_t cap, uint16_t session,
                     const uint8_t *duml, size_t duml_len);

/* Wrap `duml` as a type-5 frame WITH the 12-byte RC sub-header (see above).
 * wrap_f45 is used both as the wrapper field45 and the sub-header [2:4].
 * body_id is the session identity (same value sent in CONNECT body[0:2]).
 * sub_ctr is the per-frame counter (caller increments between calls).
 * Returns wrapped length, or -1. */
int neocmd_wrap_rc(uint8_t *out, size_t cap, uint16_t session, uint16_t wrap_f45,
                   uint16_t body_id, uint8_t sub_ctr,
                   const uint8_t *duml, size_t duml_len);
#endif
