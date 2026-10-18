import com.mojang.authlib.GameProfile;
import java.util.*;

/**
 * A fake "Minecraft" with obfuscated names and the same STRUCTURE as the real game, used to test the agent
 * without launching Minecraft.
 *   java -cp <out> FakeGame --username Alice [old] [static] [wiggle] [yaw=N] [pitch=N] [third]
 *   new style: Vec3 positions, AABB bounding boxes, a camera object + renderer with a getFov method,
 *              dropped items (NBT-style and component-style stacks), decoy entities (item frame, mob)
 *   old style: plain double fields (<=1.16), players only
 */
final class V { public final double a, b, c; V(double a, double b, double c) { this.a = a; this.b = b; this.c = c; } }   // Vec3
final class BB { public double a, b, c, d, e, f; void set(double x, double y, double z, double w, double h) { a = x - w / 2; b = y; c = z - w / 2; d = x + w / 2; e = y + h; f = z + w / 2; } } // AABB

class E {                             // Entity (new style)
    V q = new V(0, 0, 0);             // position
    V r = new V(0, 0, 0);             // delta movement
    BB bb = new BB();                 // bounding box
    double w = 3.5;                   // unrelated double
    float f1, f2;                     // yaw, pitch
    public V m1() { double yaw = Math.toRadians(f1), pitch = Math.toRadians(f2);
        return new V(-Math.sin(yaw) * Math.cos(pitch), -Math.sin(pitch), Math.cos(yaw) * Math.cos(pitch)); }   // look vector
    public V m2() { return q; }
    public V m3() { return r; }
    public float getHealth() { return 17.5f; }
}
class OE {                            // Entity (old style)
    double px, py, pz, mx, my, mz, lx, ly, lz;
    float yaw, pitch;
    public float getHealth() { return 12f; }
    public V look() { double y = Math.toRadians(yaw), p = Math.toRadians(pitch);
        return new V(-Math.sin(y) * Math.cos(p), -Math.sin(p), Math.cos(y) * Math.cos(p)); }
}
class P extends E { GameProfile gp; }
class LP extends P { }
class RP extends P { }
class OP extends OE { GameProfile gp; }
class Mob extends E { }
class Info { GameProfile gp; int latency; }       // tab-list entry: a profile but no position

// ---- items -------------------------------------------------------------------------------------------
class It { String id; int maxStack = 64; It(String id) { this.id = id; }
    public String getDescriptionId() { return "item.minecraft." + id; } }                       // registry Item
class Tag {                                                                                       // CompoundTag-like, prints SNBT
    Map<String, Object> m = new LinkedHashMap<String, Object>(); String snbt;
    public String toString() { return snbt; } }
class Holder { String key; Holder(String k) { key = k; }
    public String toString() { return "Reference{ResourceKey[minecraft:enchantment / minecraft:" + key + "]=net.fake.Enchantment@1}"; } }
class EnchData { Map<Object, Integer> enchantments = new LinkedHashMap<Object, Integer>(); boolean show = true; }
class TypeKey { String n; TypeKey(String n) { this.n = n; } public String toString() { return "minecraft:" + n; } }
class Comps { Map<Object, Object> patch = new LinkedHashMap<Object, Object>(); }                  // components-style
class Stack { It item; int count; Tag tag; Comps comps; int pop;
    public String getDescriptionId() { return item.getDescriptionId(); } }
class ItemEnt extends E { Stack stack; int age; }
class FrameEnt extends E { Stack stack; }                                                          // item frame (decoy, 0.75 box)

// ---- camera ------------------------------------------------------------------------------------------
class Cam { V p = new V(0, 0, 0); float xr, yr; float eh = 1.62f; boolean det; }
class GR { Cam cam = new Cam(); float mod = 1.0f;
    private double getF(Cam c, float partial, boolean useSetting) { return 70.0 * mod; } }          // vertical FOV in degrees

class Lvl {
    List<Object> players = new ArrayList<Object>();
    List<E> all = new ArrayList<E>();                  // mixed entity storage (players, items, mobs, frames)
    Map<Integer, E> byId = new HashMap<Integer, E>();
}
class Srv { List<Object> sp = new ArrayList<Object>(); }
class M {
    static M instance;
    Object player; Lvl level; Srv server; GR gr = new GR(); List<Info> tab = new ArrayList<Info>();
    String junk = "x";
}

public class FakeGame {
    static GameProfile gp(String n) { return new GameProfile(UUID.nameUUIDFromBytes(("OfflinePlayer:" + n).getBytes()), n); }

    static Stack stack(String id, int n) { Stack s = new Stack(); s.item = new It(id); s.count = n; return s; }

    public static void main(String[] args) throws Exception {
        final List<String> a = Arrays.asList(args);
        final boolean old = a.contains("old"), fixed = a.contains("static"), third = a.contains("third"), wiggle = a.contains("wiggle");
        float fy = 0, fp = 0;
        for (String s : args) { if (s.startsWith("yaw=")) fy = Float.parseFloat(s.substring(4)); if (s.startsWith("pitch=")) fp = Float.parseFloat(s.substring(6)); }
        final float fixedYaw = fy, fixedPitch = fp;

        final M m = new M(); M.instance = m; m.level = new Lvl(); m.server = new Srv();
        final String[] names = {"Alice", "Bob", "Carol"};
        final Object[] pl = new Object[3];
        for (int i = 0; i < 3; i++) {
            Object p;
            if (old) { OP o = new OP(); o.gp = gp(names[i]); p = o; }
            else { P x = i == 0 ? new LP() : new RP(); x.gp = gp(names[i]); p = x; m.level.all.add(x); m.level.byId.put(i, x); }
            pl[i] = p; m.level.players.add(p); m.server.sp.add(p);
            Info in = new Info(); in.gp = gp(names[i]); m.tab.add(in);
        }
        m.player = pl[0];

        final ItemEnt sword = new ItemEnt(), cobble = new ItemEnt(), book = new ItemEnt();
        final FrameEnt frame = new FrameEnt();
        if (!old) {
            sword.stack = stack("diamond_sword", 1);
            sword.stack.tag = new Tag();
            sword.stack.tag.snbt = "{Enchantments:[{id:\"minecraft:sharpness\",lvl:5s},{lvl:3s,id:\"minecraft:unbreaking\"}],Damage:12,display:{Name:'{\"text\":\"Excalibur\"}'}}";
            cobble.stack = stack("cobblestone", 37);
            book.stack = stack("enchanted_book", 1);
            book.stack.comps = new Comps();
            EnchData ed = new EnchData();
            ed.enchantments.put(new Holder("mending"), 1);
            ed.enchantments.put(new Holder("protection"), 4);
            book.stack.comps.patch.put(new TypeKey("stored_enchantments"), Optional.of(ed));
            book.stack.comps.patch.put(new TypeKey("repair_cost"), Optional.of(Integer.valueOf(3)));
            frame.stack = stack("map", 1);
            for (E e : new E[] {sword, cobble, book, frame, new Mob()}) m.level.all.add(e);
        }

        System.out.println("PID " + java.lang.management.ManagementFactory.getRuntimeMXBean().getName());
        long t0 = System.currentTimeMillis();
        while (System.currentTimeMillis() - t0 < 120000) {
            double t = (System.currentTimeMillis() - t0) / 1000.0;
            float yaw = fixed ? fixedYaw : wiggle ? (float) (-30 + 20 * Math.sin(t * 1.5)) : (float) ((t * 30) % 360 - 180);   // wiggle: head sweeps across the items
            float pitch = fixed ? fixedPitch : wiggle ? (float) (10 + 8 * Math.sin(t * 2.1)) : (float) (20 + 15 * Math.sin(t * 1.3));
            double[][] pos = fixed ? new double[][] { {100, 64, -50}, {110, 64, -40}, {90, 70, -60} }
                                   : new double[][] { {100 + 5 * Math.sin(t), 64, -50 + 5 * Math.cos(t)}, {110, 64, -40 + 3 * Math.sin(t)}, {90, 70, -60} };
            for (int i = 0; i < 3; i++) {
                if (old) { OP o = (OP) pl[i]; o.px = pos[i][0]; o.py = pos[i][1]; o.pz = pos[i][2]; o.lx = o.px - .1; o.ly = o.py; o.lz = o.pz; o.mx = .05; o.yaw = i == 0 ? yaw : 45f * i; o.pitch = i == 0 ? pitch : 10f * i; }
                else { P x = (P) pl[i]; x.q = new V(pos[i][0], pos[i][1], pos[i][2]); x.r = new V(.01, 0, .02); x.f1 = i == 0 ? yaw : 45f * i; x.f2 = i == 0 ? pitch : 10f * i;
                       x.bb.set(pos[i][0], pos[i][1], pos[i][2], 0.6, i == 2 ? 1.5 : 1.8); }      // Carol is sneaking
            }
            if (!old) {
                sword.q = new V(103, 64, -47); sword.bb.set(103, 64, -47, 0.25, 0.25);
                cobble.q = new V(98, 64, -52 + 0.2 * Math.sin(t * 3)); cobble.bb.set(98, 64, -52, 0.25, 0.25);
                book.q = new V(101, 65, -44); book.bb.set(101, 65, -44, 0.25, 0.25);
                frame.q = new V(99, 66, -46); frame.bb.set(99, 66, -46, 0.75, 0.75);
                // camera: first person at the eye, or 4 blocks behind (third person, back view)
                P me = (P) pl[0];
                double ey = pos[0][1] + 1.62, yr = Math.toRadians(me.f1), pr = Math.toRadians(me.f2);
                double fx = -Math.sin(yr) * Math.cos(pr), fyy = -Math.sin(pr), fz = Math.cos(yr) * Math.cos(pr);
                double k = third ? 4.0 : 0.0;
                m.gr.cam.p = new V(pos[0][0] - fx * k, ey - fyy * k, pos[0][2] - fz * k);
                m.gr.cam.yr = me.f1; m.gr.cam.xr = me.f2; m.gr.cam.det = third;
                m.gr.mod = (float) (1.0 + 0.1 * Math.sin(t * 0.8));                                  // sprint-style FOV modifier
            }
            if (((int) t) % 5 == 0 && t - Math.floor(t) < 0.05) System.out.printf("expected Alice yaw=%.1f pitch=%.1f fov=%.2f%n", yaw, pitch, 70.0 * m.gr.mod);
            Thread.sleep(16);
        }
    }
}
