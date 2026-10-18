package mcradar;

import java.lang.reflect.Field;
import java.lang.reflect.Modifier;

/** A resolved, type-checked accessor for one field (static or instance). */
final class Acc {
    final Field f;
    final Class<?> owner;
    final boolean isStatic;
    final long off;          // Unsafe offset, or -1 when using reflection
    private final Object base;   // static base for Unsafe static reads
    private final boolean unsafe;

    private Acc(Field f, boolean isStatic, long off, Object base, boolean unsafe) {
        this.f = f; this.owner = f.getDeclaringClass(); this.isStatic = isStatic;
        this.off = off; this.base = base; this.unsafe = unsafe;
    }

    /** Returns null if the field cannot be read safely. */
    static Acc of(Field f) {
        boolean st = Modifier.isStatic(f.getModifiers());
        if (Mem.OK) {
            try {
                long off = st ? Mem.staticFieldOffset(f) : Mem.objectFieldOffset(f);
                Object base = st ? Mem.staticFieldBase(f) : null;
                return new Acc(f, st, off, base, true);
            } catch (Throwable ignored) {
                // e.g. UnsupportedOperationException for records / hidden classes -> reflection below
            }
        }
        if (!st) {   // never read statics via reflection: that could initialise the class
            try {
                f.setAccessible(true);
                return new Acc(f, false, -1, null, false);
            } catch (Throwable ignored) { }
        }
        return null;
    }

    Object getObj(Object o) {
        try {
            if (isStatic) return unsafe ? Mem.getObject(base, off) : null;
            // CRITICAL with Unsafe: the object MUST be an instance of the declaring class, or we
            // would read arbitrary memory. Always verify.
            if (o == null || !owner.isInstance(o)) return null;
            return unsafe ? Mem.getObject(o, off) : f.get(o);
        } catch (Throwable t) { return null; }
    }

    double getDouble(Object o) {
        try {
            if (o == null || !owner.isInstance(o)) return Double.NaN;
            return unsafe ? Mem.getDouble(o, off) : f.getDouble(o);
        } catch (Throwable t) { return Double.NaN; }
    }

    float getFloat(Object o) {
        try {
            if (o == null || !owner.isInstance(o)) return Float.NaN;
            return unsafe ? Mem.getFloat(o, off) : f.getFloat(o);
        } catch (Throwable t) { return Float.NaN; }
    }

    /** Integer field; Integer.MIN_VALUE when unreadable. */
    int getInt(Object o) {
        try {
            if (o == null || !owner.isInstance(o)) return Integer.MIN_VALUE;
            return unsafe ? Mem.getInt(o, off) : f.getInt(o);
        } catch (Throwable t) { return Integer.MIN_VALUE; }
    }

    String describe() {
        return owner.getName() + "." + f.getName() + " : " + f.getType().getName()
                + (isStatic ? " [static]" : "") + (off >= 0 ? " @offset " + off : "");
    }
}
