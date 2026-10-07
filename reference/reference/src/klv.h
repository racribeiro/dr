#ifndef DJI_EVASIVE_KLV_H
#define DJI_EVASIVE_KLV_H
/* MISB ST 0601 (UAS Datalink Local Set) KLV encoder. Pure C, no I/O.
 * Tags emitted (scalings per ST 0601, see klv.c): 2 timestamp, 5 heading,
 * 6 pitch, 7 roll, 13 lat, 14 lon, 15 alt (MSL), 65 ST version, 1 checksum. */
#include <stddef.h>
#include <stdint.h>
#include "render.h"

#define KLV_UL_LEN 16
extern const uint8_t KLV_0601_UL[KLV_UL_LEN];

/* Encode one Local Set. Returns total bytes written (key+BER len+value) or 0 if
 * `cap` is too small / bad args. fd->valid==0 still encodes (timestamp only +
 * version + checksum) so the stream stays alive. ts_us = UNIX epoch microsec. */
size_t klv_encode_0601(const flightdata_t *fd, uint64_t ts_us, uint8_t *out, size_t cap);

/* ST 0601 16-bit checksum over buf[0..len) (high byte on even offsets). */
uint16_t klv_checksum(const uint8_t *buf, size_t len);

/* Locate `tag` in a Local Set produced by klv_encode_0601 (or any ST 0601 set
 * with single-byte tags). Returns pointer to the value and sets *vlen, or NULL. */
const uint8_t *klv_find(const uint8_t *set, size_t len, uint8_t tag, size_t *vlen);

#endif
