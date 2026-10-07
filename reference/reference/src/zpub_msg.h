/*
 * zpub_msg — encode flightdata_t as tek-icd protobuf messages (CARD-022).
 * Field numbers verified against tek-icd/protoFiles/{Navigation,MessageWrapper,
 * General}.proto: GetGeoPosition{lat=1,lon=2,alt=3,velocity=4(Vector3D)},
 * GetAttitudeStatus{roll=1,pitch=2,yaw=3}, MessageWrapper{time_enc=1,payload=2}.
 * Pure C, no Zenoh. Each returns the encoded length, or 0 on overflow.
 */
#ifndef DJI_EVASIVE_ZPUB_MSG_H
#define DJI_EVASIVE_ZPUB_MSG_H

#include <stddef.h>
#include <stdint.h>
#include "render.h"

size_t zmsg_geo_position(const flightdata_t *fd, uint8_t *out, size_t cap);
size_t zmsg_attitude(const flightdata_t *fd, uint8_t *out, size_t cap);
size_t zmsg_wrap(int64_t time_enc_us, const uint8_t *payload, size_t n,
                 uint8_t *out, size_t cap);

/* --- GCS ICD (system-icd-gcs `Messages`, wire-compatible subset `gcs_state`) ---
 * The format atlas's gcs-connector AND the media-server KlvInjector decode.
 * Field numbers verified against atlas_autonomy adapters/gcs/icd/system-icd-gcs
 * (SystemMessageWrapper/FlightMessages/SharedTypes) and the media-server
 * doc/proto/gcs-state/gcs_state.proto. ALL angles + lat/lon are RADIANS. */

/* Pack a Unix-epoch-microsecond time into the ICD `time_enc` int64: a 17-digit
 * decimal UTC stamp yyyyMMddHHmmssfff (e.g. 20261006121530123). NOT epoch. */
int64_t zmsg_time_enc(int64_t unix_us);

/* Build SystemMessageWrapper{ time_enc=1, aircraft_state=100: AircraftState{
 *   k_aircraft_id=1, time_enc=2, location=3{lat_rad,lng_rad,alt_m},
 *   attitude=4{psi,theta,phi}, track_rad=6 (= heading) } }.
 * `aircraft_id` is the drone's stable id; `time_enc` from zmsg_time_enc().
 * Converts fd's degrees -> radians. Returns encoded length, or 0 on overflow. */
size_t zmsg_aircraft_state(const flightdata_t *fd, const char *aircraft_id,
                           int64_t time_enc, uint8_t *out, size_t cap);

#endif
