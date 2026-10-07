/*
 * neomux — MPEG-TS video+KLV egress for the DJI Neo relay.
 *
 * Pipeline (no re-encode): Neo wrapper type-2 payload -> hevcdepay (Annex-B
 * H.265 access units) -> tsmux video PES (stream_type 0x24), interleaved with
 * an MISB ST 0601 KLV metadata PES (stream_type 0x15) built from the live
 * decoded telemetry. The 188-byte TS packets are batched (7 per 1316-byte UDP
 * datagram) and sent to a configured host:port. The media-server ingests this
 * as an mpeg_ts_udp channel.
 *
 * Single-threaded: every call comes from the neolink engine thread.
 */
#ifndef DJI_EVASIVE_NEOMUX_H
#define DJI_EVASIVE_NEOMUX_H

#include <stddef.h>
#include <stdint.h>
#include "render.h"

typedef struct neomux neomux_t;

/* Open a UDP egress to host:port (numeric or resolvable). Returns NULL on bad
 * args / socket / resolve failure. */
neomux_t *neomux_open(const char *host, uint16_t port);
void      neomux_close(neomux_t *m);

/* Feed one raw Neo type-2 UDP payload (including the 8-byte DJI wrapper). Each
 * complete HEVC access unit is muxed to a video PES and sent. now_ms is a
 * monotonic clock used for the 90 kHz PES timestamps. */
void neomux_feed_video(neomux_t *m, const uint8_t *type2_payload, size_t len,
                       uint64_t now_ms);

/* Set the latest telemetry snapshot used for the KLV track. ts_us = UNIX epoch
 * microseconds (for ST 0601 tag 2). */
void neomux_set_telemetry(neomux_t *m, const flightdata_t *fd, uint64_t ts_us);

/* Emit one ST 0601 KLV set from the latest telemetry if due (~5 Hz). Keeps the
 * metadata track alive even without a GPS fix (timestamp-only set). */
void neomux_tick_klv(neomux_t *m, uint64_t now_ms);

typedef struct {
    uint64_t aus;        /* HEVC access units muxed */
    uint64_t klv_sets;   /* KLV sets muxed */
    uint64_t ts_pkts;    /* 188-byte TS packets produced */
    uint64_t bytes;      /* UDP bytes sent */
    unsigned send_errs;  /* failed sends */
} neomux_stats_t;
void neomux_stats(const neomux_t *m, neomux_stats_t *out);

#endif
