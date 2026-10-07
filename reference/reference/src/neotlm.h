/* neotlm — decode DJI Neo OSD telemetry (DUML cmd-set 0x03 / cmd-id 0x43)
 * into a flightdata_t. Pure functions, host-testable. */
#ifndef NEOTLM_H
#define NEOTLM_H
#include <stddef.h>
#include <stdint.h>
#include "render.h"

/* Decode an OSD payload (>= 30 bytes). Returns 1 and fills *out, else 0.
 * lat/lon are PROVEN; attitude is HYPOTHESIS; alt/sats unknown. */
int neotlm_decode_osd(const uint8_t *payload, size_t len, flightdata_t *out);

/* Decode a whole DUML frame (0x55 ... CRC16). Checks SOF, cmd-set 0x03,
 * cmd-id 0x43, extracts payload (offset 11 .. len-2). Returns 1 if filled. */
int neotlm_decode_frame(const uint8_t *frame, size_t len, flightdata_t *out);

/* Decode a whole neo->phone IPv4 packet: parses IPv4/UDP, checks the source is
 * the Neo telemetry socket (192.168.2.1:9003), strips the 8-byte DJI wrapper,
 * requires wrapper type 1 (telemetry), scans the body for an OSD 0x03/0x43 DUML
 * frame and decodes it. Returns 1 if *out was filled, 0 otherwise. This is the
 * live-capture entry point (relay observer); host-testable with a real packet. */
int neotlm_decode_ipv4(const uint8_t *pkt, size_t len, flightdata_t *out);
#endif
