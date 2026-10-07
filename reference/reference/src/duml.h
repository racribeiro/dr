/*
 * duml — DJI DUML v1 frame codec (build + CRCs), in C.
 *
 * CRC parameters were DERIVED from a live capture and proven by re-encoding
 * 7276/7276 frames byte-identical (see scripts/duml.py and
 * REVERSE_ENGINEERING.md §3.2):
 *   CRC-8  (header): poly 0x31, init 0x50, refin, refout, xorout 0xff
 *   CRC-16 (frame) : poly 0x1021, init 0x496c, refin, refout, xorout 0x0
 *
 * This only builds well-formed frames. It says NOTHING about whether a given
 * cmd_set/cmd_id/payload is correct or safe for a given aircraft.
 */
#ifndef DJI_EVASIVE_DUML_H
#define DJI_EVASIVE_DUML_H

#include <stddef.h>
#include <stdint.h>

#define DUML_SOF      0x55
#define DUML_MAX      1024

/* Build a DUML frame into `out` (>= DUML_MAX). Returns the frame length, or
 * -1 if payload is too large. src/dst are device bytes (type | index<<5). */
int duml_build(uint8_t *out, uint8_t src, uint8_t dst, uint16_t seq,
               uint8_t cmd_type, uint8_t cmd_set, uint8_t cmd_id,
               const uint8_t *payload, size_t payload_len);

/* Rewrite the 16-bit seq (bytes 6-7) of an existing DUML frame in place and
 * recompute its CRC-16 (CRC-8 header is unaffected). For replaying captured
 * commands with fresh sequence numbers. Returns 0 on success. */
int duml_reseq(uint8_t *f, size_t len, uint16_t seq);

/* 1 if the first 4 bytes are a valid header (SOF + CRC-8). */
int duml_check_header(const uint8_t *f);

/* 1 if f[0..len) is exactly one well-formed frame (SOF, length, CRC-8, CRC-16). */
int duml_check(const uint8_t *f, size_t len);

/* Hex-encode `in` (len bytes) into `out` (>= 2*len+1), NUL-terminated. */
void duml_hex(const uint8_t *in, size_t len, char *out);

#endif /* DJI_EVASIVE_DUML_H */
