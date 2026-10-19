package mcradar;

import java.lang.reflect.ParameterizedType;
import java.lang.reflect.Type;
import java.lang.reflect.Field;
import java.lang.reflect.Modifier;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.IdentityHashMap;
import java.util.List;
import java.util.Map;

/**
 * Second graph search, started from the game's root singleton (the object that holds the local player):
 *   - CAMERA candidates : non-player objects with a position (Vec3 / double triple) and >= 2 float fields.
 *     The scanner later decides by VALUE which one really is the camera (its position follows the player's eye
 *     and its two angles follow the player's yaw/pitch).
 *   - ENTITY containers : Lists / Sets / Maps whose elements are entity-like (declared generic element type, or
 *     runtime elements) - the level's "all entities" storage, where dropped items live.
 */
final class AuxSearch {
    final List<FieldPath> cameraCands = new ArrayList<FieldPath>();
    final List<FieldPath> entityContainers = new ArrayList<FieldPath>();

    private final Probe p;
    private final Map<Class<?>, Boolean> entityLike = new HashMap<Class<?>, Boolean>();

    AuxSearch(Probe p) { this.p = p; }

    boolean isEntityLike(Class<?> c) {
        if (c == null || c.isPrimitive() || c.isInterface() || Probe.isJdkish(c)) return false;
        Boolean b = entityLike.get(c);
        if (b == null) {
            boolean r = false;
            try { r = !p.posCandidates(c).isEmpty(); } catch (Throwable ignored) { }
            entityLike.put(c, r);
            b = r;
        }
        return b;
    }

    private boolean cameraLike(Class<?> c) {
        if (!isEntityLike(c) || p.isPlayerClass(c)) return false;
        int floats = 0;
        for (Class<?> k = c; k != null && k != Object.class; k = k.getSuperclass())
            for (Field f : k.getDeclaredFields())
                if (!Modifier.isStatic(f.getModifiers()) && f.getType() == float.class) floats++;
        return floats >= 2;
    }

    /** Declared element type of a container field (List<E>, Map<K,V> -> V, E[]) if it is entity-like. */
    private boolean declaredEntityElements(Field f) {
        try {
            Class<?> t = f.getType();
            if (t.isArray()) return isEntityLike(t.getComponentType());
            Type g = f.getGenericType();
            if (g instanceof ParameterizedType) {
                for (Type a : ((ParameterizedType) g).getActualTypeArguments())
                    if (a instanceof Class && isEntityLike((Class<?>) a)) return true;
            }
        } catch (Throwable ignored) { }
        return false;
    }

    /** BFS from `root` (the object at the end of `rootStep`, a static field). */
    void search(Object root, Acc rootStep, int maxDepth, int maxNodes, long budgetMs) {
        cameraCands.clear();
        entityContainers.clear();
        final long deadline = System.currentTimeMillis() + budgetMs;
        IdentityHashMap<Object, Boolean> seen = new IdentityHashMap<Object, Boolean>();
        ArrayDeque<Probe.Node> q = new ArrayDeque<Probe.Node>();
        seen.put(root, Boolean.TRUE);
        q.add(new Probe.Node(root, null, rootStep, 1));

        while (!q.isEmpty() && seen.size() < maxNodes && System.currentTimeMillis() < deadline) {
            Probe.Node n = q.poll();
            for (Acc a : p.refFieldsOf(n.obj.getClass())) {
                Object v = a.getObj(n.obj);
                if (v == null) continue;
                Class<?> c = v.getClass();
                if (Probe.isContainer(v)) {
                    if (seen.put(v, Boolean.TRUE) != null) continue;
                    Object[] el = Probe.elements(v);
                    boolean entities = declaredEntityElements(a.f);
                    if (!entities && el != null) {
                        int n2 = 0;
                        for (Object e : el) { if (e != null && isEntityLike(e.getClass())) { entities = true; break; } if (++n2 > 64) break; }
                    }
                    if (entities) entityContainers.add(Probe.pathOf(n, a));
                    continue;
                }
                if (Probe.isJdkish(c) || p.isPlayerClass(c) || seen.put(v, Boolean.TRUE) != null) continue;
                if (cameraLike(c)) cameraCands.add(Probe.pathOf(n, a));
                if (isEntityLike(c)) continue;                          // do not walk into entities
                if (n.depth < maxDepth) q.add(new Probe.Node(v, n, a, n.depth + 1));
            }
        }
    }

    /**
     * Entity-container search rooted at an inner object (the level that owns the player list), with its own budget.
     * Adds containers not already known (deduplicated by container identity).
     */
    void searchEntitiesFrom(Probe.Node start, int maxDepth, int maxNodes, long budgetMs) {
        final long deadline = System.currentTimeMillis() + budgetMs;
        IdentityHashMap<Object, Boolean> known = new IdentityHashMap<Object, Boolean>();
        for (FieldPath fp : entityContainers) { Object c = fp.follow(); if (c != null) known.put(c, Boolean.TRUE); }
        IdentityHashMap<Object, Boolean> seen = new IdentityHashMap<Object, Boolean>();
        ArrayDeque<Probe.Node> q = new ArrayDeque<Probe.Node>();
        seen.put(start.obj, Boolean.TRUE);
        q.add(start);
        int added = 0;
        while (!q.isEmpty() && seen.size() < maxNodes && System.currentTimeMillis() < deadline) {
            Probe.Node n = q.poll();
            for (Acc a : p.refFieldsOf(n.obj.getClass())) {
                Object v = a.getObj(n.obj);
                if (v == null) continue;
                if (Probe.isContainer(v)) {
                    if (seen.put(v, Boolean.TRUE) != null) continue;
                    Object[] el = Probe.elements(v);
                    boolean entities = declaredEntityElements(a.f);
                    if (!entities && el != null) {
                        int n2 = 0;
                        for (Object e : el) { if (e != null && isEntityLike(e.getClass())) { entities = true; break; } if (++n2 > 64) break; }
                    }
                    if (entities && !known.containsKey(v)) { entityContainers.add(Probe.pathOf(n, a)); known.put(v, Boolean.TRUE); added++; }
                    continue;
                }
                Class<?> c = v.getClass();
                if (Probe.isJdkish(c) || p.isPlayerClass(c) || seen.put(v, Boolean.TRUE) != null) continue;
                if (isEntityLike(c)) continue;
                if (n.depth < maxDepth) q.add(new Probe.Node(v, n, a, n.depth + 1));
            }
        }
        Log.info("Level-rooted entity search: " + seen.size() + " object(s) visited, " + added + " new container(s).");
    }
}
