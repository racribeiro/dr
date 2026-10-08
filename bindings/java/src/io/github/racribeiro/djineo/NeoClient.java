package io.github.racribeiro.djineo;

import java.util.Objects;

/** Optional pure-Java event adapter. Thread/lifecycle checks stay in Java;
 * JNI only copies C inputs/outputs. No scheduler or network ownership is added.
 */
public final class NeoClient implements AutoCloseable {
    public interface UdpSender { int send(byte[] datagram); }
    public interface Listener {
        default void onTelemetry(Telemetry telemetry) {}
        default void onVideo(byte[] packet, long monotonicMs, boolean codecH265) {}
        default void onState(int state) {}
    }
    public static final class Telemetry {
        public final boolean gpsValid, attitudeValid;
        public final double latitudeDeg, longitudeDeg, relativeAltitudeM;
        public final float rollDeg, pitchDeg, yawDeg;
        public final int satellites;
        public final long monotonicMs;
        private Telemetry(boolean gps, boolean attitude, double latitude, double longitude,
                double altitude, float roll, float pitch, float yaw, int sats, long ms) {
            gpsValid = gps; attitudeValid = attitude; latitudeDeg = latitude;
            longitudeDeg = longitude; relativeAltitudeM = altitude; rollDeg = roll;
            pitchDeg = pitch; yawDeg = yaw; satellites = sats; monotonicMs = ms;
        }
    }
    private final NativeNeo api;

    public NeoClient(int sessionId, int bodyId, boolean disableActivation,
            UdpSender sender, Listener listener) {
        Objects.requireNonNull(sender); Objects.requireNonNull(listener);
        api = new NativeNeo(sessionId, bodyId, disableActivation, new NativeNeo.Callbacks() {
            @Override public int udpSend(byte[] datagram) { return sender.send(datagram); }
            @Override public void onTelemetry(boolean gps, boolean attitude, double latitude,
                    double longitude, double altitude, float roll, float pitch, float yaw,
                    int sats, long ms) {
                listener.onTelemetry(new Telemetry(gps, attitude, latitude, longitude,
                        altitude, roll, pitch, yaw, sats, ms));
            }
            @Override public void onVideo(byte[] packet, long ms, boolean h265) {
                listener.onVideo(packet, ms, h265);
            }
            @Override public void onState(int state) { listener.onState(state); }
        });
    }
    /** Explicit low-level operations, including all four safety gates. */
    public NativeNeo api() { return api; }
    @Override public void close() { api.close(); }
}
