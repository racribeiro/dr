# DJI Neo C / Java SDK

C11 SDK for encoding DJI commands and driving a Neo Wi-Fi/UDP session from
`dji-evasive` or another host application. The host owns its UDP socket and
drives `dji_neo_poll()`; the SDK creates no threads.

Implemented: DUML builders/CRCs, type-5 RC wrapping, subscription replay,
centered-stick heartbeat, configurable finite liveview start bursts, cumulative
and selective receive ACKs with video retransmission tracking, session-seeded
uplink counters, gimbal enable/rate/stop,
control keepalive, signed stick setpoints, optional signing of all type-5
activation/command uplink, and OSD telemetry decoding.

Actuation requires a host-provided signer that explicitly reports readiness for
the current session, plus session arm, command arm, takeover confirmation, and
stick enable. Passive activation can transmit with no signer; when a signer
reports ready it signs activation too. Receive/decode never needs signing.
Unsigned sessions have produced rich OSD in the app's tests; sustained SDK video
and on-drone commands remain unverified. Opaque RC fields are not fully understood:
captures alone do not establish that all of them are cryptographic signatures.
Internal tests use a mock signer and do not prove drone acceptance.

Activation uses 51 of the app's supplied 55 command kinds: liveview is scheduled
separately and three credential queries are omitted. Flight/gimbal control is
excluded from bootstrap. See the [profile and provenance](reference/activation/README.md).

See [integration and command usage](docs/INTEGRATION.md) and the public headers:
[client API](include/dji_neo/dji_neo.h), [command builders](include/dji_neo/commands.h).

## Build

Requires Make, CMake 3.20+, and a C11 compiler. The optional loopback test uses
POSIX sockets.

```sh
make build  # configure and compile the static library
make test   # build, then run CTest
make clean  # remove generated build output
make activation-check  # optional Python check of generated activation bytes
make java-test  # optional Java/JNI build + C and JVM integration tests (JDK 11+)
make capture-review CAPTURE=/path/to/file.pcap  # offline clear-IP capture review
```

`make` is equivalent to `make build`. Use `BUILD_DIR=out make test` to select a
different build directory.

Java is opt-in; C-only builds do not need a JDK. `make java` produces
`build/java/bindings/java/dji-neo.jar` and `libdji_neo_jni.so` (platform library
extension varies). The Java API targets Java 8; building it requires JDK 11+.
See [Java/JNI integration](bindings/java/README.md), including Android setup.

Tests compare builders byte-for-byte against the sanitized capture, validate
the complete emitted gimbal datagram, and exercise gates, signer errors, send
failures, counter wrap, activation cadence, stale inputs, and session loss.
The full activation profile is checked byte-for-byte after resequencing, both
unsigned and signed, including retry after a mid-batch send failure.
The real OSD/CRC fixture is also registered in CTest. The UDP loopback test is
reported as skipped if the environment denies socket creation.
Link tests cover seeded counters, complete variable-length ACK packets,
fragment-aware retransmissions, missing/received two-bit statuses, malformed
metadata, video-only liveness with stale-telemetry actuation revocation, sequence
and message-ID wrap, and session reset. The offline reviewer checks DUML,
liveview/ACK encoding and recovered original fragment sequences against private
PCAPs without transmitting or printing GPS/credentials. Unresolved receive
history is reported explicitly; a successful decode review alone is not proof
of sustained recovery.

## Integrate

```cmake
add_subdirectory(path/to/dji-reverse neo-sdk)
target_link_libraries(your_app PRIVATE dji_neo::dji_neo)
```

Or install a package with `cmake --install build --prefix /your/sdk/prefix`, then
use `find_package(dji_neo CONFIG REQUIRED)`. Static builds are the default;
configure with `-DBUILD_SHARED_LIBS=ON` for a shared library. Disable tests in
application/Android builds with `-DDJI_NEO_BUILD_TESTS=OFF`.

Video output currently contains raw type-2 stream packets. The host still needs
`hevcdepay` and TS muxing. The thin JNI mirror and a separate pure-Java telemetry
adapter are implemented. See the [October capture review](docs/CAPTURE_REVIEW_20261009.md)
for evidence and remaining abandonment/outgoing-retry/liveview-negotiation gaps. This is an
implementation ready for controlled host testing, not a completed on-drone SDK.
