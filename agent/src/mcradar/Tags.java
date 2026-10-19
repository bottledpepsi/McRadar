package mcradar;

import java.util.ArrayList;
import java.util.IdentityHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Extracting item tags / enchantments without knowing any Minecraft class or field name.
 *
 *  - Up to 1.20.4 an ItemStack carries an NBT CompoundTag. Every NBT tag class prints itself as SNBT in
 *    toString() ({Enchantments:[{id:"minecraft:sharpness",lvl:5s}],Damage:3}) - readable and complete even
 *    on obfuscated builds, so the "tags" text is simply that string and enchantments are parsed out of it.
 *  - From 1.20.5 an ItemStack carries data components. Enchantments live in an object that holds a
 *    Map<Holder,Integer> whose keys print as "...ResourceKey[minecraft:enchantment / minecraft:sharpness]...".
 *    We search the (budget-limited) component object graph for such maps.
 */
final class Tags {
    private Tags() { }

    // {id:"minecraft:sharpness",lvl:5s}  and  {lvl:5s,id:"minecraft:sharpness"}  (and legacy numeric ids: {lvl:5s,id:16s})
    private static final Pattern ID_THEN_LVL = Pattern.compile("id\\s*:\\s*\"?([a-z0-9_.-]+:[a-z0-9_/.-]+|[0-9]+)[sL]?\"?[^{}]*?lvl\\s*:\\s*([0-9]+)");
    private static final Pattern LVL_THEN_ID = Pattern.compile("lvl\\s*:\\s*([0-9]+)[sL]?[^{}]*?id\\s*:\\s*\"?([a-z0-9_.-]+:[a-z0-9_/.-]+|[0-9]+)[sL]?\"?");
    private static final Pattern HOLDER_KEY = Pattern.compile("/\\s*([a-z0-9_.-]+:[a-z0-9_/.-]+)\\s*\\]");
    private static final Pattern PLAIN_ID = Pattern.compile("^[a-z0-9_.-]+:[a-z0-9_/.-]+$");
    private static final Pattern KEY = Pattern.compile("^(item|block|tile)\\.[A-Za-z0-9_.:]+$");

    static boolean looksLikeItemKey(String s) { return s != null && s.length() < 120 && KEY.matcher(s).matches(); }

    /** "item.minecraft.diamond_sword" -> "minecraft:diamond_sword";  legacy "item.diamondSword" -> "diamondSword". */
    static String idFromKey(String key) {
        String[] parts = key.split("\\.");
        if (parts.length >= 3) {
            // item.<ns>.<path...>   (potions etc. have extra segments: item.minecraft.potion.effect.healing)
            return parts[1] + ":" + join(parts, 2);
        }
        return parts.length == 2 ? parts[1] : key;
    }

    private static String join(String[] p, int from) {
        StringBuilder b = new StringBuilder();
        for (int i = from; i < p.length; i++) { if (b.length() > 0) b.append('.'); b.append(p[i]); }
        return b.toString();
    }

    /** Parses SNBT-style text for enchantment compounds. Appends "id@level" strings to `out`. */
    static void enchantsFromText(String s, Set<String> out) {
        if (s == null || s.length() < 8) return;
        Matcher m = ID_THEN_LVL.matcher(s);
        while (m.find()) out.add(m.group(1) + "@" + m.group(2));
        m = LVL_THEN_ID.matcher(s);
        while (m.find()) out.add(m.group(2) + "@" + m.group(1));
    }

    /** Structured search for Map<Holder|ResourceKey, Number> enchantment maps inside a component object graph. */
    static void findEnchantMaps(Probe p, Object o, Set<String> out) {
        search(p, o, 0, new int[] {3000}, new IdentityHashMap<Object, Boolean>(), out);
    }

    private static void search(Probe p, Object o, int depth, int[] budget, IdentityHashMap<Object, Boolean> seen, Set<String> out) {
        if (o == null || depth > 7 || budget[0]-- <= 0) return;
        Class<?> c = o.getClass();
        if (o instanceof CharSequence || o instanceof Number || o instanceof Boolean || c.isPrimitive()) return;
        if (seen.put(o, Boolean.TRUE) != null) return;
        try {
            if (o instanceof Map) {
                Map<?, ?> m = (Map<?, ?>) o;
                int n = 0;
                for (Map.Entry<?, ?> e : m.entrySet()) {
                    if (n++ > 64) break;
                    String id = enchantId(e.getKey());
                    if (id != null && e.getValue() instanceof Number) out.add(id + "@" + ((Number) e.getValue()).intValue());
                    else search(p, e.getValue(), depth + 1, budget, seen, out);
                }
                return;
            }
            if (o instanceof Optional) { search(p, ((Optional<?>) o).orElse(null), depth + 1, budget, seen, out); return; }
            if (o instanceof Iterable) {
                int n = 0;
                for (Object x : (Iterable<?>) o) { if (n++ > 64) break; search(p, x, depth + 1, budget, seen, out); }
                return;
            }
            if (o instanceof Object[]) {
                int n = 0;
                for (Object x : (Object[]) o) { if (n++ > 64) break; search(p, x, depth + 1, budget, seen, out); }
                return;
            }
            if (Probe.isJdkish(c)) return;
            if (p.isEntityClass(c)) return;           // a stack can point back at its entity -> never wander into the world
            for (Acc a : p.refFieldsOf(c)) search(p, a.getObj(o), depth + 1, budget, seen, out);
        } catch (Throwable ignored) { }
    }

    /** Id of an enchantment map key (Holder / ResourceKey / ResourceLocation / String), else null. */
    private static String enchantId(Object key) {
        if (key == null) return null;
        try {
            String s = String.valueOf(key);
            Matcher m = HOLDER_KEY.matcher(s);
            String last = null;
            while (m.find()) last = m.group(1);
            if (last != null && s.contains("enchantment")) return last;
            if (PLAIN_ID.matcher(s).matches() && s.length() < 80) return s;      // plain "minecraft:sharpness" keys
        } catch (Throwable ignored) { }
        return null;
    }

    /** toString() unless it is the useless default Object.toString (pkg.Class@1a2b3c). Capped. */
    static String safeText(Object v, int cap) {
        if (v == null) return "";
        try {
            String s = String.valueOf(v);
            if (s.matches("[\\w.$]+@[0-9a-f]+")) return "";
            if (s.length() > cap) s = s.substring(0, cap) + "...";
            return s.replace('\n', ' ').replace('\r', ' ');
        } catch (Throwable t) { return ""; }
    }

    static List<String> sorted(Set<String> s) { return new ArrayList<String>(new LinkedHashSet<String>(s)); }
}
