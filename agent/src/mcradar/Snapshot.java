package mcradar;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/** One frame of data, plus its wire encoding (plain text, one UDP datagram). */
final class Snapshot {
    static final class Rec {
        final String name; final double x, y, z; final float yaw;
        Rec(String name, double x, double y, double z, float yaw) {
            this.name = name; this.x = x; this.y = y; this.z = z; this.yaw = yaw;
        }
    }

    String status = "ok";
    Rec local;
    final List<Rec> players = new ArrayList<Rec>();

    /**
     * Wire format (UTF-8):
     *   MCRADAR 1
     *   S <status text>
     *   L <name> <x> <y> <z> <yaw|->     local player (optional)
     *   P <name> <x> <y> <z> <yaw|->     one line per OTHER player
     */
    byte[] encode() {
        StringBuilder sb = new StringBuilder(256);
        sb.append("MCRADAR 1\n");
        sb.append("S ").append(status.replace('\n', ' ')).append('\n');
        if (local != null) line(sb, 'L', local);
        for (Rec r : players) line(sb, 'P', r);
        return sb.toString().getBytes(java.nio.charset.Charset.forName("UTF-8"));
    }

    private static void line(StringBuilder sb, char tag, Rec r) {
        String n = (r.name == null || r.name.isEmpty()) ? "?" : r.name.replaceAll("\\s", "_");
        String yaw = Float.isNaN(r.yaw) || Float.isInfinite(r.yaw) ? "-" : String.format(Locale.ROOT, "%.2f", r.yaw);
        sb.append(tag).append(' ').append(n).append(' ')
          .append(String.format(Locale.ROOT, "%.3f %.3f %.3f ", r.x, r.y, r.z)).append(yaw).append('\n');
    }
}
