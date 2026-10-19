import com.mojang.authlib.GameProfile;
import it.unimi.dsi.fastutil.ints.Int2ObjectLinkedOpenHashMap;
import it.unimi.dsi.fastutil.objects.Object2IntOpenHashMap;
import java.util.*;

/**
 * A fake Minecraft 1.21.x with the REAL structural quirks that FakeGame lacks:
 *   - ItemEntity has NO ItemStack field: the stack lives in entityData -> DataItem[] -> value
 *     (and DataItem.initialValue holds ItemStack.EMPTY, count 0)
 *   - ItemStack = Holder<Item> + int count + components + back-reference to an Entity
 *   - entities live in ClientLevel -> TransientEntityManager -> EntityLookup -> fastutil map (deep),
 *     below a big decoy object graph that exhausts a root-level BFS
 *   - the entity map is mutated by the "game thread" while the agent reads it
 * Flags: nokey (ItemStack has no String key method; id only via Holder/Item), nodecoy
 */
final class Vec3 { final double x, y, z; Vec3(double x, double y, double z) { this.x = x; this.y = y; this.z = z; } }
final class AABB { final double minX, minY, minZ, maxX, maxY, maxZ;
    AABB(double x, double y, double z, double w, double h) { minX = x - w / 2; minY = y; minZ = z - w / 2; maxX = x + w / 2; maxY = y + h; maxZ = z + w / 2; } }

final class Accessor { final int id; final Object serializer = new Object(); Accessor(int id) { this.id = id; } }
final class DataItem { final Accessor accessor; Object value; final Object initialValue; boolean dirty;
    DataItem(Accessor a, Object v, Object init) { accessor = a; value = v; initialValue = init; } }
final class SynchedData { final Object entity; final DataItem[] itemsById; boolean isDirty;
    SynchedData(Object e, int n) { entity = e; itemsById = new DataItem[n]; } }

class Entity {
    Vec3 position = new Vec3(0, 0, 0), deltaMovement = new Vec3(0, 0, 0);
    double xo, yo, zo;
    AABB bb = new AABB(0, 0, 0, 0.6, 1.8);
    SynchedData entityData;
    ClientLevel level;
    Object wide;
    float yRot, xRot;
    int tickCount;
    public Vec3 getLookAngle() { double y = Math.toRadians(yRot), p = Math.toRadians(xRot);
        return new Vec3(-Math.sin(y) * Math.cos(p), -Math.sin(p), Math.cos(y) * Math.cos(p)); }
    public float getHealth() { return 20f; }
    void moveTo(double x, double y, double z, double w, double h) { xo = position.x; yo = position.y; zo = position.z;
        position = new Vec3(x, y, z); bb = new AABB(x, y, z, w, h); }
}
class AbstractPlayer extends Entity { GameProfile gameProfile; ItemStack lastItemInMainHand = RealisticGame.EMPTY; }
class LocalPlayer extends AbstractPlayer { }
class RemotePlayer extends AbstractPlayer { }
class Mob extends Entity { ItemStack useItem = RealisticGame.EMPTY; }

final class ItemKey { final String n; ItemKey(String n) { this.n = n; } public String toString() { return "ResourceKey[minecraft:item / minecraft:" + n + "]"; } }
class Item { private final String descriptionId; Item(String id, boolean block) { descriptionId = (block ? "block.minecraft." : "item.minecraft.") + id; }
    public String getDescriptionId() { return descriptionId; } }
final class HolderRef { final ItemKey key; final Item value; HolderRef(String id, boolean block) { key = new ItemKey(id); value = new Item(id, block); }
    public String toString() { return "Reference{" + key + "=" + value.getClass().getName() + "@1f}"; } }
final class Ench { final String id; Ench(String i) { id = i; } public String toString() { return "Reference{ResourceKey[minecraft:enchantment / minecraft:" + id + "]=net.fake.Enchantment@1}"; } }
final class ItemEnchantments { final Object2IntOpenHashMap<Object> enchantments = new Object2IntOpenHashMap<Object>(); boolean showInTooltip = true; }
final class CompType { final String n; CompType(String n) { this.n = n; } public String toString() { return "minecraft:" + n; } }
final class PatchedComponents { final Object prototype = new Object(); final Map<Object, Object> patch = new LinkedHashMap<Object, Object>(); }

/** Stack with a String key method (variant A, 1.21.x ItemStack.getDescriptionId) or without (variant B). */
class ItemStack {
    final HolderRef item; int count; final PatchedComponents components; Entity entityRepresentation;
    ItemStack(HolderRef i, int c, PatchedComponents p) { item = i; count = c; components = p; }
    boolean isEmpty() { return count <= 0; }
}
final class ItemStackA extends ItemStack {
    ItemStackA(HolderRef i, int c, PatchedComponents p) { super(i, c, p); }
    public String getDescriptionId() { return item.value.getDescriptionId(); }
}

final class ItemEntity extends Entity { int age, pickupDelay = 10; float bobOffs = 0.3f; UUID thrower; }
final class ItemFrame extends Entity { }
final class Wide { int a0,a1,a2,a3,a4,a5,a6,a7,a8,a9,b0,b1,b2,b3,b4,b5,b6,b7,b8,b9; Object inner; Wide(Object i) { inner = i; } }

final class EntityLookup { final Int2ObjectLinkedOpenHashMap<Entity> byId = new Int2ObjectLinkedOpenHashMap<Entity>(); final Map<UUID, Entity> byUuid = new HashMap<UUID, Entity>(); }
final class TransientEntityManager { final Object callbacks = new Object(); final EntityLookup entityStorage = new EntityLookup(); final Object sectionStorage = new Object(); }
class ClientLevel {
    final Object tickingEntities = new Object();
    final TransientEntityManager entityStorage = new TransientEntityManager();
    final List<AbstractPlayer> players = new ArrayList<AbstractPlayer>();
    final Object[] misc = new Object[4];
}
final class Gui { final Object[] kids; Gui(int n, int depth) { kids = new Object[n]; for (int i = 0; i < n && depth > 0; i++) kids[i] = new Gui(Math.max(2, n / 2 + 1), depth - 1); } }
final class Minecraft {
    static Minecraft instance;
    Object[] decoys;                 // BEFORE level in field order: a root BFS drowns in these
    ClientLevel level; LocalPlayer player;
    Minecraft() { }
}

public class RealisticGame {
    static ItemStack EMPTY = new ItemStack(null, 0, new PatchedComponents());                // real ItemStack.EMPTY has item == null

    static ItemStack stack(boolean nokey, String id, int n, boolean block) {
        HolderRef h = new HolderRef(id, block);
        return nokey ? new ItemStack(h, n, new PatchedComponents()) : new ItemStackA(h, n, new PatchedComponents());
    }

    /** ItemEntity.defineSynchedData-like layout: only the last slot carries the stack. */
    static void putItem(ItemEntity e, ItemStack s) {
        e.entityData = new SynchedData(e, 9);
        Object[] vals = { (byte) 0, 300, Optional.empty(), Boolean.FALSE, Boolean.FALSE, Boolean.FALSE, "STANDING", 0, null };
        for (int i = 0; i < 8; i++) e.entityData.itemsById[i] = new DataItem(new Accessor(i), vals[i], vals[i]);
        e.entityData.itemsById[8] = new DataItem(new Accessor(8), s, EMPTY);
        s.entityRepresentation = e;
    }

    static GameProfile gp(String n) { return new GameProfile(UUID.nameUUIDFromBytes(("OfflinePlayer:" + n).getBytes()), n); }

    public static void main(String[] args) throws Exception {
        List<String> a = Arrays.asList(args);
        boolean nokey = a.contains("nokey");
        if (!nokey) EMPTY = new ItemStackA(null, 0, new PatchedComponents());     // key method present but throws (NPE) on the empty stack
        final Minecraft mc = new Minecraft(); Minecraft.instance = mc;
        if (!a.contains("nodecoy")) { mc.decoys = new Object[60]; for (int i = 0; i < 60; i++) mc.decoys[i] = new Gui(40, 3); }
        mc.level = new ClientLevel();
        final EntityLookup lookup = mc.level.entityStorage.entityStorage;
        String[] names = {"Alice", "Bob"};
        int id = 0;
        for (int i = 0; i < 2; i++) {
            AbstractPlayer p = i == 0 ? new LocalPlayer() : new RemotePlayer();
            p.gameProfile = gp(names[i]); p.level = mc.level; p.entityData = new SynchedData(p, 3);
            mc.level.players.add(p); lookup.byId.put(id++, p);
            if (i == 0) mc.player = (LocalPlayer) p;
        }
        final ItemEntity sword = new ItemEntity(), cobble = new ItemEntity(), book = new ItemEntity(), apple = new ItemEntity();
        ItemStack ss = stack(nokey, "diamond_sword", 1, false); putItem(sword, ss);
        ItemEnchantments ie = new ItemEnchantments(); ie.enchantments.put(new Ench("sharpness"), 5); ie.enchantments.put(new Ench("unbreaking"), 3);
        ss.components.patch.put(new CompType("enchantments"), Optional.of(ie));
        putItem(cobble, stack(nokey, "cobblestone", 37, true));
        putItem(book, stack(nokey, "enchanted_book", 1, false));
        putItem(apple, stack(nokey, "golden_apple", 3, false));
        ItemFrame frame = new ItemFrame(); putItem2(frame, stack(nokey, "map", 1, false));
        Mob mob = new Mob();
        if (a.contains("hidden")) for (ItemEntity ie2 : new ItemEntity[] {sword, cobble, book, apple}) { ie2.wide = new Wide(ie2.entityData); ie2.entityData = null; }   // layout the search cannot see
        Mob mob2 = new Mob(), mob3 = new Mob();
        for (Entity e : new Entity[] {mob, mob2, mob3, sword, cobble, book, apple, frame}) { e.level = mc.level; lookup.byId.put(id++, e); }   // mobs first, like a real world

        System.out.println("PID " + java.lang.management.ManagementFactory.getRuntimeMXBean().getName());
        long t0 = System.currentTimeMillis(); int churn = 100;
        while (System.currentTimeMillis() - t0 < 120000) {
            double t = (System.currentTimeMillis() - t0) / 1000.0;
            mc.player.yRot = (float) (-30 + 20 * Math.sin(t * 1.5)); mc.player.xRot = (float) (10 + 8 * Math.sin(t * 2.1));
            mc.player.moveTo(100, 64, -50, 0.6, 1.8); lookup.byId.get(1).moveTo(110, 64, -40, 0.6, 1.8);
            sword.moveTo(103, 64, -47, .25, .25); cobble.moveTo(98, 64, -52, .25, .25); book.moveTo(101, 65, -44, .25, .25);
            apple.moveTo(96, 64, -49, .25, .25); frame.moveTo(99, 66, -46, .75, .75); mob.moveTo(105, 64, -55, .6, 1.8); mob2.moveTo(106, 64, -55, .6, 1.8); mob3.moveTo(107, 64, -55, .6, 1.8);
            // game thread churn: spawn/despawn an extra dropped item every ~1.5 s
            if ((int) (t * 100) % 150 == 0) {
                ItemEntity x = new ItemEntity(); x.level = mc.level; putItem(x, stack(nokey, "rotten_flesh", 2, false)); x.moveTo(97, 64, -48, .25, .25);
                lookup.byId.put(churn, x); lookup.byId.remove(churn - 1 > 99 ? churn - 1 : -5); churn++;
            }
            Thread.sleep(10);
        }
    }
    static void putItem2(Entity e, ItemStack s) { e.entityData = new SynchedData(e, 9);
        e.entityData.itemsById[8] = new DataItem(new Accessor(8), s, EMPTY); }
}
