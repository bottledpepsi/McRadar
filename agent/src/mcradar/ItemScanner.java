package mcradar;

import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashMap;
import java.util.IdentityHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

/**
 * Finds dropped-item entities and reads their stack (type, count, enchantments, tags) - version independent.
 *
 *  ItemStack     = a small non-JDK class with an int field in 1..99 (the count) and a public no-arg String
 *                  method that returns a translation key ("item.minecraft.diamond_sword", "block.minecraft.stone",
 *                  legacy "item.diamondSword"). Keys are stable strings even when class/method names are obfuscated.
 *  Item entity   = an entity-like class (it has a position) with a field holding such a stack, whose hitbox is
 *                  small (~0.25 blocks) when a bounding box is available (filters item frames, arrows, ...).
 *  Containers    = AuxSearch's entity containers (the level's entity storage); the largest one is polled.
 */
final class ItemScanner {
    private final Probe p;
    private final List<FieldPath> containers = new ArrayList<FieldPath>();
    private FieldPath best;
    private long lastPick;

    private static final class StackModel { Method key; Acc count; }
    private static final class ItemClass { Acc stackField; Probe.Model model; }
    private static final class Meta { int count; String key, ench, tags; int gen; }

    private final Map<Class<?>, StackModel> stackModels = new HashMap<Class<?>, StackModel>();
    private final Map<Class<?>, ItemClass> itemClasses = new HashMap<Class<?>, ItemClass>();
    private final Map<Class<?>, Boolean> itemObjClass = new HashMap<Class<?>, Boolean>();
    private final IdentityHashMap<Object, Meta> meta = new IdentityHashMap<Object, Meta>();
    private int gen;
    String describe = "items: no entity container found yet";

    ItemScanner(Probe p) { this.p = p; }

    void setContainers(List<FieldPath> paths) {
        containers.clear();
        containers.addAll(paths);
        best = null;
        lastPick = 0;
        Log.info("ItemScanner: " + containers.size() + " entity container candidate(s).");
    }

    // ------------------------------------------------------------------ classification

    private StackModel stackModel(Object v) {
        Class<?> c = v.getClass();
        if (stackModels.containsKey(c)) return stackModels.get(c);
        StackModel sm = null;
        try {
            if (!Probe.isJdkish(c) && !c.isArray()) {
                int total = 0, doubles = 0;
                Acc count = null;
                for (Class<?> k = c; k != null && k != Object.class; k = k.getSuperclass())
                    for (Field f : k.getDeclaredFields()) {
                        if (Modifier.isStatic(f.getModifiers())) continue;
                        total++;
                        if (f.getType() == double.class) doubles++;
                        else if (f.getType() == int.class && count == null) {
                            Acc a = Acc.of(f);
                            int n = a == null ? Integer.MIN_VALUE : a.getInt(v);
                            if (n >= 1 && n <= 99) count = a;           // first declared int that looks like a count
                        }
                    }
                if (count != null && doubles == 0 && total <= 24) {
                    for (Method m : c.getMethods()) {
                        if (m.getParameterTypes().length != 0 || m.getReturnType() != String.class
                                || Modifier.isStatic(m.getModifiers()) || m.getDeclaringClass() == Object.class) continue;
                        try {
                            m.setAccessible(true);
                            Object r = m.invoke(v);
                            if (r instanceof String && Tags.looksLikeItemKey((String) r)) { sm = new StackModel(); sm.key = m; sm.count = count; break; }
                        } catch (Throwable ignored) { }
                    }
                }
            }
        } catch (Throwable ignored) { }
        stackModels.put(c, sm);
        return sm;
    }

    /** True for the registry "Item" object (it also has a translation-key method): never treat it as tag data. */
    private boolean isItemObject(Object v) {
        Class<?> c = v.getClass();
        Boolean b = itemObjClass.get(c);
        if (b != null) return b;
        boolean r = false;
        try {
            if (!Probe.isJdkish(c)) {
                for (Method m : c.getMethods()) {
                    if (m.getParameterTypes().length != 0 || m.getReturnType() != String.class || m.getDeclaringClass() == Object.class) continue;
                    try { m.setAccessible(true); Object s = m.invoke(v); if (s instanceof String && Tags.looksLikeItemKey((String) s)) { r = true; break; } }
                    catch (Throwable ignored) { }
                }
            }
        } catch (Throwable ignored) { }
        itemObjClass.put(c, r);
        return r;
    }

    private ItemClass classify(Object e) {
        Class<?> c = e.getClass();
        if (itemClasses.containsKey(c)) return itemClasses.get(c);
        ItemClass res = null;
        try {
            if (!Probe.isJdkish(c) && !p.isPlayerClass(c) && !p.posCandidates(c).isEmpty()) {
                for (Acc a : p.refFieldsOf(c)) {
                    Object v = a.getObj(e);
                    if (v != null && stackModel(v) != null) {
                        List<Object> sample = new ArrayList<Object>();
                        sample.add(e);
                        Probe.Model m = p.buildModel(c, sample);
                        if (m != null) { res = new ItemClass(); res.stackField = a; res.model = m; }
                        break;
                    }
                }
            }
        } catch (Throwable ignored) { }
        itemClasses.put(c, res);
        if (res != null) Log.info("Item entity class: " + c.getName() + " (stack field " + res.stackField.describe() + ")");
        return res;
    }

    // ------------------------------------------------------------------ stack contents

    private Meta readMeta(Object stack, StackModel sm, int count) {
        Meta m = meta.get(stack);
        if (m != null && m.count == count) { m.gen = gen; return m; }
        m = new Meta();
        m.count = count;
        m.gen = gen;
        try {
            Object k = sm.key.invoke(stack);
            m.key = Tags.idFromKey(String.valueOf(k));
        } catch (Throwable t) { m.key = "unknown"; }

        Set<String> ench = new LinkedHashSet<String>();
        StringBuilder tags = new StringBuilder();
        try {
            for (Acc a : p.refFieldsOf(stack.getClass())) {
                Object v = a.getObj(stack);
                if (v == null || isItemObject(v)) continue;
                String txt = Tags.safeText(v, 1500);
                if (!txt.isEmpty() && !txt.equals("{}") && !txt.equals("[]") && !txt.equals("null")) {
                    if (tags.length() > 0) tags.append(" | ");
                    tags.append(txt);
                }
                Tags.enchantsFromText(txt, ench);
                Tags.findEnchantMaps(p, v, ench);
            }
        } catch (Throwable ignored) { }
        m.ench = join(ench);
        m.tags = tags.length() > 900 ? tags.substring(0, 900) + "..." : tags.toString();
        meta.put(stack, m);
        return m;
    }

    private static String join(Set<String> s) {
        StringBuilder b = new StringBuilder();
        for (String x : s) { if (b.length() > 0) b.append(','); b.append(x); }
        return b.toString();
    }

    // ------------------------------------------------------------------ polling

    List<Snapshot.Item> poll(double[] eye, int max) {
        List<Snapshot.Item> out = new ArrayList<Snapshot.Item>();
        if (containers.isEmpty()) return out;
        long now = System.currentTimeMillis();
        Object[] elements = null;
        if (best == null || now - lastPick > 3000) {                    // re-pick the biggest entity container
            lastPick = now;
            FieldPath bp = null; int bn = 0;
            for (FieldPath fp : containers) {
                Object c = fp.follow();
                Object[] el = c == null ? null : Probe.elements(c);
                if (el != null && el.length > bn) { bn = el.length; bp = fp; elements = el; }
            }
            if (bp != null && bp != best) { best = bp; describe = "items: " + bp.describe(); Log.info("Entity container: " + describe); }
            if (bp == null) return out;
        }
        if (elements == null) {
            Object c = best == null ? null : best.follow();
            elements = c == null ? null : Probe.elements(c);
            if (elements == null) { best = null; return out; }
        }

        ++gen;
        double[] pos = new double[3];
        float[] size = new float[2];
        final List<double[]> keyDist = new ArrayList<double[]>();
        for (Object e : elements) {
            if (e == null) continue;
            ItemClass ic = classify(e);
            if (ic == null) continue;
            try {
                Object stack = ic.stackField.getObj(e);
                if (stack == null) continue;
                StackModel sm = stackModel(stack);
                if (sm == null) continue;
                int count = sm.count.getInt(stack);
                if (count < 1 || count > 999) continue;
                if (!ic.model.position(e, pos)) continue;
                float w = Float.NaN, h = Float.NaN;
                if (ic.model.size(e, size)) {
                    if (size[0] > 0.45f) continue;                       // not a dropped item (item frame, arrow, ...)
                    w = size[0]; h = size[1];
                }
                Meta m = readMeta(stack, sm, count);
                out.add(new Snapshot.Item(pos[0], pos[1], pos[2], w, h, count, m.key, m.ench, m.tags));
            } catch (Throwable ignored) { }
        }
        // forget stacks that vanished
        if (meta.size() > out.size() + 64) {
            java.util.Iterator<Map.Entry<Object, Meta>> it = meta.entrySet().iterator();
            while (it.hasNext()) if (it.next().getValue().gen != gen) it.remove();
        }
        final double ex = eye[0], ey = eye[1], ez = eye[2];
        Collections.sort(out, new Comparator<Snapshot.Item>() {      // nearest first (the encoder stops at the size budget)
            public int compare(Snapshot.Item a, Snapshot.Item b) {
                double da = (a.x - ex) * (a.x - ex) + (a.y - ey) * (a.y - ey) + (a.z - ez) * (a.z - ez);
                double db = (b.x - ex) * (b.x - ex) + (b.y - ey) * (b.y - ey) + (b.z - ez) * (b.z - ez);
                return Double.compare(da, db);
            }
        });
        if (out.size() > max) return new ArrayList<Snapshot.Item>(out.subList(0, max));
        return out;
    }
}
