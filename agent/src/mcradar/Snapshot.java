package mcradar;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/** One frame of data, plus its wire encoding (plain text, one UDP datagram). */
final class Snapshot {
    static final class Rec {
        final String name; final double x, y, z; final float yaw, pitch, health;
        Rec(String name, double x, double y, double z, float yaw, float pitch, float health) {
            this.name = name; this.x = x; this.y = y; this.z = z;
            this.yaw = yaw; this.pitch = pitch; this.health = health;
        }
    }

    String status = "ok";
    Rec local;
    final List<Rec> players = new ArrayList<Rec>();

    /**
     * Wire format v2 (UTF-8), one UDP datagram per snapshot:
     *   MCRADAR 2
     *   S <status text>
     *   L <name> <x> <y> <z> <yaw|-> <pitch|-> <health|->    local player (optional)
     *   P <name> <x> <y> <z> <yaw|-> <pitch|-> <health|->    one line per OTHER player
     * ("-" = value could not be determined.)  Yaw/pitch in degrees, Minecraft convention:
     * yaw 0 = +Z (south), pitch +90 = straight down.
     */
    byte[] encode() {
        StringBuilder sb = new StringBuilder(256);
        sb.append("MCRADAR 2\n");
        sb.append("S ").append(status.replace('\n', ' ')).append('\n');
        if (local != null) line(sb, 'L', local);
        for (Rec r : players) line(sb, 'P', r);
        return sb.toString().getBytes(java.nio.charset.Charset.forName("UTF-8"));
    }

    private static void line(StringBuilder sb, char tag, Rec r) {
        String n = (r.name == null || r.name.isEmpty()) ? "?" : r.name.replaceAll("\\s", "_");
        sb.append(tag).append(' ').append(n).append(' ')
          .append(String.format(Locale.ROOT, "%.3f %.3f %.3f ", r.x, r.y, r.z))
          .append(num(r.yaw)).append(' ').append(num(r.pitch)).append(' ').append(num(r.health)).append('\n');
    }

    private static String num(float v) {
        return Float.isNaN(v) || Float.isInfinite(v) ? "-" : String.format(Locale.ROOT, "%.2f", v);
    }
}
