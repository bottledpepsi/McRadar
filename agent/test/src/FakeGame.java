import com.mojang.authlib.GameProfile;
import java.util.*;

/**
 * A fake "Minecraft" with obfuscated names and the same STRUCTURE as the real game, used to
 * test the agent without launching Minecraft. Style "new" = Vec3 position (1.17+), style "old" =
 * plain double fields (<=1.16).   java -cp <out> FakeGame --username Alice [new|old]
 */
final class V {                       // Vec3
    public final double a, b, c;
    V(double a, double b, double c) { this.a = a; this.b = b; this.c = c; }
}
class E {                             // Entity (new style)
    V q = new V(0, 0, 0);             // position
    V r = new V(0, 0, 0);             // delta movement
    double w = 3.5;                   // unrelated double
    float f1, f2;                     // yaw, pitch
    public V m1() {                   // look vector (unit)
        double yaw = Math.toRadians(f1), pitch = Math.toRadians(f2);
        return new V(-Math.sin(yaw) * Math.cos(pitch), -Math.sin(pitch), Math.cos(yaw) * Math.cos(pitch));
    }
    public V m2() { return q; }       // position()  - NOT unit length
    public V m3() { return r; }       // deltaMovement
}
class OE {                            // Entity (old style): plain doubles, triples in declaration order
    double px, py, pz;                // position
    double mx, my, mz;                // motion
    double lx, ly, lz;                // last-tick position
    float yaw, pitch;
    public V look() {
        double y = Math.toRadians(yaw), p = Math.toRadians(pitch);
        return new V(-Math.sin(y) * Math.cos(p), -Math.sin(p), Math.cos(y) * Math.cos(p));
    }
}
class P extends E { GameProfile gp; }         // player (new)
class LP extends P { }                        // local player
class RP extends P { }                        // remote player
class OP extends OE { GameProfile gp; }       // player (old)
class Mob extends E { }                       // non-player entity
class Info { GameProfile gp; int latency; }   // tab-list entry: has a GameProfile but NO position
class Lvl {
    List<Object> players = new ArrayList<Object>();
    List<Object> all = new ArrayList<Object>();   // mixed: players + mobs  -> must NOT be chosen
    Map<Integer, Object> byId = new HashMap<Integer, Object>();
}
class Srv { List<Object> sp = new ArrayList<Object>(); }
class M {                              // Minecraft singleton
    static M instance;
    Object player; Lvl level; Srv server; List<Info> tab = new ArrayList<Info>();
    String junk = "x";
}

public class FakeGame {
    static GameProfile gp(String n) { return new GameProfile(UUID.nameUUIDFromBytes(n.getBytes()), n); }

    public static void main(String[] args) throws Exception {
        final boolean old = Arrays.asList(args).contains("old");
        final M m = new M(); M.instance = m; m.level = new Lvl(); m.server = new Srv();
        final String[] names = {"Alice", "Bob", "Carol"};
        final Object[] pl = new Object[3];
        for (int i = 0; i < 3; i++) {
            Object p;
            if (old) { OP o = new OP(); o.gp = gp(names[i]); p = o; }
            else { P x = i == 0 ? new LP() : new RP(); x.gp = gp(names[i]); p = x; }
            pl[i] = p; m.level.players.add(p); m.level.all.add(p); m.server.sp.add(p);
            Info in = new Info(); in.gp = gp(names[i]); m.tab.add(in);
        }
        m.level.all.add(new Mob());
        m.player = pl[0];
        System.out.println("PID " + java.lang.management.ManagementFactory.getRuntimeMXBean().getName());
        long t0 = System.currentTimeMillis();
        while (System.currentTimeMillis() - t0 < 120000) {
            double t = (System.currentTimeMillis() - t0) / 1000.0;
            float yaw = (float) ((t * 30) % 360 - 180);
            double[][] pos = { {100 + 5 * Math.sin(t), 64, -50 + 5 * Math.cos(t)}, {110, 64, -40 + 3 * Math.sin(t)}, {90, 70, -60} };
            for (int i = 0; i < 3; i++) {
                if (old) { OP o = (OP) pl[i]; o.px = pos[i][0]; o.py = pos[i][1]; o.pz = pos[i][2]; o.lx = o.px - .1; o.ly = o.py; o.lz = o.pz; o.mx = .05; o.yaw = i == 0 ? yaw : 45f * i; }
                else { P x = (P) pl[i]; x.q = new V(pos[i][0], pos[i][1], pos[i][2]); x.r = new V(.01, 0, .02); x.f1 = i == 0 ? yaw : 45f * i; }
            }
            if (((int) t) % 5 == 0 && t - Math.floor(t) < 0.05) System.out.printf("expected Alice yaw=%.1f x=%.3f z=%.3f%n", yaw, pos[0][0], pos[0][2]);
            Thread.sleep(50);
        }
    }
}
