package io.github.racribeiro.djineo;

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/** Desktop JNI tests only. MarkerSigner is NOT DJI authentication. */
public final class TestJava {
    private TestJava() {}
    private static final class CallbackFailure extends RuntimeException {
        private static final long serialVersionUID = 1L;
    }
    private static final class Io implements NativeNeo.Callbacks {
        final List<byte[]> packets = new ArrayList<>();
        NativeNeo neo;
        int telemetry, video, states;
        float yaw;
        byte[] lastVideo;
        byte[] lastAttempt;
        boolean sendFail, sendThrow, callbackThrow, reenter;
        final Thread owner = Thread.currentThread();
        @Override public int udpSend(byte[] p) {
            check(Thread.currentThread() == owner, "send owner thread");
            lastAttempt = p.clone();
            if (reenter) {
                throwsType(IllegalStateException.class, () -> neo.poll(0));
                throwsType(IllegalStateException.class, () -> neo.close());
            }
            if (sendThrow) throw new CallbackFailure();
            if (sendFail) { sendFail = false; return -1; }
            wire(p); packets.add(p); return 0;
        }
        @Override public void onTelemetry(boolean gps, boolean attitude, double latitude,
                double longitude, double altitude, float roll, float pitch, float heading,
                int sats, long ms) {
            check(Thread.currentThread() == owner, "telemetry owner thread");
            check(!gps && attitude && sats == 4 && Math.abs(roll - 0.2f) < 0.01f, "real OSD fields");
            ++telemetry; yaw = heading;
            if (callbackThrow) throw new CallbackFailure();
        }
        @Override public void onVideo(byte[] p, long ms, boolean h265) {
            check(h265 && Thread.currentThread() == owner, "video callback");
            ++video; lastVideo = p;
        }
        @Override public void onState(int state) { ++states; }
    }
    private static final class MarkerSigner implements NativeNeo.Signer {
        boolean available = true, reject, corrupt, signThrow, readyThrow;
        int signs, readiness;
        @Override public boolean ready(int session, int body) {
            ++readiness;
            if (readyThrow) throw new CallbackFailure();
            check(session == 0x4fb0 && body == 0x707d, "signer session context");
            return available;
        }
        @Override public int sign(int session, int body, int field45, int counter, byte[] h, byte[] duml) {
            ++signs;
            if (signThrow) throw new CallbackFailure();
            check(h.length == 12 && le16(h, 2) == field45 && (h[8] & 255) == counter, "signer counters");
            check(Commands.dumlValid(duml), "signer DUML");
            if (reject) return -1;
            if (corrupt) h[0] ^= 1;
            h[11] = (byte)0xa5;
            // The DUML argument is a copy. Mutations must never alter the emitted command.
            Arrays.fill(duml, (byte)0);
            return 0;
        }
    }
    private static void check(boolean value, String message) {
        if (!value) throw new AssertionError(message);
    }
    private static void ok(int result) { check(result == NativeNeo.OK, "C result " + result); }
    private static void throwsType(Class<? extends Throwable> type, Runnable action) {
        try { action.run(); } catch (Throwable t) {
            if (type.isInstance(t)) return;
            throw new AssertionError("Wrong exception", t);
        }
        throw new AssertionError("Expected " + type.getSimpleName());
    }
    private static int le16(byte[] p, int i) { return (p[i] & 255) | ((p[i + 1] & 255) << 8); }
    private static byte[] checksum(byte[] p) {
        p[7] = 0; for (int i = 0; i < 7; ++i) p[7] ^= p[i]; return p;
    }
    private static byte[] accept() { return checksum(new byte[]{8, (byte)0x80, (byte)0xb0,0x4f,0,0,0,0,1}); }
    private static void wire(byte[] p) {
        check(p.length >= 8 && (le16(p, 0) & 0x7fff) == p.length, "wrapper length");
        byte x = 0; for (int i = 0; i < 7; ++i) x ^= p[i]; check(x == p[7], "wrapper XOR");
        if (p[6] == 5) {
            check(le16(p, 4) == le16(p, 10), "duplicated field45");
            check(Commands.dumlValid(Arrays.copyOfRange(p, 20, p.length)), "wrapped DUML CRC");
        }
    }
    private static NativeNeo client(Io io, boolean disabled) {
        io.neo = new NativeNeo(0x4fb0, 0x707d, disabled, io); return io.neo;
    }
    private static void connect(NativeNeo n) {
        ok(n.setSessionArmed(true)); ok(n.poll(0)); ok(n.onDatagram(accept(), 1));
        check(n.linkState() == NativeNeo.LINK_CONNECTED, "connected");
    }
    private static void arm(NativeNeo n) {
        ok(n.setCommandArmed(true)); ok(n.confirmTakeover(true)); ok(n.setStickEnabled(true));
    }
    private static byte[] fixture(String root, String path, String name) throws Exception {
        String text = new String(Files.readAllBytes(Paths.get(root, path)), StandardCharsets.UTF_8);
        Matcher m = Pattern.compile(Pattern.quote(name) + "\\[[^\\]]*\\]\\s*=\\s*\\{([^}]+)\\}").matcher(text);
        check(m.find(), "fixture " + name);
        String[] values = m.group(1).split(","); byte[] p = new byte[values.length];
        for (int i = 0; i < p.length; ++i) p[i] = (byte)Integer.decode(values[i].trim()).intValue();
        return p;
    }
    private static void builders(String root) throws Exception {
        byte[] out = new byte[1023];
        int length = Commands.buildGimbalRate(out, 0x9e3c, -36);
        byte[] real = fixture(root, "reference/tests/test_fixtures.h", "GIMBAL_DUML");
        check(length == real.length && Arrays.equals(Arrays.copyOf(out, length), real), "captured gimbal builder");
        check(Commands.dumlValid(real), "captured gimbal CRC");
        check(Commands.dumlValid(fixture(root, "reference/tests/test_fixtures.h", "OSD_DUML")), "captured OSD CRC");
        ok(Commands.dumlReseq(real, 4)); check(Commands.dumlValid(real) && le16(real, 6) == 4, "reseq");
        check(Commands.buildGimbalRate(new byte[20], 0, -36) == NativeNeo.ENOSPACE, "capacity");
        check(Commands.buildGimbalRate(out, 0, 201) == NativeNeo.EINVAL, "invalid rate");
        check(Commands.buildGimbalEnable(out, 0, 4) == NativeNeo.EINVAL, "invalid stage");
        check(Commands.buildHeartbeat(out, -1) == NativeNeo.EINVAL, "no uint16 truncation");
        check(Commands.buildLiveview(out, 0, 65536, true) == NativeNeo.EINVAL, "no timer truncation");
        check(!Commands.dumlValid(null) && !Commands.dumlValid(new byte[1024]), "invalid frames");
        check(Commands.dumlReseq(null, 0) == NativeNeo.EINVAL, "null reseq");
        check(Commands.buildStick(out, 0, new int[]{1024,1024,1024,1024}, 0xffffffffL) > 0, "uint32 timer");
        check(Commands.buildStick(out, 0, new int[]{1024,1024,1024,1024}, 0x100000000L) == NativeNeo.EINVAL, "timer overflow");
        check(Commands.buildStick(out, 0, new int[]{-1,1024,1024,1024}, 0) == NativeNeo.EINVAL, "channel overflow");
        check(Commands.buildStick(out, 0, new int[]{1024}, 0) == NativeNeo.EINVAL, "channel count");
        check(Commands.dumlBuild(out, 256, 4, 0, 0, 4, 12, null) == NativeNeo.EINVAL, "source overflow");
        check(Commands.dumlBuild(out, 2, 4, 0, 0, 4, 12, new byte[1010]) == 1023, "max DUML");
        check(Commands.dumlBuild(out, 2, 4, 0, 0, 4, 12, new byte[1011]) == NativeNeo.EINVAL, "oversized DUML");
        for (int stage = 0; stage < 4; ++stage) {
            int len = Commands.buildGimbalEnable(out, 2, stage);
            check(len > 0 && Commands.dumlValid(Arrays.copyOf(out, len)), "enable builder");
        }
        check(Commands.buildGimbalKeepalive(out, 0) > 0, "control keepalive builder");
        check(Commands.buildLiveview(out, 0, 1, true) > 0, "liveview builder");
        check(Commands.buildLiveviewEx(out, 0, 1, 0x2f, true) == 23 && out[15] == 0x2f,
                "opaque liveview token");
        check(Commands.dumlValid(Arrays.copyOf(out, 23)), "extended liveview CRC");
        check(Commands.buildLiveviewEx(out, 0, 1, 256, true) == NativeNeo.EINVAL, "token overflow");
    }
    private static void commands(String root) throws Exception {
        Io io = new Io();
        try (NativeNeo n = client(io, true)) {
            check(n.gimbalStart() == NativeNeo.ESTATE, "closed gates");
            ok(n.setKeepaliveBody(new byte[26])); ok(n.setKeepaliveBody(null));
            ok(n.setLiveviewProfile(0x2f, 50));
            throwsType(IllegalArgumentException.class, () -> n.setLiveviewProfile(256, 50));
            throwsType(IllegalArgumentException.class, () -> n.setLiveviewProfile(0x2f, 19));
            io.reenter = true; connect(n); io.reenter = false;
            check(n.getCapabilities() == NativeNeo.CAP_LINK, "unsigned capability");
            check(n.gimbalStart() == NativeNeo.ESTATE, "unsigned movement");
            MarkerSigner s = new MarkerSigner(); n.setSigner(s);
            check(n.getCapabilities() == 3, "ready actuation capability");
            ok(n.setCommandArmed(true)); check(n.gimbalStart() == NativeNeo.ESTATE, "takeover gate");
            ok(n.confirmTakeover(true)); check(n.gimbalStart() == NativeNeo.ESTATE, "stick gate");
            ok(n.setStickEnabled(true)); ok(n.gimbalStart());
            check(s.signs == 4, "four signed enables");
            ok(n.gimbalSetRate(-36, 1)); ok(n.poll(1));
            byte[] golden = fixture(root, "tests/command_golden.h", "golden_gimbal_packet");
            check(Arrays.equals(io.packets.get(io.packets.size() - 1), golden), "byte-exact gimbal datagram from Java");
            ok(n.gimbalStop());
            check(n.setStick(1,2,3,4,2) == NativeNeo.ESTATE, "sticks require enabled activation");
            s.available = false;
            check(n.getCapabilities() == 1, "lost signing capability");
            check(n.gimbalStart() == NativeNeo.EAUTH, "readiness loss");
            s.available = true;
            check(n.gimbalStart() == NativeNeo.ESTATE, "readiness loss revoked gates");
            arm(n);
            byte[] heartbeat = new byte[13]; check(Commands.buildHeartbeat(heartbeat, 0) == 13, "heartbeat");
            s.reject = true; int before = io.packets.size();
            check(n.sendQuery(heartbeat) == NativeNeo.EAUTH && io.packets.size() == before, "signer rejection");
            s.reject = false; s.corrupt = true;
            check(n.sendActuation(heartbeat) == NativeNeo.EAUTH, "protected identity");
            s.corrupt = false; io.sendFail = true;
            check(n.sendQuery(heartbeat) == NativeNeo.EIO, "UDP error");
            byte[] failedPacket = io.lastAttempt;
            ok(n.sendQuery(heartbeat));
            check(Arrays.equals(failedPacket, io.lastAttempt), "send failure preserves wrapper counters");
            ok(n.sendActuation(heartbeat)); // Raw API still requires all four gates.
            io.sendThrow = true; throwsType(CallbackFailure.class, () -> n.sendQuery(heartbeat));
            io.sendThrow = false; ok(n.sendQuery(heartbeat));
            s.signThrow = true; throwsType(CallbackFailure.class, () -> n.sendQuery(heartbeat));
            s.signThrow = false; ok(n.sendQuery(heartbeat));
            s.readyThrow = true; throwsType(CallbackFailure.class, () -> n.getCapabilities());
            s.readyThrow = false;
            n.setSigner(null); check(n.getCapabilities() == 1, "remove signer");
            check(n.restartLiveview() == NativeNeo.ESTATE, "activation disabled");
            ok(n.setSessionArmed(false)); ok(n.resetSession(0x1234, 0x5078));
            throwsType(IllegalArgumentException.class, () -> n.resetSession(0x1234, 0x5678));
        }
        io.neo.close(); // Idempotent.
        throwsType(IllegalStateException.class, () -> io.neo.poll(3));
    }
    private static void dataPlane(String root) throws Exception {
        byte[] osd = fixture(root, "reference/tests/test_fixtures.h", "OSD_DGRAM");
        Io io = new Io();
        try (NativeNeo n = client(io, false)) {
            MarkerSigner s = new MarkerSigner(); s.reject = true; n.setSigner(s);
            ok(n.onDatagram(osd, 0));
            check(io.telemetry == 1 && Math.abs(io.yaw - 318.9f) < 0.01f, "real OSD via Java");
            check(s.signs == 0 && s.readiness == 0, "receive isolated from signer");
            byte[] v = checksum(new byte[]{11,(byte)0x80,0,0,0,0,2,0,1,2,3});
            ok(n.onDatagram(v, 1)); Arrays.fill(v, (byte)0);
            check(Arrays.equals(io.lastVideo, new byte[]{1,2,3}), "owned video copy");
            io.callbackThrow = true; throwsType(CallbackFailure.class, () -> n.onDatagram(osd, 2));
            io.callbackThrow = false; ok(n.onDatagram(osd, 3));
            check(n.onDatagram(new byte[65508], 4) == NativeNeo.EINVAL, "datagram limit");
            throwsType(IllegalArgumentException.class, () -> n.poll(-1));
            throwsType(IllegalArgumentException.class, () -> n.setKeepaliveBody(new byte[25]));
            throwsType(NullPointerException.class, () -> n.onDatagram(null, 4));
            final Throwable[] result = new Throwable[1];
            Thread thread = new Thread(() -> {
                try { n.getCapabilities(); } catch (Throwable t) { result[0] = t; }
            });
            thread.start(); thread.join();
            check(result[0] instanceof IllegalStateException, "wrong thread rejected in Java");
        }
        final int[] delivered = {0};
        try (NeoClient adapter = new NeoClient(1, 2, false, p -> 0, new NeoClient.Listener() {
            @Override public void onTelemetry(NeoClient.Telemetry t) {
                check(t.attitudeValid && Math.abs(t.yawDeg - 318.9f) < 0.01f, "Java structured telemetry");
                ++delivered[0];
            }
        })) { ok(adapter.api().onDatagram(osd, 0)); }
        check(delivered[0] == 1, "adapter delivery");
    }
    private static void activation() {
        Io io = new Io();
        try (NativeNeo n = client(io, false)) {
            connect(n); ok(n.poll(1));
            int type5 = 0; for (byte[] p : io.packets) if (p[6] == 5) ++type5;
            check(type5 >= 4 && n.getCapabilities() == 1, "unsigned activation without signer");
            MarkerSigner s = new MarkerSigner(); n.setSigner(s);
            for (long t = 6; t < 100; t += 5) ok(n.poll(t));
            check(s.signs > 40, "ready signer signs subscription/stick/liveview paths");
            check(n.gimbalStart() == NativeNeo.ESTATE, "activation cannot open actuation gates");
            arm(n); ok(n.setStick(1,2,3,4,100)); ok(n.poll(105));
            int before = io.packets.size();
            s.readyThrow = true; throwsType(CallbackFailure.class, () -> n.poll(110));
            check(io.packets.size() == before, "ready exception cannot downgrade to unsigned output");
            s.readyThrow = false;
            ok(n.restartLiveview());
        }
        // Repeated create/replace/destroy exercises JNI global/local ref cleanup.
        for (int i = 0; i < 300; ++i) {
            try (NativeNeo n = new NativeNeo(i, (i + 1) & 0xf8ff, true, p -> 0)) {
                n.setSigner(new MarkerSigner()); n.setSigner(new MarkerSigner()); n.setSigner(null);
            }
        }
    }
    public static void main(String[] args) throws Exception {
        builders(args[0]); commands(args[0]); dataPlane(args[0]); activation();
        System.out.println("Java/JNI packet, signer, telemetry, lifecycle and error tests passed");
    }
}
