package mcradar;

import java.io.File;
import java.io.FileOutputStream;
import java.lang.instrument.Instrumentation;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.util.*;

/**
 * Version-independent discovery of Minecraft's player objects.
 *
 * Minecraft's class and field names change every version and mapping (vanilla obfuscation,
 * Fabric intermediary, Forge SRG, Mojang names). So nothing here relies on a Minecraft name.
 * It relies on STRUCTURE, which is stable:
 *
 *  1. com.mojang.authlib.GameProfile is a library class and is never obfuscated.
 *  2. A "player class" = a class (with superclasses) that has a GameProfile field AND a position:
 *     either a small "Vec3-like" object (a class with exactly three double fields) or 3+ double fields.
 *     (The tab-list entry class has a GameProfile too, but no position -> rejected.)
 *  3. Starting from static fields (preferring self-typed singletons like Minecraft.instance) we walk
 *     the object graph breadth-first looking for a container (List/Set/Map/array) whose elements are
 *     ALL player objects. That is the level's player list. The path of fields leading there is
 *     remembered and replayed every tick, so it survives world changes.
 *  4. Which double triple is the position is decided by value: the position has the largest
 *     magnitude among Vec3-like fields/double triples (velocity is tiny, the position is not).
 *  5. Heading comes from a no-arg method returning a unit-length Vec3-like (the look vector).
 */
final class Probe {
    private static final String[] SKIP = {
        "java.", "javax.", "jdk.", "sun.", "com.sun.", "org.lwjgl.", "io.netty.", "com.google.",
        "org.apache.", "it.unimi.", "org.slf4j.", "org.joml.", "com.mojang.blaze3d.", "com.mojang.authlib.",
        "com.mojang.datafixers.", "com.mojang.serialization.", "com.mojang.brigadier.", "org.objectweb.",
        "org.spongepowered.", "kotlin.", "scala.", "org.jline.", "oshi.", "com.ibm.icu.", "org.w3c.",
        "org.xml.", "org.checkerframework.", "mcradar.", "net.fabricmc.", "net.minecraftforge.fml.",
        "cpw.mods.", "org.apache.logging."
    };

    private final Instrumentation inst;
    private final AgentConfig cfg;
    private final String username;

    private Class<?> gameProfile;
    private Method gpNameMethod;
    private Acc gpNameField;

    private final Map<Class<?>, Boolean> vecCache = new HashMap<Class<?>, Boolean>();
    private final Map<Class<?>, Acc[]> vecAccCache = new HashMap<Class<?>, Acc[]>();
    private final Map<Class<?>, Boolean> playerCache = new HashMap<Class<?>, Boolean>();
    private final Map<Class<?>, Acc[]> refFieldCache = new HashMap<Class<?>, Acc[]>();
    private final Map<Class<?>, Acc> profileFieldCache = new HashMap<Class<?>, Acc>();
    private final Map<Class<?>, Model> models = new HashMap<Class<?>, Model>();
    private final Map<Class<?>, Long> lastModelAttempt = new HashMap<Class<?>, Long>();
    private long lastModelRebuild;

    private FieldPath containerPath, localPath;
    private boolean ready;
    private int failTicks;
    private String status = "starting";

    Probe(Instrumentation inst, AgentConfig cfg) {
        this.inst = inst;
        this.cfg = cfg;
        this.username = cfg.username != null ? cfg.username : detectUsername();
        Log.info("Local username: " + (username == null ? "(unknown - will use shallowest player object)" : username));
    }

    boolean isReady() { return ready; }
    String status() { return status; }
    void reset() { ready = false; failTicks = 0; }

    /** Parses --username from the launch command. Other arguments (access tokens!) are never logged or exported. */
    private static String detectUsername() {
        String cmd = System.getProperty("sun.java.command");
        if (cmd == null) return null;
        String[] t = cmd.split("\\s+");
        for (int i = 0; i + 1 < t.length; i++) if (t[i].equals("--username")) return t[i + 1];
        return null;
    }

    // ------------------------------------------------------------------ class classification

    private static boolean isJdkish(Class<?> c) {
        String n = c.getName();
        for (String p : SKIP) if (n.startsWith(p)) return true;
        return n.contains("$$Lambda") || n.contains("/0x");
    }

    private static boolean containerish(Class<?> t) {
        return java.util.Collection.class.isAssignableFrom(t) || java.util.Map.class.isAssignableFrom(t)
                || t.isAssignableFrom(ArrayList.class) || t.isAssignableFrom(HashMap.class)
                || t.isAssignableFrom(java.util.concurrent.ConcurrentHashMap.class)
                || t.isAssignableFrom(java.util.concurrent.CopyOnWriteArrayList.class)
                || t.isAssignableFrom(LinkedList.class) || t.isAssignableFrom(ArrayDeque.class);
    }

    /** A class whose instances are "3 doubles and nothing else" (Vec3, Vec3d, Vector3d, ...). */
    private boolean isVec3Like(Class<?> t) {
        if (t.isPrimitive() || t.isArray() || t.isInterface() || isJdkish(t)) return false;
        Boolean b = vecCache.get(t);
        if (b != null) return b;
        boolean r = false;
        try {
            int d = 0, other = 0;
            for (Class<?> k = t; k != null && k != Object.class; k = k.getSuperclass())
                for (Field f : k.getDeclaredFields()) {
                    if (Modifier.isStatic(f.getModifiers())) continue;
                    if (f.getType() == double.class) d++; else other++;
                }
            r = d == 3 && other == 0 && vecAccessors(t) != null;
        } catch (Throwable ignored) { }
        vecCache.put(t, r);
        return r;
    }

    /** Accessors for the (x, y, z) doubles of a Vec3-like class, in declaration order. */
    private Acc[] vecAccessors(Class<?> t) {
        if (vecAccCache.containsKey(t)) return vecAccCache.get(t);
        Acc[] out = null;
        try {
            List<Acc> l = new ArrayList<Acc>();
            for (Class<?> k = t; k != null && k != Object.class; k = k.getSuperclass())
                for (Field f : k.getDeclaredFields())
                    if (!Modifier.isStatic(f.getModifiers()) && f.getType() == double.class) {
                        Acc a = Acc.of(f);
                        if (a == null) { l = null; break; }
                        l.add(a);
                    }
            if (l != null && l.size() == 3) out = l.toArray(new Acc[0]);
        } catch (Throwable ignored) { }
        vecAccCache.put(t, out);
        return out;
    }

    private boolean isPlayerClass(Class<?> c) {
        Boolean b = playerCache.get(c);
        if (b != null) return b;
        boolean r = false;
        if (gameProfile != null && !c.isInterface() && !c.isArray() && !isJdkish(c)) {
            try {
                boolean hasGp = false, hasVec = false;
                int doubles = 0;
                for (Class<?> k = c; k != null && k != Object.class; k = k.getSuperclass())
                    for (Field f : k.getDeclaredFields()) {
                        if (Modifier.isStatic(f.getModifiers())) continue;
                        Class<?> t = f.getType();
                        if (t == gameProfile) hasGp = true;
                        else if (t == double.class) doubles++;
                        else if (!t.isPrimitive() && isVec3Like(t)) hasVec = true;
                    }
                r = hasGp && (hasVec || doubles >= 3);
            } catch (Throwable ignored) { }
        }
        playerCache.put(c, r);
        return r;
    }

    /** Non-static reference fields worth following when walking the object graph. */
    private Acc[] refFieldsOf(Class<?> c) {
        Acc[] cached = refFieldCache.get(c);
        if (cached != null) return cached;
        List<Acc> l = new ArrayList<Acc>();
        try {
            for (Class<?> k = c; k != null && k != Object.class; k = k.getSuperclass()) {
                if (isJdkish(k)) break;
                for (Field f : k.getDeclaredFields()) {
                    if (Modifier.isStatic(f.getModifiers())) continue;
                    Class<?> t = f.getType();
                    if (t.isPrimitive() || t == String.class) continue;
                    if (!t.isArray() && t != Object.class && isJdkish(t) && !containerish(t)) continue;
                    Acc a = Acc.of(f);
                    if (a != null) l.add(a);
                }
            }
        } catch (Throwable ignored) { }
        Acc[] out = l.toArray(new Acc[0]);
        refFieldCache.put(c, out);
        return out;
    }

    private List<Acc> staticRoots(Class<?>[] all, boolean selfTypedOnly) {
        List<Acc> roots = new ArrayList<Acc>();
        for (Class<?> c : all) {
            try {
                if (c.isArray() || c.isInterface() || c.isPrimitive() || isJdkish(c)) continue;
                for (Field f : c.getDeclaredFields()) {
                    if (!Modifier.isStatic(f.getModifiers())) continue;
                    Class<?> t = f.getType();
                    if (t.isPrimitive() || t == String.class) continue;
                    if (selfTypedOnly && t != c) continue;
                    if (!t.isArray() && t != Object.class && isJdkish(t) && !containerish(t)) continue;
                    Acc a = Acc.of(f);
                    if (a != null) roots.add(a);
                }
            } catch (Throwable ignored) { /* NoClassDefFoundError etc. from half-loaded classes */ }
        }
        return roots;
    }

    private static boolean isContainer(Object o) {
        return o instanceof Collection || o instanceof Map || o instanceof Object[];
    }

    /** Snapshot of a container's elements; null on failure. Never iterates live (avoids CME). */
    private static Object[] elements(Object o) {
        try {
            if (o instanceof Object[]) return ((Object[]) o).clone();
            if (o instanceof Collection) return ((Collection<?>) o).toArray();
            if (o instanceof Map) return ((Map<?, ?>) o).values().toArray();
        } catch (Throwable ignored) { }
        return null;
    }

    // ------------------------------------------------------------------ player name

    private String nameOf(Object player) {
        try {
            Class<?> c = player.getClass();
            Acc pf;
            if (profileFieldCache.containsKey(c)) pf = profileFieldCache.get(c);
            else {
                pf = null;
                outer:
                for (Class<?> k = c; k != null && k != Object.class; k = k.getSuperclass())
                    for (Field f : k.getDeclaredFields())
                        if (!Modifier.isStatic(f.getModifiers()) && f.getType() == gameProfile) { pf = Acc.of(f); break outer; }
                profileFieldCache.put(c, pf);
            }
            if (pf == null) return null;
            Object gp = pf.getObj(player);
            if (gp == null) return null;
            if (gpNameMethod != null) {
                Object s = gpNameMethod.invoke(gp);
                return s instanceof String ? (String) s : null;
            }
            if (gpNameField != null) {
                Object s = gpNameField.getObj(gp);
                return s instanceof String ? (String) s : null;
            }
        } catch (Throwable ignored) { }
        return null;
    }

    // ------------------------------------------------------------------ discovery (graph search)

    private static final class Node {
        final Object obj; final Node parent; final Acc via; final int depth;
        Node(Object obj, Node parent, Acc via, int depth) { this.obj = obj; this.parent = parent; this.via = via; this.depth = depth; }
    }

    private static final class Cand {
        final Node parent; final Acc via; final int depth, count;
        Cand(Node parent, Acc via, int depth, int count) { this.parent = parent; this.via = via; this.depth = depth; this.count = count; }
    }

    private static FieldPath pathOf(Node parent, Acc last) {
        LinkedList<Acc> l = new LinkedList<Acc>();
        if (last != null) l.addFirst(last);
        for (Node n = parent; n != null; n = n.parent) l.addFirst(n.via);
        return new FieldPath(l);
    }

    private final class Search {
        final IdentityHashMap<Object, Boolean> seen = new IdentityHashMap<Object, Boolean>();
        final ArrayDeque<Node> queue = new ArrayDeque<Node>();
        final List<Cand> containers = new ArrayList<Cand>();
        final List<Node> locals = new ArrayList<Node>();
        final int maxNodes, maxDepth;

        Search(int maxNodes, int maxDepth) { this.maxNodes = maxNodes; this.maxDepth = maxDepth; }

        void consider(Object v, Node parent, Acc via, int depth) {
            if (v == null) return;
            Class<?> c = v.getClass();
            if (isContainer(v)) {
                if (seen.put(v, Boolean.TRUE) != null) return;
                Object[] el = elements(v);
                if (el == null || el.length == 0 || el.length > 2048) return;
                int n = 0;
                for (Object e : el) {
                    if (e == null) continue;
                    if (!isPlayerClass(e.getClass())) return;     // must contain ONLY players
                    n++;
                }
                if (n > 0) containers.add(new Cand(parent, via, depth, n));
                return;
            }
            if (isJdkish(c) || seen.put(v, Boolean.TRUE) != null) return;
            Node node = new Node(v, parent, via, depth);
            if (isPlayerClass(c)) { if (locals.size() < 32) locals.add(node); return; }
            if (depth < maxDepth) queue.add(node);
        }

        void run(List<Acc> roots, long deadline) {
            for (Acc r : roots) consider(r.getObj(null), null, r, 1);
            int foundDepth = Integer.MAX_VALUE;
            while (!queue.isEmpty()) {
                if (seen.size() > maxNodes || System.currentTimeMillis() > deadline) break;
                Node n = queue.poll();
                for (Cand c : containers) foundDepth = Math.min(foundDepth, c.depth);
                if (n.depth > foundDepth) break;                 // BFS: nothing shallower can still appear
                for (Acc a : refFieldsOf(n.obj.getClass())) consider(a.getObj(n.obj), n, a, n.depth + 1);
            }
        }
    }

    /** Returns true once a player container was found. Cheap to call repeatedly (caller backs off on failure). */
    boolean discover() {
        long t0 = System.currentTimeMillis();
        Class<?>[] all = inst.getAllLoadedClasses();

        if (gameProfile == null) {
            for (Class<?> c : all)
                if (c.getName().equals("com.mojang.authlib.GameProfile")) { gameProfile = c; break; }
            if (gameProfile == null) { status = "waiting: GameProfile class not loaded (is this a Minecraft JVM?)"; return false; }
            for (String n : new String[] {"getName", "name"}) {
                try {
                    Method m = gameProfile.getMethod(n);
                    if (m.getReturnType() == String.class) { gpNameMethod = m; break; }
                } catch (Throwable ignored) { }
            }
            if (gpNameMethod == null) {
                try { gpNameField = Acc.of(gameProfile.getDeclaredField("name")); } catch (Throwable ignored) { }
            }
            Log.info("Found " + gameProfile.getName() + " (loader " + gameProfile.getClassLoader() + ")");
        }
        if (!Mem.OK) { status = "Unsafe unavailable; add --sun-misc-unsafe-memory-access=allow to the game's JVM args"; return false; }

        // Stage 1: only self-typed singleton roots (Minecraft.instance-like). Small and fast.
        Search s = new Search(150_000, 5);
        s.run(staticRoots(all, true), t0 + 4000);
        if (s.containers.isEmpty()) {
            // Stage 2: every static field. Bigger, slower, last resort.
            s = new Search(500_000, 6);
            s.run(staticRoots(all, false), System.currentTimeMillis() + 8000);
        }
        if (s.containers.isEmpty()) {
            status = "searching: no player list yet (join a world)";
            return false;
        }

        Cand best = s.containers.get(0);
        for (Cand c : s.containers)
            if (c.depth < best.depth || (c.depth == best.depth && c.count > best.count)) best = c;
        containerPath = pathOf(best.parent, best.via);

        Node localNode = null;
        for (Node n : s.locals) {
            String nm = nameOf(n.obj);
            if (username != null && username.equals(nm)) { localNode = n; break; }
            if (localNode == null || n.depth < localNode.depth) localNode = n;
        }
        localPath = localNode == null ? null : pathOf(localNode.parent, localNode.via);

        ready = true;
        failTicks = 0;
        status = "ok";
        Log.info("Discovery done in " + (System.currentTimeMillis() - t0) + " ms; " + best.count
                + " player(s) in container at depth " + best.depth);
        Log.info("  player list path: " + containerPath.describe());
        if (localPath != null) Log.info("  local player path: " + localPath.describe());
        exportLayout();
        return true;
    }

    // ------------------------------------------------------------------ position / yaw model

    private static final class PosCand {
        final Acc vecField; final Acc[] xyz; final boolean vec;
        PosCand(Acc vecField, Acc[] xyz, boolean vec) { this.vecField = vecField; this.xyz = xyz; this.vec = vec; }
    }

    private static final class Model {
        PosCand pos;
        Method look;
        Acc[] lookXyz;
        Method healthMethod;

        boolean position(Object p, double[] o) {
            if (pos.vec) {
                Object v = pos.vecField.getObj(p);
                if (v == null) return false;
                for (int i = 0; i < 3; i++) o[i] = pos.xyz[i].getDouble(v);
            } else {
                for (int i = 0; i < 3; i++) o[i] = pos.xyz[i].getDouble(p);
            }
            return sane(o);
        }

        /** out[0] = yaw, out[1] = pitch (degrees); NaN when unknown. */
        void orient(Object p, float[] out) {
            out[0] = Float.NaN; out[1] = Float.NaN;
            if (look == null) return;
            try {
                Object v = look.invoke(p);
                if (v == null) return;
                double x = lookXyz[0].getDouble(v), y = lookXyz[1].getDouble(v), z = lookXyz[2].getDouble(v);
                if (Double.isNaN(x) || Double.isNaN(y) || Double.isNaN(z)) return;
                // Minecraft look vector: x = -sin(yaw)*cos(pitch), y = -sin(pitch), z = cos(yaw)*cos(pitch)
                if (Math.hypot(x, z) > 1e-6) out[0] = (float) Math.toDegrees(Math.atan2(-x, z));
                double n = Math.sqrt(x * x + y * y + z * z);
                if (n > 0.5) out[1] = (float) Math.toDegrees(-Math.asin(Math.max(-1.0, Math.min(1.0, y / n))));
            } catch (Throwable t) { /* leave NaN */ }
        }

        float health(Object p) {
            if (healthMethod == null) return Float.NaN;
            try {
                Object v = healthMethod.invoke(p);
                float f = v instanceof Number ? ((Number) v).floatValue() : Float.NaN;
                return (f >= 0 && f <= 2048) ? f : Float.NaN;
            } catch (Throwable t) { return Float.NaN; }
        }

        static boolean sane(double[] o) {
            for (double d : o) if (Double.isNaN(d) || Double.isInfinite(d) || Math.abs(d) > 3.2e7) return false;
            return true;
        }
    }

    private Model buildModel(Class<?> c, List<Object> samples) {
        // --- candidates for the position
        List<PosCand> cands = new ArrayList<PosCand>();
        for (Class<?> k = c; k != null && k != Object.class; k = k.getSuperclass()) {
            List<Field> doubles = new ArrayList<Field>();
            for (Field f : k.getDeclaredFields()) {
                if (Modifier.isStatic(f.getModifiers())) continue;
                Class<?> t = f.getType();
                if (t == double.class) doubles.add(f);
                else if (!t.isPrimitive() && isVec3Like(t)) {
                    Acc fa = Acc.of(f);
                    Acc[] va = vecAccessors(t);
                    if (fa != null && va != null) cands.add(new PosCand(fa, va, true));
                }
            }
            // Older versions (<=1.16) store posX/posY/posZ as plain double fields, grouped in triples.
            for (int i = 0; i + 2 < doubles.size(); i += 3) {
                Acc x = Acc.of(doubles.get(i)), y = Acc.of(doubles.get(i + 1)), z = Acc.of(doubles.get(i + 2));
                if (x != null && y != null && z != null) cands.add(new PosCand(null, new Acc[] {x, y, z}, false));
            }
        }
        // --- pick by value: biggest magnitude (position >> velocity), summed over all samples
        Model m = new Model();
        double bestScore = 0;
        double[] o = new double[3];
        for (PosCand pc : cands) {
            Model tmp = new Model();
            tmp.pos = pc;
            double score = 0;
            boolean ok = true;
            for (Object s : samples) {
                if (!tmp.position(s, o)) { ok = false; break; }
                score += Math.sqrt(o[0] * o[0] + o[1] * o[1] + o[2] * o[2]);
            }
            if (ok && score > bestScore) { bestScore = score; m.pos = pc; }
        }
        if (m.pos == null) return null;

        // --- heading: public no-arg method returning a unit-length Vec3-like (the look vector)
        List<Method> good = new ArrayList<Method>();
        for (Method me : c.getMethods()) {
            try {
                if (me.getParameterTypes().length != 0 || Modifier.isStatic(me.getModifiers())) continue;
                Class<?> rt = me.getReturnType();
                if (me.getDeclaringClass() == Object.class || !isVec3Like(rt)) continue;
                Acc[] va = vecAccessors(rt);
                try { me.setAccessible(true); } catch (Throwable ignored) { }
                boolean all = true;
                for (Object s : samples) {
                    Object v = me.invoke(s);
                    if (v == null) { all = false; break; }
                    double x = va[0].getDouble(v), y = va[1].getDouble(v), z = va[2].getDouble(v);
                    double n = Math.sqrt(x * x + y * y + z * z);
                    if (!(n > 0.99 && n < 1.01)) { all = false; break; }
                }
                if (all) good.add(me);
            } catch (Throwable ignored) { }
        }
        if (!good.isEmpty()) {
            // Prefer a look vector with a vertical component (the true 3-D view direction) over a
            // horizontal-only "forward" vector, so pitch is available. Then: base class first, then name.
            final Map<Method, Double> ySum = new HashMap<Method, Double>();
            for (Method me : good) {
                double sum = 0;
                Acc[] va = vecAccessors(me.getReturnType());
                for (Object s : samples) {
                    try { sum += Math.abs(va[1].getDouble(me.invoke(s))); } catch (Throwable ignored) { }
                }
                ySum.put(me, sum);
            }
            Collections.sort(good, new Comparator<Method>() {
                public int compare(Method a, Method b) {
                    int va = ySum.get(a) > 1e-3 ? 0 : 1, vb = ySum.get(b) > 1e-3 ? 0 : 1;
                    if (va != vb) return va - vb;
                    int d = depthOf(a.getDeclaringClass()) - depthOf(b.getDeclaringClass());
                    return d != 0 ? d : a.getName().compareTo(b.getName());
                }
            });
            m.look = good.get(0);
            m.lookXyz = vecAccessors(m.look.getReturnType());
        }

        // --- health: only a method literally named getHealth()/health() (works with Mojang, Yarn, MCP
        //     and Forge-mapped names; obfuscated/intermediary builds report health as unknown "-").
        for (Method me : c.getMethods()) {
            try {
                if (me.getParameterTypes().length != 0 || Modifier.isStatic(me.getModifiers())) continue;
                Class<?> rt = me.getReturnType();
                if (rt != float.class && rt != double.class) continue;
                String n = me.getName().toLowerCase(Locale.ROOT);
                if (!n.equals("gethealth") && !n.equals("health")) continue;
                try { me.setAccessible(true); } catch (Throwable ignored) { }
                boolean ok = true;
                for (Object s : samples) {
                    Object v = me.invoke(s);
                    float f = v instanceof Number ? ((Number) v).floatValue() : Float.NaN;
                    if (!(f >= 0 && f <= 2048)) { ok = false; break; }
                }
                if (ok) { try { me.setAccessible(true); } catch (Throwable ignored) { } m.healthMethod = me; break; }
            } catch (Throwable ignored) { }
        }
        return m;
    }

    private static int depthOf(Class<?> c) {
        int d = 0;
        for (Class<?> k = c; k != null; k = k.getSuperclass()) d++;
        return d;
    }

    // ------------------------------------------------------------------ per-tick read

    Snapshot read() {
        Snapshot s = new Snapshot();
        if (!ready) { s.status = status; return s; }

        Object container = containerPath.follow();
        Object[] el = container == null ? null : elements(container);
        if (el == null) {
            if (++failTicks > 60) { Log.info("Player list path went stale; re-discovering."); ready = false; status = "re-discovering"; }
            s.status = "no world loaded (player list unavailable)";
            return s;
        }
        failTicks = 0;

        List<Object> players = new ArrayList<Object>();
        for (Object e : el) if (e != null && isPlayerClass(e.getClass())) players.add(e);

        Object localObj = localPath == null ? null : localPath.follow();
        if (localObj != null && !isPlayerClass(localObj.getClass())) localObj = null;
        String localName = localObj == null ? null : nameOf(localObj);
        if (localObj == null && username != null)
            for (Object p : players) if (username.equals(nameOf(p))) { localObj = p; localName = username; break; }

        // (Re)build models per class from the live samples.
        Map<Class<?>, List<Object>> byClass = new LinkedHashMap<Class<?>, List<Object>>();
        for (Object p : players) addSample(byClass, p);
        if (localObj != null) addSample(byClass, localObj);
        long now = System.currentTimeMillis();
        boolean rebuild = now - lastModelRebuild > 15000;
        boolean built = false;
        for (Map.Entry<Class<?>, List<Object>> e : byClass.entrySet()) {
            Class<?> c = e.getKey();
            Long last = lastModelAttempt.get(c);
            if ((!models.containsKey(c) && (last == null || now - last > 1000)) || (rebuild && models.containsKey(c))) {
                lastModelAttempt.put(c, now);
                Model m = buildModel(c, e.getValue());
                if (m != null) { models.put(c, m); built = true; }
            }
        }
        if (rebuild) lastModelRebuild = now;
        if (built) exportLayout();

        double[] p3 = new double[3];
        float[] o2 = new float[2];
        if (localObj != null) {
            Model m = models.get(localObj.getClass());
            if (m != null && m.position(localObj, p3)) {
                m.orient(localObj, o2);
                s.local = new Snapshot.Rec(localName, p3[0], p3[1], p3[2], o2[0], o2[1], m.health(localObj));
            }
        }
        for (Object p : players) {
            if (p == localObj) continue;
            String nm = nameOf(p);
            if (localName != null && localName.equals(nm)) continue;
            Model m = models.get(p.getClass());
            if (m == null || !m.position(p, p3)) continue;
            m.orient(p, o2);
            s.players.add(new Snapshot.Rec(nm, p3[0], p3[1], p3[2], o2[0], o2[1], m.health(p)));
        }
        s.status = s.local == null ? "connected, but local player not found (set user=NAME?)"
                                   : "agent ok: " + s.players.size() + " other player(s)";
        return s;
    }

    private static void addSample(Map<Class<?>, List<Object>> m, Object p) {
        List<Object> l = m.get(p.getClass());
        if (l == null) { l = new ArrayList<Object>(); m.put(p.getClass(), l); }
        l.add(p);
    }

    // ------------------------------------------------------------------ export of discovered offsets

    private static String esc(String s) {
        if (s == null) return "null";
        StringBuilder b = new StringBuilder("\"");
        for (char ch : s.toCharArray()) {
            if (ch == '"' || ch == '\\') b.append('\\').append(ch);
            else if (ch < 0x20) b.append(' ');
            else b.append(ch);
        }
        return b.append('"').toString();
    }

    private static String accJson(Acc a) {
        return "{\"class\":" + esc(a.owner.getName()) + ",\"field\":" + esc(a.f.getName())
                + ",\"type\":" + esc(a.f.getType().getName()) + ",\"static\":" + a.isStatic
                + ",\"offset\":" + (a.off >= 0 ? Long.toString(a.off) : "null") + "}";
    }

    private static String pathJson(FieldPath p) {
        if (p == null) return "null";
        StringBuilder b = new StringBuilder("[");
        for (int i = 0; i < p.steps.length; i++) { if (i > 0) b.append(','); b.append(accJson(p.steps[i])); }
        return b.append(']').toString();
    }

    /**
     * Writes everything that was discovered to esp_layout.json: the field paths and the
     * in-object byte offsets (as reported by Unsafe) - i.e. the "offsets" for THIS exact game
     * version / mapping / JVM. Offsets inside an object can be reused with ReadProcessMemory.
     * (Deliberately does NOT include the launch command line - it contains your access token.)
     */
    private void exportLayout() {
        try {
            StringBuilder b = new StringBuilder();
            b.append("{\n  \"generated\": ").append(esc(new Date().toString()))
             .append(",\n  \"java\": ").append(esc(System.getProperty("java.version") + " / " + System.getProperty("java.vm.name")))
             .append(",\n  \"username\": ").append(esc(username))
             .append(",\n  \"unsafe\": ").append(Mem.OK)
             .append(",\n  \"gameProfileClass\": ").append(esc(gameProfile == null ? null : gameProfile.getName()))
             .append(",\n  \"playerListPath\": ").append(pathJson(containerPath))
             .append(",\n  \"localPlayerPath\": ").append(pathJson(localPath))
             .append(",\n  \"playerClasses\": [");
            boolean first = true;
            for (Map.Entry<Class<?>, Model> e : models.entrySet()) {
                Model m = e.getValue();
                if (!first) b.append(',');
                first = false;
                b.append("\n    {\"class\": ").append(esc(e.getKey().getName())).append(", \"position\": ");
                if (m.pos.vec) {
                    b.append("{\"kind\":\"vec3\",\"field\":").append(accJson(m.pos.vecField))
                     .append(",\"x\":").append(accJson(m.pos.xyz[0])).append(",\"y\":").append(accJson(m.pos.xyz[1]))
                     .append(",\"z\":").append(accJson(m.pos.xyz[2])).append('}');
                } else {
                    b.append("{\"kind\":\"doubles\",\"x\":").append(accJson(m.pos.xyz[0])).append(",\"y\":")
                     .append(accJson(m.pos.xyz[1])).append(",\"z\":").append(accJson(m.pos.xyz[2])).append('}');
                }
                b.append(", \"lookMethod\": ").append(m.look == null ? "null"
                        : esc(m.look.getDeclaringClass().getName() + "." + m.look.getName() + "() -> " + m.look.getReturnType().getName()))
                 .append(", \"healthMethod\": ").append(m.healthMethod == null ? "null"
                        : esc(m.healthMethod.getDeclaringClass().getName() + "." + m.healthMethod.getName() + "()"))
                 .append('}');
            }
            b.append("\n  ]\n}\n");
            File dir = new File(cfg.exportDir);
            if (!dir.isDirectory()) dir.mkdirs();
            File out = new File(dir, "esp_layout.json");
            FileOutputStream fos = new FileOutputStream(out);
            try { fos.write(b.toString().getBytes("UTF-8")); } finally { fos.close(); }
        } catch (Throwable t) {
            Log.info("Could not write layout export: " + t);
        }
    }
}
