# dji-evasive integration boundary

The SDK consumes complete UDP payloads from the Neo AP (`192.168.2.1:9003`) and
emits decoded telemetry or H.265 packets synchronously. The host drives
`dji_neo_poll()` from its existing single-threaded loop and forwards received
datagrams to `dji_neo_on_datagram()`.

Keep these existing app responsibilities outside this library:

- BLE provisioning / AP keepalive;
- Android network binding and UDP socket ownership;
- Zenoh publication of the app's `AircraftState` ICD;
- KLV production, MPEG-TS muxing and media-server egress;
- policy/UI for all dangerous command decisions.

`dji_neo_set_signer()` is optional. Receive/decode paths never invoke it.
Without it the library has `has_actuation == 0`, but telemetry/video and
read-only query framing remain available. Actuation requires all four explicit
gates in order: session arm, command arm, takeover confirmation, stick enable.

The Kotlin layer is deliberately pure Kotlin; JNI should remain a one-to-one
mirror of `include/dji_neo/dji_neo.h` when added to the Android build.
